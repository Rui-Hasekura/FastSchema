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

#ifndef FSCHEMA_REMAP_RULE_H_
#define FSCHEMA_REMAP_RULE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "fschema/remap/remap.h"

namespace fschema::remap {

// Version window predicate.
//
// Every rule carries `anchor_dv`, the DataVersion at which the semantic
// change landed. A rule is active when the anchor falls inside the
// half-open window between target and source:
//
//   downgrade (target < source): target < anchor <= source
//   upgrade   (source < target): source < anchor <= target

[[nodiscard]] constexpr bool RuleActive(std::int32_t target_dv,
                                        std::int32_t source_dv,
                                        std::int32_t anchor_dv) noexcept {
  if (target_dv < source_dv) {
    return target_dv < anchor_dv && anchor_dv <= source_dv;
  }
  if (source_dv < target_dv) {
    return source_dv < anchor_dv && anchor_dv <= target_dv;
  }
  return false;
}

inline constexpr std::int32_t kDvComponents = 3837;   // item components era
inline constexpr std::int32_t kDvSnbtText = 4325;     // SNBT text components
inline constexpr std::int32_t kDvEntityReorg = 4190;  // entity structure reorg

// Family gates: version-window predicates.
[[nodiscard]] constexpr bool ItemFamilyActive(std::int32_t target_dv,
                                              std::int32_t source_dv,
                                              RemapDirection dir) noexcept {
  return dir == RemapDirection::kDowngrade &&
         RuleActive(target_dv, source_dv, kDvComponents);
}
[[nodiscard]] constexpr bool TextFamilyActive(std::int32_t target_dv,
                                              std::int32_t source_dv,
                                              RemapDirection dir) noexcept {
  return dir == RemapDirection::kDowngrade &&
         RuleActive(target_dv, source_dv, kDvSnbtText);
}
// 1.21.5 entity structure reorg (25w02a+): `equipment` compound
// (ArmorItems / HandItems / body_armor_item merged), `drop_chances`
// (ArmorDropChances / HandDropChances merged), saddle unification.
[[nodiscard]] constexpr bool EntityStructFamilyActive(
    std::int32_t target_dv,
    std::int32_t source_dv,
    RemapDirection dir) noexcept {
  return dir == RemapDirection::kDowngrade &&
         RuleActive(target_dv, source_dv, kDvEntityReorg);
}

// Diagnostic families.
enum class Family : std::uint8_t {
  kInfo = 0,
  kItem = 1,
  kText = 2,
  kAttribute = 3,
  kEntity = 4,
  kBlockEntity = 5,
  kPalette = 6,
  kFallback = 7,
  kUpgrade = 8,
};

// Component -> tag rule table. Rows live in tables/component_map.inc.

enum class ComponentKind : std::uint8_t {
  kText,                // custom_name / lore -> display.Name / display.Lore
  kEnchantments,        // map form -> tag.Enchantments list [{id,lvl}]
  kStoredEnchantments,  // map form -> tag.StoredEnchantments (enchanted books)
  kDamage,              // int -> tag.Damage
  kMaxDamage,           // int -> tag.MaxDamage
  kCustomModelData,     // int (or 1.21.4+ compound -> floats[0]) -> tag
  kUnbreakable,         // byte/compound -> tag.Unbreakable (+ HideFlags bit)
  kCharged,             // present/compound -> tag.Charged:1b
  kChargedProjectiles,  // [{item}] -> tag.ChargedProjectiles + tag.Charged:1b
  kDyedColor,           // {rgb} -> display.color (+ HideFlags bit)
  kMapColor,            // int -> display.MapColor
  kMapId,               // int -> tag.map (filled map id)
  kPotionContents,     // {potion, custom_color, custom_effects} -> tag.Potion /
                       // tag.customPotionColor / tag.custom_potion_effects
  kCustomData,         // compound merged into tag root
  kContainer,          // [{slot,item}] -> tag.Items (+ Slot synthesis)
  kTooltipDisplay,     // {hide_*} -> HideFlags bits
  kTrim,               // {pattern, material} -> tag.Trim
  kFireworks,          // {flight_duration, explosions} -> tag.Fireworks
  kFireworkExplosion,  // explosion body -> tag.Explosion
  kAttributeModifiers,  // modifiers list -> tag.AttributeModifiers
  kCanPlaceOn,          // predicates -> tag.CanPlaceOn [ids]
  kCanBreak,            // predicates -> tag.CanDestroy [ids]
  kProfile,             // {name, id, properties} -> tag.SkullOwner
  kStewEffects,         // [{id, duration}] -> tag.Effects
  kBundleContents,      // [{slot, item}] -> tag.Items (+ Slot float)
  kWritableBook,        // [{raw, filtered?}] -> tag.pages
  kWrittenBook,         // {title, author, pages, ...} -> tag fields
  kLodestoneTracker,    // {target, tracked} -> tag.Lodestone* fields
  kDroppedKnown,        // known component, no 3463 equivalent
  kRenameToField,       // pure rename: field named `rule.target`, placed at
                        // `rule.rename_target`. Payload is moved verbatim.
  kEntityData,          // -> tag.EntityTag (recursive entity)
  kBlockEntityData,     // -> tag.BlockEntityTag (recursive BE, merged)
  kBucketEntityData,    // -> tag.EntityTag (recursive entity, merged)
  kBees,                // -> tag.BlockEntityTag.Bees
  kContainerLoot,       // -> tag.BlockEntityTag.LootTable/LootTableSeed
  kPotDecorations,      // -> tag.BlockEntityTag.sherds
  kBannerPatterns,      // -> tag.BlockEntityTag.Patterns [{Color:int,
                        // Pattern:string}] via GetBannerColorMap() /
                        // GetBannerPatternMap(); id is forced to
                        // "minecraft:banner".
  kMapDecorations,      // {map_key: {x, z, rotation, type}} -> tag.Decorations
                        // [{id, x, z, rot, type}] via GetDecorationTypeMap().
  kAxolotlVariant,
  kTropicalFishData,
};

// Destination compound for kRenameToField results.
enum class RenameTarget : std::uint8_t {
  kItemRoot = 0,        // item root: `item.<target>`
  kDisplay = 1,         // `item.display.<target>`
  kBlockEntityTag = 2,  // `item.tag.BlockEntityTag.<target>`
};

struct ComponentRule {
  std::string_view component;  // "minecraft:custom_name"
  ComponentKind kind;
  std::int32_t anchor_dv;  // earliest source dv the component can appear
  std::uint16_t rule_id;   // stable id for diagnostics

