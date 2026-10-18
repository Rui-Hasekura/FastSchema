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

// FastSchema remap: entity structural rollback.
//
// Source form: `equipment` and `drop_chances` compounds keyed by slot, plus
// an optional entity `components` wrapper.
//
// Target form: HandItems / ArmorItems lists, HandDropChances /
// ArmorDropChances lists, root-level ArmorItem / SaddleItem compounds, and
// the Saddle byte.
//
// Items are moved, not converted here; the generic deep walk converts each
// item's component form afterwards via the resulting lists / compounds.
#include "fschema/remap/internal/entity.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "fschema/remap/internal/context.h"
#include "fschema/remap/internal/item.h"
#include "fschema/remap/internal/nbt_utils.h"
#include "fschema/remap/internal/rule.h"
#include "fschema/remap/internal/walker.h"

namespace fschema::remap::internal {

namespace {

using base::NbtCompound;
using base::NbtList;
using base::NbtPayload;
using base::NbtTag;
using base::TagType;

constexpr float kDefaultDropChance = 0.085f;

// Returns true for horse-like mobs that store horse armor / llama carpet
// in a root-level ArmorItem compound.
bool IsHorseFamily(std::string_view id) noexcept {
  return id == "minecraft:horse" || id == "minecraft:donkey" ||
         id == "minecraft:mule" || id == "minecraft:skeleton_horse" ||
         id == "minecraft:zombie_horse" || id == "minecraft:llama" ||
         id == "minecraft:trader_llama" || id == "minecraft:camel";
}

// Returns true for mobs whose saddle state is the boolean Saddle field.
bool IsFlagSaddleMob(std::string_view id) noexcept {
  return id == "minecraft:pig" || id == "minecraft:strider";
}

// Returns true when the attribute id already uses the legacy prefix form
// (minecraft:generic. / player. / zombie. / horse.).
bool HasLegacyAttrPrefix(std::string_view id) noexcept {
  constexpr std::string_view kGeneric = "minecraft:generic.";
  constexpr std::string_view kPlayer = "minecraft:player.";
  constexpr std::string_view kZombie = "minecraft:zombie.";
  constexpr std::string_view kHorse = "minecraft:horse.";
  return id.substr(0, kGeneric.size()) == kGeneric ||
         id.substr(0, kPlayer.size()) == kPlayer ||
         id.substr(0, kZombie.size()) == kZombie ||
         id.substr(0, kHorse.size()) == kHorse;
}

// Moves the equipment entry named `key` into slot `index` of `list`,
// keeping positional {} fillers for absent slots. The item is not
// converted here; the deep walk handles it via the resulting list.
void FillEquipmentSlot(NbtCompound& eq,
                       std::string_view key,
                       NbtList& list,
                       std::size_t index) noexcept {
  while (list.children.size() <= index) {
    list.children.push_back(NbtPayload{std::make_unique<NbtCompound>()});
  }
  std::unique_ptr<NbtCompound> item = TakeCompoundChild(eq, key);
  if (item == nullptr) return;  // keep the {} filler
  list.children[index] = NbtPayload{std::move(item)};
}

std::optional<float> ChanceOf(const NbtCompound& dc, std::string_view key) {
  const NbtTag* t = FindChild(dc, key);
  if (t == nullptr) return std::nullopt;
  if (const auto f = FloatOf(*t)) return static_cast<float>(*f);
  if (const auto i = IntOf(*t)) return static_cast<float>(*i);
  return std::nullopt;
}

// Writes an IntArray[3] as three Int fields named <prefix>X/Y/Z into
// `dst`. Returns false when `src` is not an IntArray[3].
bool WriteBlockPosPrefixed(const NbtTag& src,
                           NbtCompound& dst,
                           std::string_view prefix) {
  if (src.type != TagType::kIntArray) return false;
  const auto* raw = std::get_if<std::span<const std::byte>>(&src.payload);
  if (raw == nullptr) return false;
  const std::vector<std::int32_t> ints = ReadIntArray(*raw);
  if (ints.size() != 3) return false;
  std::string x(prefix);
  x += 'X';
  std::string y(prefix);
  y += 'Y';
  std::string z(prefix);
  z += 'Z';
  dst.children.push_back(MakeIntTag(x, ints[0]));
  dst.children.push_back(MakeIntTag(y, ints[1]));
  dst.children.push_back(MakeIntTag(z, ints[2]));
  return true;
}

}  // namespace

ParseResult<void> DowngradeEntityId(RemapContext& ctx,
                                    NbtCompound& root,
                                    std::deque<std::string>& scratch,
                                    std::string_view path) {
  (void)scratch;
  NbtTag* id = FindChild(root, "id");
  if (id == nullptr || id->type != TagType::kString) return {};
  const std::string_view cur = AsString(*id);

  // Split <id>_boat / <id>_chest_boat / <id>_raft / <id>_chest_raft into
  // Type:<id> + minecraft:boat / minecraft:chest_boat.
  {
    constexpr std::string_view kPrefix = "minecraft:";
    if (cur.starts_with(kPrefix)) {
      const std::string_view rest = cur.substr(kPrefix.size());
      constexpr std::string_view kChestRaft = "_chest_raft";
      constexpr std::string_view kChestBoat = "_chest_boat";
      constexpr std::string_view kRaft = "_raft";
      constexpr std::string_view kBoat = "_boat";

      std::string_view type;
      std::string_view target_id;
      if (rest.ends_with(kChestRaft)) {
        type = rest.substr(0, rest.size() - kChestRaft.size());
        target_id = "minecraft:chest_boat";
      } else if (rest.ends_with(kChestBoat)) {
        type = rest.substr(0, rest.size() - kChestBoat.size());
        target_id = "minecraft:chest_boat";
      } else if (rest.ends_with(kRaft)) {
        type = rest.substr(0, rest.size() - kRaft.size());
        target_id = "minecraft:boat";
      } else if (rest.ends_with(kBoat)) {
        type = rest.substr(0, rest.size() - kBoat.size());
        target_id = "minecraft:boat";
      }

      if (!type.empty() && !target_id.empty()) {
        // `type` still points into the source owner buffer; swapping
        // id->payload to a static literal does not invalidate it.
        id->payload = target_id;
        RemoveChild(root, "Type");
        root.children.push_back(StringTag("Type", type));
        ++ctx.stats().counters.entity_boats_split;
        ctx.Report(Diag::kEntityBoatSplit, 0, path);
        return {};
      }
    }
  }

  if (ctx.IdRenameFamilyActive()) {
    if (const IdRenameRule* r = FindIdRename(cur, ctx.tgt_dv(), ctx.src_dv())) {
      id->payload = r->older_id;
      ctx.Report(Diag::kBlockIdRenamed, r->rule_id, path);
    }
  }
  return {};
}

void DowngradeEntityFields(RemapContext& ctx,
                           NbtCompound& root,
                           std::deque<std::string>& scratch,
                           std::string_view path) {
  (void)scratch;  // no int-array scratch needed here

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

    struct SimpleRename {
      std::string_view from;
      std::string_view to;
    };
    static constexpr SimpleRename kRenames[] = {
        {"has_egg", "HasEgg"},
        {"life_ticks", "LifeTicks"},
        {"size", "Size"},
        {"fall_distance", "FallDistance"},
    };
    bool renamed = false;
    for (const auto& r : kRenames) {
      if (name == r.from) {
        replace_named(r.to);
        NbtTag moved;
        moved.type = child.type;
        moved.name = r.to;
        moved.payload = std::move(child.payload);
        out.push_back(std::move(moved));
        renamed = true;
        break;
      }
    }
    if (renamed) continue;

    struct PosSplit {
      std::string_view from;
      std::string_view prefix;
    };
    static constexpr PosSplit kPosSplits[] = {
        {"anchor_pos", "A"},
        {"block_pos", "Tile"},
        {"bound_pos", "Bound"},
        {"home_pos", "HomePos"},
        {"sleeping_pos", "Sleeping"},
        {"patrol_target", "Patrol"},
        {"wander_target", "Wander"},
        {"leash", "Leash"},
    };
    bool split = false;
    for (const auto& p : kPosSplits) {
      if (name != p.from) continue;
      NbtCompound pos;
      if (!WriteBlockPosPrefixed(child, pos, p.prefix)) {
        ctx.Report(Diag::kEntityFieldNoTarget, 0, path);
        break;  // keep the source field verbatim
      }
      std::string px(p.prefix);
      px += 'X';
      std::string py(p.prefix);
      py += 'Y';
      std::string pz(p.prefix);
      pz += 'Z';
      replace_named(px);
      replace_named(py);
      replace_named(pz);
      for (auto& f : pos.children) out.push_back(std::move(f));
      split = true;
      break;
    }
    if (split) continue;

    if (name == "flower_pos" || name == "hive_pos") {
      const std::string_view target =
          name == "flower_pos" ? "FlowerPos" : "HivePos";
      NbtCompound pos;
      if (MakeBlockPosCompound(child, pos)) {
        replace_named(target);
        out.push_back(MakeCompoundTag(
            target, std::make_unique<NbtCompound>(std::move(pos))));
        continue;
      }
      ctx.Report(Diag::kEntityFieldNoTarget, 0, path);
      // fall through to keep the source field verbatim
    }

    out.push_back(std::move(child));
  }

