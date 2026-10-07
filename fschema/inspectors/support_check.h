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

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "fschema/base/nbt/property.h"
#include "fschema/filters/internal/neighbors.h"
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

/// Extracts a string property value by key from a BlockState
/// (supports Schem k=v and Litematica NBT).
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

/// Scans a region for blocks that require support but lack it.
///
/// Uses `kSupportRules` to determine which blocks need support
/// and in which direction.
///
/// @pre `r.is_materialized == true`.
[[nodiscard]] inline std::vector<UnsupportedBlock> SupportCheck(
    const ir::Region& r) {
  std::vector<UnsupportedBlock> result;

  // Build a lookup index: palette_idx -> rule index (or -1).
  std::vector<std::int32_t> rule_index(r.palette.size(), -1);
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    for (std::size_t j = 0; j < std::size(kSupportRules); ++j) {
      if (r.palette[i].name == kSupportRules[j].block_name) {
        rule_index[i] = static_cast<std::int32_t>(j);
        break;
      }
    }
  }

  const auto& bounds = r.bounds;
  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const std::uint16_t* data = r.block_indices.data();

  for (std::int32_t y = 0; y < sy; ++y) {
    const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
    for (std::int32_t z = 0; z < sz; ++z) {
      const std::uint64_t z_base = y_base + static_cast<std::uint64_t>(z) * sx;
      for (std::int32_t x = 0; x < sx; ++x) {
        const std::uint16_t pal = data[z_base + x];
        if (pal >= rule_index.size()) continue;

        const std::int32_t r_idx = rule_index[pal];
        if (r_idx < 0) continue;  // No support rule for this block

        const auto& rule = kSupportRules[r_idx];

        // For facing-based directions, extract the facing property.
        std::string_view facing;
        if (rule.dir == SupportDir::kFacingInv) {
          facing = ExtractFacing(r.palette[pal]);
          if (facing.empty()) {
            // Block has a facing-dependent rule but no facing property.
            // Report it as a data issue.
            result.push_back(UnsupportedBlock{filters::LocalPos{x, y, z},
                                              rule.block_name,
                                              "missing facing property"});
            continue;
          }
        }

        if (rule.dir == SupportDir::kAnyHorizontalOrAbove) {
          bool supported =
              filters::internal::IsSupportCapableAt(r, x, y + 1, z) ||  // Above
              filters::internal::IsSupportCapableAt(r, x - 1, y, z) ||  // West
              filters::internal::IsSupportCapableAt(r, x + 1, y, z) ||  // East
              filters::internal::IsSupportCapableAt(r, x, y, z - 1) ||  // North
              filters::internal::IsSupportCapableAt(r, x, y, z + 1);    // South
          if (!supported) {
            result.push_back(UnsupportedBlock{
                filters::LocalPos{x, y, z}, rule.block_name, rule.issue});
          }
          continue;
        }

        if (rule.dir == SupportDir::kFaceDependent) {
          const std::string_view face = ExtractFace(r.palette[pal]);
          if (face.empty()) {
            result.push_back(UnsupportedBlock{filters::LocalPos{x, y, z},
                                              rule.block_name,
                                              "missing face property"});
            continue;
          }

          if (face == "floor") {
            if (!filters::internal::IsSupportCapableAt(r, x, y - 1, z)) {
              result.push_back(UnsupportedBlock{
                  filters::LocalPos{x, y, z}, rule.block_name, rule.issue});
            }
            continue;
          }

          if (face == "ceiling") {
            if (!filters::internal::IsSupportCapableAt(r, x, y + 1, z)) {
              result.push_back(UnsupportedBlock{
                  filters::LocalPos{x, y, z}, rule.block_name, rule.issue});
            }
            continue;
          }

          // face == "wall"
          const std::string_view facing = ExtractFacing(r.palette[pal]);
          if (facing.empty()) {
            result.push_back(UnsupportedBlock{filters::LocalPos{x, y, z},
                                              rule.block_name,
                                              "missing facing property"});
            continue;
          }
          const auto [dx, dy, dz] =
              SupportOffset(SupportDir::kFacingInv, facing);
          if (!filters::internal::IsSupportCapableAt(
                  r, x + dx, y + dy, z + dz)) {
            result.push_back(UnsupportedBlock{
                filters::LocalPos{x, y, z}, rule.block_name, rule.issue});
          }
          continue;
        }

        if (rule.dir == SupportDir::kHangingDependent) {
          const std::string_view hanging =
              ExtractStringProp(r.palette[pal], "hanging");
          if (hanging.empty()) {
            result.push_back(UnsupportedBlock{filters::LocalPos{x, y, z},
                                              rule.block_name,
                                              "missing hanging property"});
            continue;
          }
          const bool ok =
              (hanging == "true")
                  ? filters::internal::IsSupportCapableAt(r, x, y + 1, z)
                  : filters::internal::IsSupportCapableAt(r, x, y - 1, z);
          if (!ok) {
            result.push_back(UnsupportedBlock{
                filters::LocalPos{x, y, z}, rule.block_name, rule.issue});
          }
          continue;
        }

        const auto [dx, dy, dz] = SupportOffset(rule.dir, facing);
        const std::int32_t nx = x + dx;
        const std::int32_t ny = y + dy;
        const std::int32_t nz = z + dz;

        // Check if the neighbor at the offset position can provide support.
        if (!filters::internal::IsSupportCapableAt(r, nx, ny, nz)) {
          result.push_back(UnsupportedBlock{
              filters::LocalPos{x, y, z}, rule.block_name, rule.issue});
        }
      }
    }
  }

  return result;
}

}  // namespace fschema::inspectors

#endif  // FSCHEMA_INSPECTORS_SUPPORT_CHECK_H_