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

#ifndef FSCHEMA_FILTERS_INTERNAL_NEIGHBORS_H_
#define FSCHEMA_FILTERS_INTERNAL_NEIGHBORS_H_

#include <cstdint>
#include <string_view>

#include "fschema/base/block_utils.h"
#include "fschema/filters/pos.h"
#include "fschema/filters/view.h"
#include "fschema/ir/types.h"

namespace fschema::filters::internal {

/// Returns true if the block at (x, y, z) is an air variant.
///
/// OOB treated as air.
[[nodiscard]] inline bool IsAirAt(const ir::Region& r,
                                  std::int32_t x,
                                  std::int32_t y,
                                  std::int32_t z) noexcept {
  const auto& b = r.bounds;
  if (x < 0 || x >= b.size[0] || y < 0 || y >= b.size[1] || z < 0 ||
      z >= b.size[2]) {
    return true;  // Out of bounds = air
  }
  const std::uint64_t idx = LinearIndex(LocalPos{x, y, z}, b);
  const std::uint16_t pal = r.block_indices[idx];
  return pal < r.palette.size() && base::IsAirVariant(r.palette[pal].name);
}

/// Returns true if the block has at least one air neighbor among its 6 faces.
[[nodiscard]] inline bool IsExposedAt(const ir::Region& r,
                                      std::int32_t x,
                                      std::int32_t y,
                                      std::int32_t z) noexcept {
  return IsAirAt(r, x - 1, y, z) || IsAirAt(r, x + 1, y, z) ||
         IsAirAt(r, x, y - 1, z) || IsAirAt(r, x, y + 1, z) ||
         IsAirAt(r, x, y, z - 1) || IsAirAt(r, x, y, z + 1);
}

/// Returns true if the block is surrounded by air on all 6 faces.
[[nodiscard]] inline bool IsFloatingAt(const ir::Region& r,
                                       std::int32_t x,
                                       std::int32_t y,
                                       std::int32_t z) noexcept {
  return IsAirAt(r, x - 1, y, z) && IsAirAt(r, x + 1, y, z) &&
         IsAirAt(r, x, y - 1, z) && IsAirAt(r, x, y + 1, z) &&
         IsAirAt(r, x, y, z - 1) && IsAirAt(r, x, y, z + 1);
}

/// Returns true if the block is air, a fluid (water/lava), or out-of-bounds.
///
/// Fluids do not provide structural support.
[[nodiscard]] inline bool IsAirLikeAt(const ir::Region& r,
                                      std::int32_t x,
                                      std::int32_t y,
                                      std::int32_t z) noexcept {
  const auto& b = r.bounds;
  if (x < 0 || x >= b.size[0] || y < 0 || y >= b.size[1] || z < 0 ||
      z >= b.size[2]) {
    return true;
  }
  const std::uint64_t idx = LinearIndex(LocalPos{x, y, z}, b);
  const std::uint16_t pal = r.block_indices[idx];
  if (pal >= r.palette.size()) return true;
  const auto name = r.palette[pal].name;
  if (base::IsAirVariant(name)) return true;
  // Fluids: still water, flowing water, still lava, flowing lava.
  return name == "minecraft:water" || name == "minecraft:flowing_water" ||
         name == "minecraft:lava" || name == "minecraft:flowing_lava";
}

/// Returns true if the block can provide structural support
/// (solid, non-fluid, in-bounds). Convenience wrapper around `!IsAirLikeAt`.
[[nodiscard]] inline bool IsSupportCapableAt(const ir::Region& r,
                                             std::int32_t x,
                                             std::int32_t y,
                                             std::int32_t z) noexcept {
  return !IsAirLikeAt(r, x, y, z);
}

}  // namespace fschema::filters::internal

#endif  // FSCHEMA_FILTERS_INTERNAL_NEIGHBORS_H_