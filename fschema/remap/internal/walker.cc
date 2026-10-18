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

#include "fschema/remap/internal/walker.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fschema/remap/internal/context.h"
#include "fschema/remap/internal/entity.h"
#include "fschema/remap/internal/item.h"
#include "fschema/remap/internal/nbt_utils.h"
#include "fschema/remap/internal/rule.h"
#include "fschema/remap/internal/text.h"

namespace fschema::remap::internal {

namespace {
using base::NbtCompound;
using base::NbtList;
using base::NbtPayload;
using base::NbtTag;
using base::TagType;

constexpr int kMaxWalkDepth = 64;

// Container lists: index-implied slots with `{}` fillers become non-empty
// items with explicit Slot bytes.
void DowngradePositionalList(RemapContext& ctx,
                             NbtList& list,
                             std::deque<std::string>& scratch,
                             std::string_view path) {
  std::vector<NbtPayload> out;
  out.reserve(list.children.size());
  std::int64_t index = 0;
  for (auto& e : list.children) {
    const std::int64_t slot = index++;
    auto* entry = std::get_if<std::unique_ptr<NbtCompound>>(&e);
    if (entry == nullptr || *entry == nullptr) {
      out.push_back(std::move(e));
      continue;
    }
    if ((*entry)->children.empty()) {
      ++ctx.stats().counters.empty_slots_dropped;
      ctx.Report(Diag::kEmptySlotsDropped, 0, path);
      continue;
    }
    // An entry with an explicit Slot keeps its original slot; sparse
    // lists must never be re-indexed.
    if (NbtTag* slot_tag = FindChild(**entry, "Slot")) {
      const std::optional<std::int64_t> explicit_slot = IntOf(*slot_tag);
      if (explicit_slot.has_value() && *explicit_slot >= 0 &&
          *explicit_slot <= 127) {
        DowngradeItemCompound(ctx, **entry, scratch, path);
        ++ctx.stats().counters.be_slots_preserved;
        ctx.Report(Diag::kBeSlotPreserved, 0, path);
        out.push_back(std::move(e));
        continue;
      }
    }
    DowngradeItemCompound(ctx, **entry, scratch, path);
    if (slot > 127) {
      ++ctx.stats().counters.item_count_clamps;
      ctx.Report(Diag::kItemCountClamped, 0, path);
      continue;
    }
    RemoveChild(**entry, "Slot");
    (*entry)->children.push_back(
        MakeByteTag("Slot", static_cast<std::int8_t>(slot)));
    ++ctx.stats().counters.slots_synthesized;
    ctx.Report(Diag::kSlotsSynthesized, 0, path);
    out.push_back(std::move(e));
  }
  list.children = std::move(out);
}

// Equipment lists (HandItems / ArmorItems): `{}` fillers are positional;
// only the item shells are downgraded.
void DowngradeEquipmentList(RemapContext& ctx,
                            NbtList& list,
                            std::deque<std::string>& scratch,
                            std::string_view path,
                            std::size_t min_size) {
  std::vector<NbtPayload> out;
  out.reserve(list.children.size());
  for (auto& e : list.children) {
    auto* entry = std::get_if<std::unique_ptr<NbtCompound>>(&e);
    if (entry == nullptr || *entry == nullptr) {
      out.push_back(std::move(e));
      continue;
    }
    if (!(*entry)->children.empty()) {
      DowngradeItemCompound(ctx, **entry, scratch, path);
    }
    out.push_back(std::move(e));
  }
  while (out.size() < min_size) {
    out.push_back(NbtPayload{std::make_unique<NbtCompound>()});
  }
  list.children = std::move(out);
}

}  // namespace

void WalkCompoundDeep(RemapContext& ctx,
                      NbtCompound& c,
                      std::deque<std::string>& scratch,
                      std::string_view path,
                      int depth,
                      bool entity_root) {
  if (depth > kMaxWalkDepth) return;

  if (entity_root) {
    if (ctx.EntityStructFamilyActive()) {
      DowngradeEntityFields(ctx, c, scratch, path);
      DowngradeEntityStructure(ctx, c, path, scratch);
    } else if (FindChild(c, "components") != nullptr) {
      ctx.Report(Diag::kEntityComponentsKept, 0x0501, path);
    }
    if (ctx.AttributeFamilyActive()) {
      DowngradeEntityAttributes(ctx, c, path, scratch);
    }
  }

  if (ctx.TextFamilyActive()) {
    DowngradeTextFieldInPlace(ctx, c, "CustomName", 0x0301, path, scratch);
    DowngradeTextFieldInPlace(ctx, c, "CustomName2", 0x0302, path, scratch);
  }

  for (auto& child : c.children) {
    const std::string_view name = child.name;
    if (child.type == TagType::kList) {
      auto* lp = std::get_if<std::unique_ptr<NbtList>>(&child.payload);
      if (lp == nullptr || *lp == nullptr) continue;
      NbtList& list = **lp;
      if (list.element_type != TagType::kCompound) continue;
      if (name == "Items" || name == "Inventory") {
        DowngradePositionalList(ctx, list, scratch, path);
      } else if (name == "HandItems") {
        DowngradeEquipmentList(ctx, list, scratch, path, 2);
      } else if (name == "ArmorItems") {
        DowngradeEquipmentList(ctx, list, scratch, path, 4);
      } else {
        // Passengers (nested entity roots) and generic compound lists.
        for (auto& e : list.children) {
          auto* sub = std::get_if<std::unique_ptr<NbtCompound>>(&e);
          if (sub != nullptr && *sub != nullptr) {
            if (name == "Passengers") {
              // Nested entities must also have their id downgraded.
              (void)DowngradeEntityId(ctx, **sub, scratch, path);
              WalkCompoundDeep(ctx, **sub, scratch, path, depth + 1, true);
            } else {
              WalkCompoundDeep(ctx, **sub, scratch, path, depth + 1, false);
            }
          }
        }
      }
      continue;
    }
    if (child.type == TagType::kCompound) {
      if (name == "equipment") {
        if (!entity_root) {
          ctx.Report(Diag::kFallbackPassthrough, 0x0501, path);
        }
        continue;
      }
      auto* cp = std::get_if<std::unique_ptr<NbtCompound>>(&child.payload);
      if (cp != nullptr && *cp != nullptr) {
        // Entity root's `Item` field always holds a single item, even
        // when its `components` key is absent.
        if (entity_root && name == "Item") {
          DowngradeItemCompound(ctx, **cp, scratch, path);
          continue;
        }
        // BE single-item fields are item compounds; dispatch explicitly
        // so the shell rewrite happens here.
        if (!entity_root &&
            (name == "item" || name == "RecordItem" || name == "Book")) {
          DowngradeItemCompound(ctx, **cp, scratch, path);
          continue;
        }
        WalkCompoundDeep(ctx, **cp, scratch, path, depth + 1, name == "Entity");
      }
    }
  }
}

}  // namespace fschema::remap::internal