  root.children = std::move(out);
}

void DowngradeEntityStructure(RemapContext& ctx,
                              NbtCompound& root,
                              std::string_view path,
                              std::deque<std::string>& scratch) {
  std::string_view ent_id;
  if (const NbtTag* idt = FindChild(root, "id")) ent_id = AsString(*idt);

  // Entity data components wrapper: kept verbatim and diagnosed.
  if (FindChild(root, "components") != nullptr) {
    ctx.Report(Diag::kEntityComponentsKept, 0x0501, path);
  }

  // equipment compound -> HandItems / ArmorItems / ArmorItem /
  // SaddleItem / Saddle.
  if (std::unique_ptr<NbtCompound> eq = TakeCompoundChild(root, "equipment");
      eq != nullptr) {
    ++ctx.stats().counters.entity_equipment_rewritten;
    ctx.Report(Diag::kEntityEquipmentRewritten, 0x0501, path);

    NbtList& hand = EnsureList(root, "HandItems", TagType::kCompound);
    FillEquipmentSlot(*eq, "mainhand", hand, 0);
    FillEquipmentSlot(*eq, "offhand", hand, 1);

    NbtList& armor = EnsureList(root, "ArmorItems", TagType::kCompound);
    FillEquipmentSlot(*eq, "feet", armor, 0);
    FillEquipmentSlot(*eq, "legs", armor, 1);
    if (IsHorseFamily(ent_id)) {
      // Horse-family mobs store the chest item as a root-level ArmorItem
      // compound, not as ArmorItems[2].
      if (std::unique_ptr<NbtCompound> horse_armor =
              TakeCompoundChild(*eq, "chest");
          horse_armor != nullptr) {
        DowngradeItemCompound(ctx, *horse_armor, scratch, path);
        RemoveChild(root, "ArmorItem");
        root.children.push_back(
            MakeCompoundTag("ArmorItem", std::move(horse_armor)));
      }
    } else {
      FillEquipmentSlot(*eq, "chest", armor, 2);
    }
    FillEquipmentSlot(*eq, "head", armor, 3);

    // Body slot: no target-era equivalent.
    if (TakeCompoundChild(*eq, "body") != nullptr) {
      ++ctx.stats().counters.entity_fields_dropped;
      std::string body_path = std::string(path) + ".equipment.body";
      ctx.Report(Diag::kEntityFieldNoTarget, 0x0503, body_path);
    }

    // saddle: horse-like -> SaddleItem + Saddle:1b; pig/strider -> Saddle:1b.
    if (std::unique_ptr<NbtCompound> saddle = TakeCompoundChild(*eq, "saddle");
        saddle != nullptr) {
      ctx.Report(Diag::kEntitySaddleRewritten, 0x0504, path);
      RemoveChild(root, "Saddle");
      root.children.push_back(MakeByteTag("Saddle", 1));
      if (!IsFlagSaddleMob(ent_id)) {
        DowngradeItemCompound(ctx, *saddle, scratch, path);
        RemoveChild(root, "SaddleItem");
        root.children.push_back(
            MakeCompoundTag("SaddleItem", std::move(saddle)));
      }
      // Flag-saddle mobs keep only the boolean; the item is not preserved.
    }

    // Any remaining unrecognized equipment keys: dropped + diagnosed.
    for (const auto& k : eq->children) {
      (void)k;
      ++ctx.stats().counters.entity_fields_dropped;
      ctx.Report(Diag::kEntityFieldNoTarget, 0x0505, path);
    }
  }

  // drop_chances compound -> HandDropChances / ArmorDropChances.
  if (std::unique_ptr<NbtCompound> dc = TakeCompoundChild(root, "drop_chances");
      dc != nullptr) {
    ++ctx.stats().counters.entity_drop_chances_rewritten;
    ctx.Report(Diag::kEntityDropChancesRewritten, 0x0506, path);

    NbtList& hand = EnsureList(root, "HandDropChances", TagType::kFloat);
    while (hand.children.size() < 2) {
      hand.children.push_back(NbtPayload{kDefaultDropChance});
    }
    hand.children[0] =
        NbtPayload{ChanceOf(*dc, "mainhand").value_or(kDefaultDropChance)};
    hand.children[1] =
        NbtPayload{ChanceOf(*dc, "offhand").value_or(kDefaultDropChance)};

    NbtList& armor = EnsureList(root, "ArmorDropChances", TagType::kFloat);
    while (armor.children.size() < 4) {
      armor.children.push_back(NbtPayload{kDefaultDropChance});
    }
    armor.children[0] =
        NbtPayload{ChanceOf(*dc, "feet").value_or(kDefaultDropChance)};
    armor.children[1] =
        NbtPayload{ChanceOf(*dc, "legs").value_or(kDefaultDropChance)};
    armor.children[2] =
        NbtPayload{ChanceOf(*dc, "chest").value_or(kDefaultDropChance)};
    armor.children[3] =
        NbtPayload{ChanceOf(*dc, "head").value_or(kDefaultDropChance)};

    if (FindChild(*dc, "body") != nullptr ||
        FindChild(*dc, "saddle") != nullptr) {
      ++ctx.stats().counters.entity_fields_dropped;
      ctx.Report(Diag::kEntityFieldNoTarget, 0x0507, path);
    }
  }
}

