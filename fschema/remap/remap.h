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

#ifndef FSCHEMA_REMAP_REMAP_H_
#define FSCHEMA_REMAP_REMAP_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/container/inlined_vector.h"
#include "fschema/base/error.h"
#include "fschema/ir/types.h"

namespace fschema::remap {

// ---------------------------------------------------------------------------
// Public API of the cross-version remap pass.
//
// Rewrites an in-memory ir::Schema so its data semantics match a target
// Minecraft DataVersion. On success, schema.data_version == target_dv and
// schema.source_version is set to the container version implied by target_dv.
// ---------------------------------------------------------------------------

enum class RemapDirection : std::uint8_t {
  kDowngrade = 0,  // source dv > target dv
  kUpgrade = 1,    // source dv < target dv
};

struct RemapOptions {
  // 0 = derive from the target container version (see
  // `ResolveDataVersionForContainer`). Non-zero = force an explicit target.
  std::int32_t target_data_version = 0;

  // Cap for retained diagnostics; excess diagnostics are only counted.
  std::uint32_t max_diagnostics = 64;

  // Policy for components that have no target-version equivalent
  // (e.g. `minecraft:food` when downgrading to 1.20.1).
  enum class UnmappedComponentPolicy : std::uint8_t {
    kDrop = 0,          // default: drop the component, keep the item
    kMergeIntoTag = 1,  // keep data verbatim under `tag` (non-canonical)
  };
  UnmappedComponentPolicy unmapped_components = UnmappedComponentPolicy::kDrop;
};

// ---------------------------------------------------------------------------
// Diagnostics (soft failures / informational events).
// `RemapStats` carries them; the `std::expected` error channel is reserved
// for hard failures only (unsupported target, corrupted IR).
// ---------------------------------------------------------------------------

enum class Diag : std::uint32_t {
  // 0x00x info
  kNoRulesCoverage = 0x0001,  // window has no implemented rules; data untouched
  kNoOpAtTarget = 0x0002,     // already at target dv

  // 0x20x items / components
  kItemShellRewritten = 0x2001,       // components-form item -> tag-form item
  kItemCountClamped = 0x2002,         // int count clamped into byte Count
  kComponentMapped = 0x2003,          // component successfully mapped to tag(s)
  kComponentDropped = 0x2004,         // unmapped component dropped (see policy)
  kComponentMergedVerbatim = 0x2005,  // unmapped component merged into tag
  kCustomDataCollision = 0x2006,      // custom_data vs component key clash
  kSlotsSynthesized = 0x2007,         // container list index -> Slot byte
  kEmptySlotsDropped = 0x2008,        // {} placeholder removed in tag form
  kHideFlagsFolded = 0x2009,          // show_in_tooltip* -> HideFlags bits
  kEnchantmentLevelClamped = 0x200A,  // level narrowed int -> short
  kContainerToItems = 0x200B,         // minecraft:container -> tag.Items
  kItemIdRenamed = 0x200C,            // item id renamed via palette table
  kCustomModelDataCoerced = 0x200D,   // 1.21.4+ compound -> legacy int
  kPredicateDowngraded = 0x200E,    // block predicate -> plain id list (lossy)
  kEffectNoTarget = 0x200F,         // effect id has no numeric form (dropped)
  kBookFilteredDropped = 0x2010,    // book page filtered text dropped
  kFireworkShapeUnknown = 0x2011,   // unknown firework shape -> Type 0
  kProfileIdDropped = 0x2012,       // unparseable profile id dropped
  kComponentNoTarget = 0x2013,      // known component, no tag-form equivalent
  kFireworkFlightClamped = 0x2014,  // flight_duration clamped into Flight byte
  kFieldMissing = 0x2015,

  // 0x30x text
  kTextDowngraded = 0x3001,        // structured (post-4325) text -> JSON string
  kTextUnsupportedShape = 0x3002,  // text value kept as best-effort JSON

  // 0x40x attributes
  kAttributeRenamed = 0x4001,  // modifier type renamed via section-5 table
  kAttributeNoTarget =
      0x4002,  // attribute has no 3463-era id (dropped / kept-verbatim)
  kAttrModifierDropped = 0x4003,  // malformed modifier entry dropped
  kAttrIdHashed = 0x4004,  // non-UUID id deterministically hashed to UUID
  kAttrSlotBroadened =
      0x4005,  // hand/armor/body/saddle slot broadened to "any"

  // 0x50x entities
  kEntityEquipmentRewritten =
      0x5001,  // equipment compound -> HandItems/ArmorItems
  kEntityDropChancesRewritten =
      0x5002,  // drop_chances -> Hand/ArmorDropChances
  kEntitySaddleRewritten =
      0x5003,  // equipment.saddle -> SaddleItem / Saddle byte
  kEntityFieldNoTarget = 0x5004,   // e.g. body slot: no 3463-era field
  kEntityComponentsKept = 0x5005,  // entity data components kept verbatim
  kEntityBoatSplit = 0x5006,       // <id>_boat/_raft -> Type + generic boat id

  // 0x60x block entities
  kBeSlotPreserved = 0x6001,  // explicit Slot tag kept (not re-indexed)
  kBeFieldNoTarget = 0x6002,  // e.g. banner Patterns ids
  kBeIdGuessed = 0x6003,      // BE id synthesized via heuristic table
  kBeIdUnguessed = 0x6004,    // no id and no heuristic match: empty string

