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

#ifndef FSCHEMA_INSPECTORS_FLOATING_CHECK_H_
#define FSCHEMA_INSPECTORS_FLOATING_CHECK_H_

#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

#include "fschema/filters/internal/neighbors.h"
#include "fschema/filters/pos.h"
#include "fschema/filters/view.h"
#include "fschema/ir/types.h"

namespace fschema::inspectors {

/// Bitmask selecting which categories of floating blocks/entities to report.
/// Combinations are OR'd together (e.g., `kFloatingBlocks | kFloatingGravity`).
enum class FloatingCheckMode : std::uint8_t {
  kFloatingBlocks = 1 << 0,    // Non-air blocks with all 6 neighbors as air.
  kFloatingGravity = 1 << 1,   // Gravity-affected blocks that are floating.
  kFloatingEntities = 1 << 2,  // Entities whose block position is in air.
};

[[nodiscard]] constexpr FloatingCheckMode operator|(
    FloatingCheckMode a,
    FloatingCheckMode b) noexcept {
  return static_cast<FloatingCheckMode>(static_cast<std::uint8_t>(a) |
                                        static_cast<std::uint8_t>(b));
}

[[nodiscard]] constexpr bool operator&(FloatingCheckMode a,
                                       FloatingCheckMode b) noexcept {
  return (static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b)) != 0;
}

struct FloatingBlock {
  filters::LocalPos pos;
  std::string_view block_name;
  bool is_gravity_affected;
};

struct FloatingEntity {
  std::string_view id;
  std::array<double, 3> position;
};

struct FloatingCheckResult {
  std::vector<FloatingBlock> blocks;
  std::vector<FloatingEntity> entities;
};

/// Checks a static list of gravity-affected block names.
[[nodiscard]] inline bool IsGravityAffected(std::string_view name) noexcept {
  constexpr std::string_view kGravityBlocks[] = {
      "minecraft:sand",
      "minecraft:red_sand",
      "minecraft:gravel",
      "minecraft:anvil",
      "minecraft:chipped_anvil",
      "minecraft:damaged_anvil",
      "minecraft:white_concrete_powder",
      "minecraft:orange_concrete_powder",
      "minecraft:magenta_concrete_powder",
      "minecraft:light_blue_concrete_powder",
      "minecraft:yellow_concrete_powder",
      "minecraft:lime_concrete_powder",
      "minecraft:pink_concrete_powder",
      "minecraft:gray_concrete_powder",
      "minecraft:light_gray_concrete_powder",
      "minecraft:cyan_concrete_powder",
      "minecraft:purple_concrete_powder",
      "minecraft:blue_concrete_powder",
      "minecraft:brown_concrete_powder",
      "minecraft:green_concrete_powder",
      "minecraft:red_concrete_powder",
      "minecraft:black_concrete_powder",
      "minecraft:scaffolding",
      "minecraft:dragon_egg",
      "minecraft:pointed_dripstone",
      "minecraft:suspicious_sand",
      "minecraft:suspicious_gravel",
  };
  for (auto g : kGravityBlocks) {
    if (name == g) return true;
  }
  return false;
}

/// Scans a region for floating blocks and/or floating entities.
///
/// @param mode Bitmask of `FloatingCheckMode` values. For example:
///             `kFloatingBlocks | kFloatingGravity` reports all floating
///             blocks, with gravity-affected ones flagged.
/// @pre `r.is_materialized == true`.
[[nodiscard]] inline FloatingCheckResult FloatingCheck(
    const ir::Region& r,
    FloatingCheckMode mode = FloatingCheckMode::kFloatingBlocks |
                             FloatingCheckMode::kFloatingGravity |
                             FloatingCheckMode::kFloatingEntities) {
  FloatingCheckResult result;

  const bool check_blocks = (mode & FloatingCheckMode::kFloatingBlocks) ||
                            (mode & FloatingCheckMode::kFloatingGravity);
  const bool check_entities = (mode & FloatingCheckMode::kFloatingEntities);

  if (check_blocks) {
    const auto& bounds = r.bounds;
    const std::int32_t sx = bounds.size[0];
    const std::int32_t sy = bounds.size[1];
    const std::int32_t sz = bounds.size[2];
    const std::uint16_t* data = r.block_indices.data();

    std::uint64_t li = 0;
    for (std::int32_t y = 0; y < sy; ++y) {
      for (std::int32_t z = 0; z < sz; ++z) {
        for (std::int32_t x = 0; x < sx; ++x, ++li) {
          const std::uint16_t pal = data[li];
          if (pal >= r.palette.size()) continue;

          const auto& name = r.palette[pal].name;
          if (r.IsAir(pal)) continue;  // skip air

          // A block is floating if all 6 neighbors are air (or out of bounds).
          const bool floating = filters::internal::IsFloatingAt(r, x, y, z);
          if (!floating) continue;

          const bool gravity = IsGravityAffected(name);

          // Report based on mode: kFloatingBlocks reports all,
          // kFloatingGravity reports ONLY gravity-affected ones.
          if ((mode & FloatingCheckMode::kFloatingBlocks) ||
              (gravity && (mode & FloatingCheckMode::kFloatingGravity))) {
            result.blocks.push_back(
                FloatingBlock{filters::LocalPos{x, y, z}, name, gravity});
          }
        }
      }
    }
  }

  if (check_entities) {
    for (const auto& ent : r.entities) {
      // An entity is "floating" if the block at its position is air.
      // Convert world-space double position to region-local integer block
      // coords.
      const std::int32_t ex =
          static_cast<std::int32_t>(std::floor(ent.position[0])) -
          r.bounds.origin[0];
      const std::int32_t ey =
          static_cast<std::int32_t>(std::floor(ent.position[1])) -
          r.bounds.origin[1];
      const std::int32_t ez =
          static_cast<std::int32_t>(std::floor(ent.position[2])) -
          r.bounds.origin[2];
      if (filters::internal::IsAirAt(r, ex, ey, ez)) {
        result.entities.push_back(FloatingEntity{ent.id, ent.position});
      }
    }
  }

  return result;
}

}  // namespace fschema::inspectors

#endif  // FSCHEMA_INSPECTORS_FLOATING_CHECK_H_