void DowngradeEntityAttributes(RemapContext& ctx,
                               NbtCompound& root,
                               std::string_view path,
                               std::deque<std::string>& scratch) {
  NbtTag* attrs = FindChild(root, "Attributes");
  if (attrs == nullptr) return;
  NbtList* list = AsListMutable(*attrs);
  if (list == nullptr) return;

  std::vector<NbtPayload> kept;
  kept.reserve(list->children.size());
  for (auto& e : list->children) {
    auto* ep = std::get_if<std::unique_ptr<NbtCompound>>(&e);
    if (ep == nullptr || *ep == nullptr) {
      kept.push_back(std::move(e));
      continue;
    }
    NbtCompound& a = **ep;

    if (FindChild(a, "Name") == nullptr) {
      if (NbtTag* id_tag = FindChild(a, "id"); id_tag != nullptr) {
        a.children.push_back(StringTag("Name", AsString(*id_tag)));
        RemoveChild(a, "id");
      } else if (NbtTag* type_tag = FindChild(a, "type"); type_tag != nullptr) {
        a.children.push_back(StringTag("Name", AsString(*type_tag)));
        RemoveChild(a, "type");
      }

      if (NbtTag* base_tag = FindChild(a, "base"); base_tag != nullptr) {
        if (const auto f = FloatOf(*base_tag)) {
          a.children.push_back(MakeDoubleTag("Base", *f));
        } else if (const auto i = IntOf(*base_tag)) {
          a.children.push_back(MakeDoubleTag("Base", static_cast<double>(*i)));
        }
        RemoveChild(a, "base");
      } else if (NbtTag* amount_tag = FindChild(a, "amount");
                 amount_tag != nullptr) {
        if (const auto f = FloatOf(*amount_tag)) {
          a.children.push_back(MakeDoubleTag("Base", *f));
        } else if (const auto i = IntOf(*amount_tag)) {
          a.children.push_back(MakeDoubleTag("Base", static_cast<double>(*i)));
        }
        RemoveChild(a, "amount");
      }
    }

    // Field rename is structural, independent of whether `Name` already
    // exists on the source side.
    if (NbtTag* mods = FindChild(a, "modifiers")) {
      mods->name = "Modifiers";
    }

    if (NbtTag* name_tag = FindChild(a, "Name")) {
      const std::string_view cur = AsString(*name_tag);
      if (const AttributeRenameRule* r =
              FindAttributeRename(cur, ctx.tgt_dv(), ctx.src_dv())) {
        if (r->older_id.empty()) {
          ++ctx.stats().counters.attribute_no_target;
          ctx.Report(Diag::kAttributeNoTarget, r->rule_id, path);
          continue;  // drop the modifier entirely
        }
        if (r->older_id == "minecraft:generic.scale" && ctx.tgt_dv() < 3801) {
          ++ctx.stats().counters.attribute_no_target;
          ctx.Report(Diag::kAttributeNoTarget, r->rule_id, path);
          continue;  // drop the modifier entirely
        }
        name_tag->payload = r->older_id;
        ++ctx.stats().counters.attribute_renames;
        ctx.Report(Diag::kAttributeRenamed, r->rule_id, path);
      } else if (!HasLegacyAttrPrefix(cur)) {
        ++ctx.stats().counters.attribute_no_target;
        ctx.Report(Diag::kAttributeNoTarget, 0, path);
      }
    }

    // Modifiers[].Id: string form -> IntArray form.
    if (NbtTag* mods = FindChild(a, "Modifiers"); mods != nullptr) {
      if (NbtList* ml = AsListMutable(*mods)) {
        for (auto& me : ml->children) {
          auto* mp = std::get_if<std::unique_ptr<NbtCompound>>(&me);
          if (mp == nullptr || *mp == nullptr) continue;
          NbtCompound& m = **mp;
          NbtTag* idt = FindChild(m, "Id");
          if (idt == nullptr || idt->type != TagType::kString) continue;
          const std::string_view id = AsString(*idt);
          if (id.empty()) continue;
          std::array<std::int32_t, 4> uuid;
          if (auto parsed = ParseUuidIntArray(id)) {
            uuid = *parsed;
          } else {
            uuid = HashUuidIntArray(id);
            ++ctx.stats().counters.attr_ids_hashed;
            ctx.Report(Diag::kAttrIdHashed, 0, path);
          }
          RemoveChild(m, "Id");
          m.children.push_back(
              MakeIntArrayTag("Id", uuid.data(), uuid.size(), scratch));
        }
      }
    }
    kept.push_back(std::move(e));
  }
  list->children = std::move(kept);
}