  // 0x70x palette
  kBlockIdRenamed = 0x7001,   // palette entry renamed to target-era id
  kBlockIdNoTarget = 0x7002,  // no target-era id known; id kept verbatim

  // 0x80x fallback
  kFallbackPassthrough = 0x8001,  // generic "kept verbatim + diagnosed"
};

// One diagnostic event. `path` is a stable, dot/bracket style node path
// (e.g. "Regions.main.TileEntities[3].Items[0].components"). Messages are
// NOT stored; render them from `code` at the query site (English only).
struct Diagnostic {
  std::uint32_t code = 0;     // Diag
  std::uint16_t rule_id = 0;  // 0 = table-less / fallback event
  std::string path;           // IR-relative node path
};

struct RemapCounters {
  // palette family
  std::uint32_t blocks_renamed = 0;
  std::uint32_t block_rename_misses = 0;  // unknown ids kept verbatim
  // item family
  std::uint32_t items_rewritten = 0;
  std::uint32_t item_ids_renamed = 0;
  std::uint32_t components_mapped = 0;
  std::uint32_t components_dropped = 0;
  std::uint32_t components_merged_verbatim = 0;
  std::uint32_t custom_data_collisions = 0;
  std::uint32_t hideflags_folded = 0;
  std::uint32_t slots_synthesized = 0;
  std::uint32_t empty_slots_dropped = 0;
  std::uint32_t item_count_clamps = 0;
  std::uint32_t enchant_level_clamps = 0;
  std::uint32_t container_components = 0;
  std::uint32_t components_no_target = 0;  // known component w/o target
  std::uint32_t predicates_downgraded = 0;
  std::uint32_t effects_dropped = 0;
  // attribute family
  std::uint32_t attribute_renames = 0;
  std::uint32_t attribute_no_target = 0;
  std::uint32_t attr_modifiers_dropped = 0;
  std::uint32_t attr_ids_hashed = 0;
  std::uint32_t attr_modifier_displays_dropped = 0;
  // entity structure family
  std::uint32_t entity_equipment_rewritten = 0;
  std::uint32_t entity_drop_chances_rewritten = 0;
  std::uint32_t entity_fields_dropped = 0;
  std::uint32_t entity_boats_split = 0;
  // block entity structure family
  std::uint32_t be_slots_preserved = 0;
  std::uint32_t be_ids_guessed = 0;
  std::uint32_t be_ids_unguessed = 0;
  // text family
  std::uint32_t texts_downgraded = 0;
  std::uint32_t text_unsupported_shapes = 0;
  // block entity shells
  std::uint32_t te_wrappers_unwrapped = 0;
  // pre-scan statistics
  std::uint32_t roots_scanned = 0;
  std::uint32_t roots_materialized = 0;
};

// One bit per diagnostic code, indexed by code value.
struct Watchlist {
  static constexpr std::size_t kBits = 65536;
  std::uint64_t bits[kBits / 64] = {};

  void Set(std::uint32_t code) noexcept {
    if (code < kBits) bits[code >> 6] |= (1ull << (code & 63));
  }
  [[nodiscard]] bool Test(std::uint32_t code) const noexcept {
    return code < kBits && (bits[code >> 6] & (1ull << (code & 63))) != 0;
  }
};

struct RemapStats {
  RemapDirection direction = RemapDirection::kDowngrade;
  std::int32_t source_data_version = 0;
  std::int32_t target_data_version = 0;

  RemapCounters counters{};

  // Bit i == some diagnostic with code i was emitted at least once.
  Watchlist watchlist{};

  absl::InlinedVector<Diagnostic, 8> diagnostics;
  std::uint32_t dropped_diagnostics = 0;  // beyond max_diagnostics

  // Number of rules that were active for the (target, source] window.
  std::uint32_t active_rules = 0;
};

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

// Rewrites `schema` in place to target-version semantics.
// Hard failures (returned via `std::expected`): unsupported target
// container, IR corrupted mid-walk. Everything else is a diagnostic.
[[nodiscard]] ParseResult<RemapStats> RemapToDataVersion(
    ir::Schema& schema,
    std::int32_t target_data_version,
    const RemapOptions& options = {});

// Resolves the effective target DataVersion (explicit remap option, else
// derived from the target container) and remaps only when it differs from
// the source.
[[nodiscard]] ParseResult<void> MaybeRemapForTarget(
    ir::Schema& schema,
    ir::SourceFormat target_format,
    std::int32_t target_container_version,
    const RemapOptions& remap_options,
    RemapStats* stats_out);

// Resolve the canonical Minecraft DataVersion for a container target.
// v6 -> 3463 (1.20).
// v7 / unknown / schem -> 0 (round-trip).
// v5 -> -1 (rejected).
[[nodiscard]] constexpr std::int32_t ResolveDataVersionForContainer(
    ir::SourceFormat fmt,
    std::int32_t target_version) noexcept {
  if (fmt != ir::SourceFormat::kLitematica) return 0;
  switch (target_version) {
    case 5:
      return -1;  // v5 rejected
    case 6:
      return 3463;  // 1.20 anchor
    default:
      return 0;  // 7 or unspecified: round-trip semantics
  }
}

[[nodiscard]] constexpr std::int32_t ContainerForDataVersion(
    std::int32_t dv) noexcept {
  if (dv >= 3837) return 7;  // 1.20.5+: components era container
  if (dv >= 2860) return 6;  // 1.18+
  return 5;
}

}  // namespace fschema::remap

#endif  // FSCHEMA_REMAP_REMAP_H_
