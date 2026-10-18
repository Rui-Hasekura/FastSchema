// Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fschema/remap/internal/block_entity.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fschema/remap/internal/component_extra.h"
#include "fschema/remap/internal/context.h"
#include "fschema/remap/internal/entity.h"
#include "fschema/remap/internal/item.h"
#include "fschema/remap/internal/nbt_utils.h"
#include "fschema/remap/internal/rule.h"
#include "fschema/remap/internal/text.h"
#include "fschema/remap/internal/walker.h"

namespace fschema::remap::internal {

namespace {
using base::NbtCompound;
using base::NbtList;
using base::NbtPayload;
using base::NbtTag;
using base::TagType;

struct TileEntityIdGuessRule {
  std::string_view primary;
  std::array<std::string_view, 2> additional;
  std::uint8_t additional_count;
  std::string_view id;
};

inline constexpr TileEntityIdGuessRule kTileEntityIdGuesses[] = {
#include "fschema/remap/internal/tables/tile_entity_id_guesses.inc"
};

// Synthesizes a BE id when none is present, using a fixed heuristic table.
void EnsureTileEntityId(RemapContext& ctx,
                        NbtCompound& root,
                        std::string_view path) {
  if (NbtTag* id = FindChild(root, "id")) {
    if (id->type == TagType::kString && !AsString(*id).empty()) return;
    RemoveChild(root, "id");
  }

  if (NbtTag* id = FindChild(root, "Id"); id != nullptr) {
    const std::string_view v = AsString(*id);
    RemoveChild(root, "Id");
    root.children.push_back(StringTag("id", v));
    return;
  }

  for (const auto& rule : kTileEntityIdGuesses) {
    if (FindChild(root, rule.primary) == nullptr) continue;
    bool all_match = true;
    for (std::size_t i = 0; i < rule.additional_count; ++i) {
      if (FindChild(root, rule.additional[i]) == nullptr) {
        all_match = false;
        break;
      }
    }
    if (!all_match) continue;
    root.children.push_back(StringTag("id", rule.id));
    ++ctx.stats().counters.be_ids_guessed;
    ctx.Report(Diag::kBeIdGuessed, 0, path);
    return;
  }

  root.children.push_back(StringTag("id", ""));
  ++ctx.stats().counters.be_ids_unguessed;
  ctx.Report(Diag::kBeIdUnguessed, 0, path);
}

// BE root field-level downgrades.
void DowngradeBlockEntityFields(RemapContext& ctx,
                                NbtCompound& root,
                                std::deque<std::string>& scratch,
                                std::string_view path) {
  std::vector<NbtTag> out;
  out.reserve(root.children.size());

  auto replace_named = [&out](std::string_view target) {
    out.erase(
        std::remove_if(out.begin(),
                       out.end(),
                       [target](const NbtTag& t) { return t.name == target; }),
        out.end());
  };

  for (auto& child : root.children) {
    const std::string_view name = child.name;

    if (name == "custom_name") {
      replace_named("CustomName");
      NbtTag moved;
      moved.type = child.type;
      moved.name = "CustomName";
      moved.payload = std::move(child.payload);
      out.push_back(std::move(moved));
      continue;
    }

    if (name == "ticks_since_song_started") {
      std::int64_t tick = 0;
      if (child.type == TagType::kLong) {
        if (const auto* lp = std::get_if<std::int64_t>(&child.payload)) {
          tick = *lp;
        }
      } else if (const auto i = IntOf(child)) {
        tick = *i;
      }
      replace_named("RecordStartTick");
      replace_named("TickCount");
      replace_named("IsPlaying");
      out.push_back(MakeLongTag("RecordStartTick", 0));
      out.push_back(MakeLongTag("TickCount", tick));
      out.push_back(MakeByteTag("IsPlaying", 0));
      continue;
    }

    if (name == "flower_pos" || name == "exit_portal") {
      const std::string_view target =
          name == "flower_pos" ? "FlowerPos" : "ExitPortal";
      NbtCompound pos;
      if (!MakeBlockPosCompound(child, pos)) {
        ctx.Report(Diag::kBeFieldNoTarget, 0, path);
        out.push_back(std::move(child));
        continue;
      }
      replace_named(target);
      out.push_back(MakeCompoundTag(
          target, std::make_unique<NbtCompound>(std::move(pos))));
      continue;
    }

    if (name == "primary_effect" || name == "secondary_effect") {
      const std::string_view target =
          name == "primary_effect" ? "Primary" : "Secondary";
      std::int32_t effect = 1;
      if (child.type == TagType::kString) {
        const std::string_view id = AsString(child);
        const auto& map = GetBeaconEffectMap();
        const auto it = map.find(id);
        if (it != map.end()) {
          effect = it->second;
        } else if (!id.empty()) {
          ctx.Report(Diag::kBeFieldNoTarget, 0, path);
        }
      }
      replace_named(target);
      out.push_back(MakeIntTag(target, effect));
      continue;
    }

    if (name == "patterns" && child.type == TagType::kList) {
      NbtList* src = AsListMutable(child);
      if (src != nullptr && src->element_type == TagType::kCompound) {
        replace_named("Patterns");
        auto list = std::make_unique<NbtList>();
        list->element_type = TagType::kCompound;
        WriteBannerPatternsListInto(ctx, *list, *src, 0, path);
        NbtTag list_tag;
        list_tag.type = TagType::kList;
        list_tag.name = "Patterns";
        list_tag.payload = std::move(list);
        out.push_back(std::move(list_tag));
        continue;
      }
    }

    if (name == "bees" && child.type == TagType::kList) {
      NbtList* src = AsListMutable(child);
      if (src != nullptr && src->element_type == TagType::kCompound) {
        replace_named("Bees");
        auto list = std::make_unique<NbtList>();
        list->element_type = TagType::kCompound;
        WriteBeesListInto(ctx, *list, *src, 0, scratch, path);
        NbtTag list_tag;
        list_tag.type = TagType::kList;
        list_tag.name = "Bees";
        list_tag.payload = std::move(list);
        out.push_back(std::move(list_tag));
        continue;
      }
    }

    if (name == "profile" && child.type == TagType::kCompound) {
      NbtCompound tmp;
      if (WriteSkullOwner(ctx, child, tmp, "SkullOwner", scratch, 0, path)) {
        replace_named("SkullOwner");
        for (auto& f : tmp.children) {
          out.push_back(std::move(f));
        }
        continue;
      }
    }

    out.push_back(std::move(child));
  }

  root.children = std::move(out);
}

}  // namespace

// Converts one bees list into the target-era `Bees` list. Output rows are
// {TicksInHive:int, MinOccupationTicks:int, EntityData:Compound}.
void WriteBeesListInto(RemapContext& ctx,
                       NbtList& dst,
                       NbtList& src,
                       std::uint16_t rule_id,
                       std::deque<std::string>& scratch,
                       std::string_view path) {
  (void)rule_id;
  for (auto& e : src.children) {
    auto* ep = std::get_if<std::unique_ptr<NbtCompound>>(&e);
    if (ep == nullptr || *ep == nullptr) continue;
    NbtCompound& src_row = **ep;

    NbtCompound& dst_row = AppendListElementCompound(dst);

    {
      std::int32_t v = 0;
      if (const NbtTag* t = FindChild(src_row, "ticks_in_hive"); t != nullptr) {
        if (const auto i = IntOf(*t)) v = static_cast<std::int32_t>(*i);
      }
      dst_row.children.push_back(MakeIntTag("TicksInHive", v));
    }
    {
      std::int32_t v = 0;
      if (const NbtTag* t = FindChild(src_row, "min_ticks_in_hive");
          t != nullptr) {
        if (const auto i = IntOf(*t)) v = static_cast<std::int32_t>(*i);
      }
      dst_row.children.push_back(MakeIntTag("MinOccupationTicks", v));
    }

    if (std::unique_ptr<NbtCompound> ed =
            TakeCompoundChild(src_row, "entity_data");
        ed != nullptr) {
      if (auto r = DowngradeEntityCompound(ctx, *ed, scratch, path); !r) {
        dst_row.children.push_back(
            MakeCompoundTag("EntityData", std::make_unique<NbtCompound>()));
      } else {
        dst_row.children.push_back(
            MakeCompoundTag("EntityData", std::move(ed)));
      }
    } else {
      dst_row.children.push_back(
          MakeCompoundTag("EntityData", std::make_unique<NbtCompound>()));
    }
  }
}

// Converts one banner-pattern list into the target-era `Patterns` list.
// Output rows are {Color:int, Pattern:string}; unknown color -> 0, unknown
// pattern id -> "b", both diagnosed as kBeFieldNoTarget.
void WriteBannerPatternsListInto(RemapContext& ctx,
                                 NbtList& dst,
                                 const NbtList& src,
                                 std::uint16_t rule_id,
                                 std::string_view path) {
  const auto& color_map = GetBannerColorMap();
  const auto& pattern_map = GetBannerPatternMap();
  for (const auto& e : src.children) {
    const auto* ep = std::get_if<std::unique_ptr<NbtCompound>>(&e);
    if (ep == nullptr || *ep == nullptr) continue;
    const NbtCompound& src_row = **ep;
    NbtCompound& dst_row = AppendListElementCompound(dst);

    std::int32_t color = 0;
    if (const NbtTag* c = FindChild(src_row, "color"); c != nullptr) {
      const std::string_view s = AsString(*c);
      if (auto it = color_map.find(s); it != color_map.end()) {
        color = it->second;
      } else if (!s.empty()) {
        ctx.Report(Diag::kBeFieldNoTarget, rule_id, path);
      }
    }
    dst_row.children.push_back(MakeIntTag("Color", color));

    std::string_view pattern = "b";
    if (const NbtTag* p = FindChild(src_row, "pattern"); p != nullptr) {
      const std::string_view s = AsString(*p);
      if (auto it = pattern_map.find(s); it != pattern_map.end()) {
        pattern = it->second;
      } else if (!s.empty()) {
        ctx.Report(Diag::kBeFieldNoTarget, rule_id, path);
      }
    }
    dst_row.children.push_back(StringTag("Pattern", pattern));
  }
}

ParseResult<void> DowngradeBlockEntityCompound(RemapContext& ctx,
                                               NbtCompound& root,
                                               std::deque<std::string>& scratch,
                                               std::string_view path) {
  // Unwrap the components container.
  if (std::unique_ptr<NbtCompound> wrapper =
          TakeCompoundChild(root, "components");
      wrapper != nullptr) {
    for (auto& f : wrapper->children) {
      root.children.push_back(std::move(f));
    }
    ++ctx.stats().counters.te_wrappers_unwrapped;
  }

  EnsureTileEntityId(ctx, root, path);

  DowngradeBlockEntityFields(ctx, root, scratch, path);
  if (ctx.IdRenameFamilyActive()) {
    if (NbtTag* id = FindChild(root, "id"); id != nullptr) {
      const std::string_view cur = AsString(*id);
      if (const IdRenameRule* r =
              FindIdRename(cur, ctx.tgt_dv(), ctx.src_dv())) {
        id->payload = r->older_id;
        ctx.Report(Diag::kBlockIdRenamed, r->rule_id, path);
      }
    }
  }

  WalkCompoundDeep(ctx, root, scratch, path, 0, /*entity_root=*/false);

  if (ctx.TextFamilyActive()) {
    for (const std::string_view side : {"front_text", "back_text"}) {
      if (NbtTag* ft = FindChild(root, side); ft != nullptr) {
        if (NbtCompound* ftc = AsCompoundMutable(*ft)) {
          if (NbtTag* msgs = FindChild(*ftc, "messages"); msgs != nullptr) {
            if (NbtList* ml = AsListMutable(*msgs)) {
              if (ml->element_type != TagType::kString &&
                  ml->element_type != TagType::kCompound) {
                ctx.Report(
                    Diag::kTextUnsupportedShape,
                    0x0301,
                    std::string(path) + "." + std::string(side) + ".messages");
              } else {
                for (auto& e : ml->children) {
                  if (const auto* sv = std::get_if<std::string_view>(&e)) {
                    // Sign message Strings use the SNBT text anchor.
                    if (ctx.src_dv() >= kDvSnbtText) {
                      scratch.push_back(WrapPlainTextAsJson(*sv));
                      e = NbtPayload{std::string_view(scratch.back())};
                      ++ctx.stats().counters.texts_downgraded;
                    }
                    continue;
                  }
                  if (auto json = StructuredTextToJson(ml->element_type, e)) {
                    scratch.push_back(std::move(*json));
                    e = NbtPayload{std::string_view(scratch.back())};
                    ++ctx.stats().counters.texts_downgraded;
                  } else {
                    ++ctx.stats().counters.text_unsupported_shapes;
                    ctx.Report(Diag::kTextUnsupportedShape,
                               0x0301,
                               std::string(path) + "." + std::string(side) +
                                   ".messages");
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return {};
}

ParseResult<void> RemapBlockEntity(RemapContext& ctx,
                                   ir::BlockEntity& be,
                                   std::string_view path) {
  ++ctx.stats().counters.roots_scanned;

  // Pre-scan: untouched roots keep their zero-copy span.
  bool hit = false;
  if (ctx.ItemFamilyActive()) {
    hit = hit || ContainsPattern(be.raw_nbt, "components") ||
          ContainsPattern(be.raw_nbt, "Items") ||
          ContainsPattern(be.raw_nbt, "Inventory") ||
          ContainsPattern(be.raw_nbt, "item") ||
          ContainsPattern(be.raw_nbt, "RecordItem") ||
          ContainsPattern(be.raw_nbt, "Book");
  }
  if (ctx.TextFamilyActive()) {
    hit = hit || ContainsPattern(be.raw_nbt, "CustomName") ||
          ContainsPattern(be.raw_nbt, "messages") ||
          ContainsPattern(be.raw_nbt, "front_text") ||
          ContainsPattern(be.raw_nbt, "back_text");
  }
  if (ctx.IdRenameFamilyActive() || ctx.EntityStructFamilyActive() ||
      ctx.AttributeFamilyActive()) {
    hit = hit || ContainsPattern(be.raw_nbt, "profile") ||
          ContainsPattern(be.raw_nbt, "patterns") ||
          ContainsPattern(be.raw_nbt, "bees");
  }
  if (!hit) return {};

  auto tree_res = MaterializeCompound(be.raw_nbt);
  if (!tree_res) return std::unexpected(tree_res.error());
  ++ctx.stats().counters.roots_materialized;

  std::deque<std::string> scratch;
  if (auto r = DowngradeBlockEntityCompound(ctx, *tree_res, scratch, path);
      !r) {
    return r;
  }

  auto bytes = SerializeCompoundBody(*tree_res);
  if (auto span = ctx.PersistBytes(bytes); !span.empty()) {
    be.raw_nbt = span;
  }
  return {};
}

}  // namespace fschema::remap::internal