  // kRenameToField only: destination field name + destination compound.
  // Empty for all other kinds.
  std::string_view target;
  RenameTarget rename_target = RenameTarget::kItemRoot;
};

// Sorted by `component` (lexicographic). Lookup: std::lower_bound.
inline constexpr ComponentRule kComponentRules[] = {
#include "fschema/remap/internal/tables/component_map.inc"
};

// Compile-time guard: the .inc paste must stay sorted (for std::lower_bound).
consteval bool ComponentTableSorted() {
  for (std::size_t i = 1; i < std::size(kComponentRules); ++i) {
    if (!(kComponentRules[i - 1].component < kComponentRules[i].component)) {
      return false;
    }
  }
  return true;
}
static_assert(ComponentTableSorted(),
              "component_map.inc must be sorted by component name");

[[nodiscard]] constexpr const ComponentRule* FindComponentRule(
    std::string_view component) noexcept {
  const auto* first = kComponentRules;
  const auto* last = kComponentRules + std::size(kComponentRules);
  const auto* it = std::lower_bound(
      first, last, component, [](const ComponentRule& r, std::string_view key) {
        return r.component < key;
      });
  return (it != last && it->component == component) ? it : nullptr;
}

// Rows sorted by `newer_id`. Downgrade maps newer -> older.
// Names are static storage, so renames are zero-allocation span re-points.

struct IdRenameRule {
  std::int32_t anchor_dv;     // first dv where `newer_id` exists
  std::string_view newer_id;  // e.g. "minecraft:iron_chain"
  std::string_view older_id;  // e.g. "minecraft:chain"
  std::uint16_t rule_id;
};

inline constexpr IdRenameRule kIdRenames[] = {
#include "fschema/remap/internal/tables/block_id_renames.inc"
};

// Lookup by current (source-era) id. Returns nullptr when no rename is
// active for the window (table is sorted by `newer_id`).
consteval bool IdTableSorted() {
  for (std::size_t i = 1; i < std::size(kIdRenames); ++i) {
    if (!(kIdRenames[i - 1].newer_id < kIdRenames[i].newer_id)) return false;
  }
  return true;
}
static_assert(IdTableSorted(),
              "block_id_renames.inc must be sorted by newer_id");

[[nodiscard]] constexpr const IdRenameRule* FindIdRename(
    std::string_view current_id,
    std::int32_t target_dv,
    std::int32_t source_dv) noexcept {
  const auto* first = kIdRenames;
  const auto* last = kIdRenames + std::size(kIdRenames);
  const auto* it = std::lower_bound(
      first, last, current_id, [](const IdRenameRule& r, std::string_view key) {
        return r.newer_id < key;
      });
  if (it == last || it->newer_id != current_id) return nullptr;
  return RuleActive(target_dv, source_dv, it->anchor_dv) ? it : nullptr;
}

// Number of id-rename rows whose anchor falls in the active window.
[[nodiscard]] constexpr std::uint32_t CountActiveIdRenames(
    std::int32_t target_dv,
    std::int32_t source_dv) noexcept {
  std::uint32_t n = 0;
  for (const auto& r : kIdRenames) {
    if (RuleActive(target_dv, source_dv, r.anchor_dv)) ++n;
  }
  return n;
}

// Attribute id rollback table.
// `tables/attribute_renames.inc` provides the rows. `older_id` empty =
// the attribute has no 3463-era equivalent (drop the modifier + diagnose).
// Used by both the item component (minecraft:attribute_modifiers) and the
// entity root "Attributes" list.

struct AttributeRenameRule {
  std::string_view newer_id;  // sorted; source-era id
  std::string_view older_id;  // 3463-era id; empty = no target
  std::int32_t anchor_dv;     // first dv where newer_id exists
  std::uint16_t rule_id;
};

inline constexpr AttributeRenameRule kAttributeRenames[] = {
#include "fschema/remap/internal/tables/attribute_renames.inc"
};

consteval bool AttributeTableSorted() {
  for (std::size_t i = 1; i < std::size(kAttributeRenames); ++i) {
    if (!(kAttributeRenames[i - 1].newer_id < kAttributeRenames[i].newer_id)) {
      return false;
    }
  }
  return true;
}
static_assert(AttributeTableSorted(),
              "attribute_renames.inc must be sorted by newer_id");

// Window-gated lookup by source-era attribute id. Returns nullptr when
// the id is not in the table at all (unknown id: caller keeps it verbatim
// + diagnoses) or when no row is active for the window.
[[nodiscard]] constexpr const AttributeRenameRule* FindAttributeRename(
    std::string_view current_id,
    std::int32_t target_dv,
    std::int32_t source_dv) noexcept {
  const auto* first = kAttributeRenames;
  const auto* last = kAttributeRenames + std::size(kAttributeRenames);
  const auto* it =
      std::lower_bound(first,
                       last,
                       current_id,
                       [](const AttributeRenameRule& r, std::string_view key) {
                         return r.newer_id < key;
                       });
  if (it == last || it->newer_id != current_id) return nullptr;
  return RuleActive(target_dv, source_dv, it->anchor_dv) ? it : nullptr;
}

[[nodiscard]] constexpr std::uint32_t CountActiveAttributeRenames(
    std::int32_t target_dv,
    std::int32_t source_dv) noexcept {
  std::uint32_t n = 0;
  for (const auto& r : kAttributeRenames) {
    if (RuleActive(target_dv, source_dv, r.anchor_dv)) ++n;
  }
  return n;
}

// Status effect string id -> numeric id.
// `tables/potion_effect_ids.inc` provides the rows; numeric values are the
// 1.20.1 (3463) MobEffects registry ids used by legacy Id / EffectId bytes.

struct PotionEffectRule {
  std::string_view id;  // sorted; namespaced source-era id
  std::int32_t numeric;
};

inline constexpr PotionEffectRule kPotionEffectIds[] = {
#include "fschema/remap/internal/tables/potion_effect_ids.inc"
};

consteval bool PotionEffectTableSorted() {
  for (std::size_t i = 1; i < std::size(kPotionEffectIds); ++i) {
    if (!(kPotionEffectIds[i - 1].id < kPotionEffectIds[i].id)) return false;
  }
  return true;
}
static_assert(PotionEffectTableSorted(),
              "potion_effect_ids.inc must be sorted by id");

// Lookup accepts both "minecraft:regeneration" and bare "regeneration".
// Returns nullptr when the effect has no numeric 1.20.1 id.
[[nodiscard]] constexpr const PotionEffectRule* FindPotionEffectId(
    std::string_view id) noexcept {
  std::string_view key = id;
  if (key.size() > 10 && key.substr(0, 10) == "minecraft:") {
    key = key.substr(10);
  }
  const auto* first = kPotionEffectIds;
  const auto* last = kPotionEffectIds + std::size(kPotionEffectIds);
  const auto* it = std::lower_bound(
      first, last, key, [](const PotionEffectRule& r, std::string_view bare) {
        std::string_view rb =
            r.id.size() > 10 && r.id.substr(0, 10) == "minecraft:"
                ? r.id.substr(10)
                : r.id;
        return rb < bare;
      });
  if (it == last) return nullptr;
  std::string_view ib =
      it->id.size() > 10 && it->id.substr(0, 10) == "minecraft:"
          ? it->id.substr(10)
          : it->id;
  return ib == key ? it : nullptr;
}

[[nodiscard]] inline const absl::flat_hash_map<std::string_view, std::int32_t>&
GetBannerColorMap() {
  static const absl::flat_hash_map<std::string_view, std::int32_t> map = {
#include "fschema/remap/internal/tables/banner_colors.inc"
  };
  return map;
}

[[nodiscard]] inline const absl::flat_hash_map<std::string_view,
                                               std::string_view>&
GetBannerPatternMap() {
  static const absl::flat_hash_map<std::string_view, std::string_view> map = {
#include "fschema/remap/internal/tables/banner_patterns.inc"
  };
  return map;
}

[[nodiscard]] inline const absl::flat_hash_map<std::string_view, std::int8_t>&
GetDecorationTypeMap() {
  static const absl::flat_hash_map<std::string_view, std::int8_t> map = {
#include "fschema/remap/internal/tables/decoration_types.inc"
  };
  return map;
}

[[nodiscard]] inline const absl::flat_hash_map<std::string_view, std::int32_t>&
GetBeaconEffectMap() {
  static const absl::flat_hash_map<std::string_view, std::int32_t> map = {
#include "fschema/remap/internal/tables/beacon_effects.inc"
  };
  return map;
}

}  // namespace fschema::remap

#endif  // FSCHEMA_REMAP_RULE_H_