ParseResult<void> DowngradeEntityCompound(RemapContext& ctx,
                                          NbtCompound& root,
                                          std::deque<std::string>& scratch,
                                          std::string_view path) {
  if (auto r = DowngradeEntityId(ctx, root, scratch, path); !r) return r;
  WalkCompoundDeep(ctx, root, scratch, path, 0, /*entity_root=*/true);
  return {};
}

ParseResult<void> RemapEntity(RemapContext& ctx,
                              ir::Entity& ent,
                              std::string_view path) {
  ++ctx.stats().counters.roots_scanned;

  bool hit = false;
  if (ctx.ItemFamilyActive()) {
    hit = ContainsPattern(ent.raw_nbt, "components");
  }
  if (!hit && ctx.TextFamilyActive()) {
    hit = ContainsPattern(ent.raw_nbt, "CustomName");
  }
  if (!hit && ctx.EntityStructFamilyActive()) {
    hit = ContainsPattern(ent.raw_nbt, "equipment") ||
          ContainsPattern(ent.raw_nbt, "drop_chances");
  }
  if (!hit && ctx.AttributeFamilyActive()) {
    hit = ContainsPattern(ent.raw_nbt, "Attributes");
  }
  // Boat split is not gated by any family; the root is materialized
  // whenever a concrete boat/raft id is present.
  if (!hit) {
    hit = ContainsPattern(ent.raw_nbt, "_boat") ||
          ContainsPattern(ent.raw_nbt, "_raft");
  }
  if (!hit) return {};

  auto tree_res = MaterializeCompound(ent.raw_nbt);
  if (!tree_res) return std::unexpected(tree_res.error());
  ++ctx.stats().counters.roots_materialized;

  std::deque<std::string> scratch;
  if (auto r = DowngradeEntityCompound(ctx, *tree_res, scratch, path); !r) {
    return r;
  }

  auto bytes = SerializeCompoundBody(*tree_res);
  if (auto span = ctx.PersistBytes(bytes); !span.empty()) {
    ent.raw_nbt = span;
  }
  return {};
}

}  // namespace fschema::remap::internal