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

#ifndef FSCHEMA_INSPECTORS_SUPPORT_CHECK_H_
#define FSCHEMA_INSPECTORS_SUPPORT_CHECK_H_

#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "fschema/base/block_utils.h"
#include "fschema/base/nbt/property.h"
#include "fschema/filters/pos.h"
#include "fschema/filters/view.h"
#include "fschema/ir/types.h"

namespace fschema::inspectors {

struct UnsupportedBlock {
  filters::LocalPos pos;
  std::string_view block_name;
  std::string_view issue;
};

/// Direction relative to the block where structural support is required.
enum class SupportDir : std::uint8_t {
  kBelow,      // -Y: support required below (floor-mounted, e.g., ground torch)
  kAbove,      // +Y: support required above (hanging, e.g., lantern)
  kNorth,      // -Z
  kSouth,      // +Z
  kEast,       // +X
  kWest,       // -X
  kFacingInv,  // Support required opposite to `facing`
               // (e.g., ladder, wall torch)
  kAnyHorizontalOrAbove,  // Any of the 4 horizontal neighbors or above
  kFaceDependent,  // Depends on attached face (e.g., floor, ceiling, or wall)
  kHangingDependent,  // Depends on hanging state (e.g., lantern)
};

/// Defines a support rule: "block `block_name` requires a support-capable
/// block at the position indicated by `dir`."
struct SupportRule {
  std::string_view block_name;
  SupportDir dir;
  std::string_view issue;  // Human-readable description of the failure
};

inline constexpr SupportRule kSupportRules[] = {
    // clang-format off
    {"minecraft:torch", SupportDir::kBelow, "torch: no floor"},
    {"minecraft:wall_torch", SupportDir::kFacingInv, "wall_torch: no wall"},
    {"minecraft:soul_torch", SupportDir::kBelow, "soul_torch: no floor"},
    {"minecraft:soul_wall_torch", SupportDir::kFacingInv, "soul_wall_torch: no wall"},

    // Ladders attach to the block behind them,
    // opposite to their facing direction.
    {"minecraft:ladder", SupportDir::kFacingInv, "ladder: no wall"},

    // Wall signs
    {"minecraft:acacia_wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},
    {"minecraft:bamboo_wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},
    {"minecraft:birch_wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},
    {"minecraft:cherry_wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},
    {"minecraft:crimson_wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},
    {"minecraft:dark_oak_wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},
    {"minecraft:jungle_wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},
    {"minecraft:mangrove_wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},
    {"minecraft:oak_wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},
    {"minecraft:pale_oak_wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},
    {"minecraft:poplar_wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},
    {"minecraft:spruce_wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},
    {"minecraft:wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},
    {"minecraft:warped_wall_sign", SupportDir::kFacingInv, "wall_sign: no wall"},

    // Signs on ground
    {"minecraft:acacia_sign", SupportDir::kBelow, "sign: no floor"},
    {"minecraft:bamboo_sign", SupportDir::kBelow, "sign: no floor"},
    {"minecraft:birch_sign", SupportDir::kBelow, "sign: no floor"},
    {"minecraft:cherry_sign", SupportDir::kBelow, "sign: no floor"},
    {"minecraft:crimson_sign", SupportDir::kBelow, "sign: no floor"},
    {"minecraft:dark_oak_sign", SupportDir::kBelow, "sign: no floor"},
    {"minecraft:jungle_sign", SupportDir::kBelow, "sign: no floor"},
    {"minecraft:mangrove_sign", SupportDir::kBelow, "sign: no floor"},
    {"minecraft:oak_sign", SupportDir::kBelow, "sign: no floor"},
    {"minecraft:pale_oak_sign", SupportDir::kBelow, "sign: no floor"},
    {"minecraft:poplar_sign", SupportDir::kBelow, "sign: no floor"},
    {"minecraft:sign", SupportDir::kBelow, "sign: no floor"},
    {"minecraft:spruce_sign", SupportDir::kBelow, "sign: no floor"},
    {"minecraft:warped_sign", SupportDir::kBelow, "sign: no floor"},

    // Buttons
    {"minecraft:acacia_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:bamboo_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:birch_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:cherry_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:crimson_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:dark_oak_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:jungle_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:mangrove_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:oak_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:pale_oak_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:polished_blackstone_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:poplar_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:spruce_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:stone_button", SupportDir::kFaceDependent, "button: no support"},
    {"minecraft:warped_button", SupportDir::kFaceDependent, "button: no support"},

    // Levers
    {"minecraft:lever", SupportDir::kFaceDependent, "lever: no support"},

    // Tripwire hooks
    {"minecraft:tripwire_hook", SupportDir::kFacingInv, "tripwire_hook: no wall"},

    // Hanging blocks
    {"minecraft:lantern", SupportDir::kHangingDependent, "lantern: no support"},
    {"minecraft:soul_lantern", SupportDir::kHangingDependent, "lantern: no support"},

    // Vines need support from above or any horizontal neighbor.
    {"minecraft:vine", SupportDir::kAnyHorizontalOrAbove, "vine: no support"},
    // clang-format on
};

/// Static HashMap for O(1) rule lookups by block name.
[[nodiscard]] inline const absl::flat_hash_map<std::string_view, std::int32_t>&
GetSupportRuleLookup() noexcept {
  static const absl::flat_hash_map<std::string_view, std::int32_t> lookup = [] {
    absl::flat_hash_map<std::string_view, std::int32_t> m;
    m.reserve(std::size(kSupportRules));
    for (std::size_t i = 0; i < std::size(kSupportRules); ++i) {
      m.emplace(kSupportRules[i].block_name, static_cast<std::int32_t>(i));
    }
    return m;
  }();
  return lookup;
}

/// Extracts a string property value by key from a BlockState
/// (supports Sponge k=v and Litematica NBT).
/// @return The property value, or empty string_view if not found.
[[nodiscard]] inline std::string_view ExtractStringProp(
    const ir::BlockState& bs,
    std::string_view key) noexcept {
  if (bs.raw_properties.empty()) return {};

  if (bs.prop_encoding == ir::PropertyEncoding::kString) {
    std::string_view props(
        reinterpret_cast<const char*>(bs.raw_properties.data()),
        bs.raw_properties.size());
    std::string needle;
    needle.reserve(key.size() + 1);
    needle.append(key);
    needle.push_back('=');
    auto pos = props.find(needle);
    if (pos == std::string_view::npos) return {};
    auto start = pos + needle.size();
    auto end = props.find(',', start);
    if (end == std::string_view::npos) end = props.size();
    return props.substr(start, end - start);
  }

  if (bs.prop_encoding == ir::PropertyEncoding::kNbt) {
    auto result = base::FindNbtStringProp(bs.raw_properties, key);
    return result ? *result : std::string_view{};
  }
  return {};
}

[[nodiscard]] inline std::string_view ExtractFacing(
    const ir::BlockState& bs) noexcept {
  return ExtractStringProp(bs, "facing");
}

[[nodiscard]] inline std::string_view ExtractFace(
    const ir::BlockState& bs) noexcept {
  return ExtractStringProp(bs, "face");
}

/// Computes the (dx, dy, dz) offset to the support position
/// based on `dir` and the block's `facing` property.
[[nodiscard]] inline std::array<std::int32_t, 3> SupportOffset(
    SupportDir dir,
    std::string_view facing) noexcept {
  switch (dir) {
    case SupportDir::kBelow:
      return {0, -1, 0};
    case SupportDir::kAbove:
      return {0, 1, 0};
    case SupportDir::kNorth:
      return {0, 0, -1};
    case SupportDir::kSouth:
      return {0, 0, 1};
    case SupportDir::kEast:
      return {1, 0, 0};
    case SupportDir::kWest:
      return {-1, 0, 0};
    case SupportDir::kFacingInv:
      // Support is on the opposite side of `facing`.
      if (facing == "north") return {0, 0, 1};
      if (facing == "south") return {0, 0, -1};
      if (facing == "east") return {-1, 0, 0};
      if (facing == "west") return {1, 0, 0};
      break;
    case SupportDir::kAnyHorizontalOrAbove:
    case SupportDir::kFaceDependent:
    case SupportDir::kHangingDependent:
      break;
  }
  return {0, 0, 0};  // fallback: no offset
}

/// Pre-extracted properties for a single palette entry.
struct PalettePropsCache {
  std::string_view facing;
  std::string_view face;
  std::string_view hanging;
};

/// Scans a region for blocks that require support but lack it.
///
/// Uses `kSupportRules` to determine which blocks need support
/// and in which direction.
///
/// @pre `r.is_materialized == true`.
[[nodiscard]] inline std::vector<UnsupportedBlock> SupportCheck(
    const ir::Region& r) {
  std::vector<UnsupportedBlock> result;

  const auto& rule_lookup = GetSupportRuleLookup();

  const std::size_t pal_size = r.palette.size();
  if (pal_size == 0) return result;

  // 1. Build a lookup index: palette_idx -> rule index (or -1).
  std::vector<std::int32_t> rule_index(pal_size, -1);
  for (std::size_t i = 0; i < pal_size; ++i) {
    auto it = rule_lookup.find(r.palette[i].name);
    if (it != rule_lookup.end()) {
      rule_index[i] = it->second;
    }
  }

  // 2. Pre-extract required properties for matching palette entries.
  // This avoids repeated string/NBT parsing in the inner loop.
  std::vector<PalettePropsCache> props_cache(pal_size);
  for (std::size_t i = 0; i < pal_size; ++i) {
    if (rule_index[i] < 0) continue;  // Only extract for blocks with rules
    const auto& bs = r.palette[i];
    props_cache[i].facing = ExtractFacing(bs);
    props_cache[i].face = ExtractFace(bs);
    props_cache[i].hanging = ExtractStringProp(bs, "hanging");
  }

  // 3. Precompute a support-capability LUT for every palette entry so that
  // neighbor lookups never branch on block names in the inner loop.
  std::vector<std::uint8_t> is_support_lut(pal_size, 1);
  for (std::size_t i = 0; i < pal_size; ++i) {
    const auto& name = r.palette[i].name;
    if (base::IsAirVariant(name) || name == "minecraft:water" ||
        name == "minecraft:flowing_water" || name == "minecraft:lava" ||
        name == "minecraft:flowing_lava") {
      is_support_lut[i] = 0;
    }
  }
  const std::uint8_t* is_support = is_support_lut.data();
  const std::uint16_t* data = r.block_indices.data();
  const std::int32_t* rule_idx_data = rule_index.data();

  const auto& bounds = r.bounds;
  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const std::uint64_t vol = static_cast<std::uint64_t>(sx) * sy * sz;
  if (vol == 0) return result;

  const std::uint64_t y_stride = static_cast<std::uint64_t>(sx) * sz;

  auto is_support_at =
      [&](std::int32_t x, std::int32_t y, std::int32_t z) -> bool {
    if (x < 0 || x >= sx || y < 0 || y >= sy || z < 0 || z >= sz) return false;
    const std::uint64_t idx = static_cast<std::uint64_t>(y) * y_stride +
                              static_cast<std::uint64_t>(z) * sx + x;
    const std::uint16_t p = data[idx];
    return p < pal_size ? is_support[p] : false;
  };

  // Check a single block; extracted so the serial and parallel paths share
  // the same logic and the parallel path can drive it from a flat index.
  auto check_block = [&](std::uint64_t li,
                         std::int32_t x,
                         std::int32_t y,
                         std::int32_t z,
                         std::vector<UnsupportedBlock>& local_result) {
    const std::uint16_t pal = data[li];
    if (pal >= pal_size) return;

    const std::int32_t r_idx = rule_idx_data[pal];
    if (r_idx < 0) return;  // No support rule for this block

    const auto& rule = kSupportRules[r_idx];
    const auto& pc = props_cache[pal];
    bool unsupported = false;
    std::string_view issue_str = rule.issue;

    if (rule.dir == SupportDir::kFacingInv) {
      if (pc.facing.empty()) {
        // Block has a facing-dependent rule but no facing property.
        // Report it as a data issue.
        unsupported = true;
        issue_str = "missing facing property";
      } else {
        const auto [dx, dy, dz] = SupportOffset(rule.dir, pc.facing);
        if (!is_support_at(x + dx, y + dy, z + dz)) unsupported = true;
      }
    } else if (rule.dir == SupportDir::kAnyHorizontalOrAbove) {
      const bool supported = is_support_at(x, y + 1, z) ||  // Above
                             is_support_at(x - 1, y, z) ||  // West
                             is_support_at(x + 1, y, z) ||  // East
                             is_support_at(x, y, z - 1) ||  // North
                             is_support_at(x, y, z + 1);    // South
      if (!supported) unsupported = true;
    } else if (rule.dir == SupportDir::kFaceDependent) {
      if (pc.face.empty()) {
        unsupported = true;
        issue_str = "missing face property";
      } else if (pc.face == "floor") {
        if (!is_support_at(x, y - 1, z)) unsupported = true;
      } else if (pc.face == "ceiling") {
        if (!is_support_at(x, y + 1, z)) unsupported = true;
      } else {  // face == "wall"
        if (pc.facing.empty()) {
          unsupported = true;
          issue_str = "missing facing property";
        } else {
          const auto [dx, dy, dz] =
              SupportOffset(SupportDir::kFacingInv, pc.facing);
          if (!is_support_at(x + dx, y + dy, z + dz)) unsupported = true;
        }
      }
    } else if (rule.dir == SupportDir::kHangingDependent) {
      if (pc.hanging.empty()) {
        unsupported = true;
        issue_str = "missing hanging property";
      } else {
        const bool ok = (pc.hanging == "true") ? is_support_at(x, y + 1, z)
                                               : is_support_at(x, y - 1, z);
        if (!ok) unsupported = true;
      }
    } else {
      const auto [dx, dy, dz] = SupportOffset(rule.dir, pc.facing);
      if (!is_support_at(x + dx, y + dy, z + dz)) unsupported = true;
    }

    if (unsupported) {
      local_result.push_back(UnsupportedBlock{
          filters::LocalPos{x, y, z}, rule.block_name, issue_str});
    }
  };

  constexpr std::uint64_t kParallelThreshold = 1 << 18;
  // 16K blocks per chunk: small enough for even load balancing across
  // threads, large enough to amortize TBB scheduling overhead.
  constexpr std::uint64_t kChunkSize = 1 << 14;

  if (vol < kParallelThreshold) {
    for (std::uint64_t li = 0; li < vol; ++li) {
      const std::uint32_t y = static_cast<std::uint32_t>(li / y_stride);
      const std::uint64_t rem = li % y_stride;
      const std::uint32_t z = static_cast<std::uint32_t>(rem / sx);
      const std::uint32_t x = static_cast<std::uint32_t>(rem % sx);
      check_block(li, x, y, z, result);
    }
  } else {
    // Thread-local result buffers avoid atomic/lock overhead on the hot
    // push_back path; they are merged after the parallel region.
    tbb::enumerable_thread_specific<std::vector<UnsupportedBlock>> tls;

    tbb::parallel_for(
        tbb::blocked_range<std::uint64_t>(0, vol, kChunkSize),
        [&](const tbb::blocked_range<std::uint64_t>& range) {
          auto& local_result = tls.local();

          // Seed the (li, x, y, z) cursor at the chunk start, then walk
          // linearly so the inner loop has no per-iteration division.
          std::uint64_t li = range.begin();
          std::uint32_t y = static_cast<std::uint32_t>(li / y_stride);
          std::uint64_t rem = li % y_stride;
          std::uint32_t z = static_cast<std::uint32_t>(rem / sx);
          std::uint32_t x = static_cast<std::uint32_t>(rem % sx);

          for (std::uint64_t i = range.begin(); i < range.end(); ++i) {
            check_block(li, x, y, z, local_result);

            ++li;
            ++x;
            if (x == sx) {
              x = 0;
              ++z;
              if (z == sz) {
                z = 0;
                ++y;
              }
            }
          }
        },
        tbb::static_partitioner{});

    // Merge thread-local buffers into the final result.
    std::size_t total_size = 0;
    for (const auto& vec : tls) {
      total_size += vec.size();
    }
    result.reserve(total_size);
    for (const auto& vec : tls) {
      result.insert(result.end(), vec.begin(), vec.end());
    }
  }

  return result;
}

}  // namespace fschema::inspectors

#endif  // FSCHEMA_INSPECTORS_SUPPORT_CHECK_H_