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

#include "fschema/ir/types.h"

namespace fschema::filters::internal {

[[nodiscard]] inline bool IsAirAt(const ir::Region& r,
                                  std::int32_t x,
                                  std::int32_t y,
                                  std::int32_t z) noexcept {
  const auto& b = r.bounds;
  if (x < 0 || x >= b.size[0] || y < 0 || y >= b.size[1] || z < 0 ||
      z >= b.size[2]) {
    return true;  // Out of bounds = air
  }
  const std::uint64_t idx =
      static_cast<std::uint64_t>(y) *
          (static_cast<std::uint64_t>(b.size[0]) * b.size[2]) +
      static_cast<std::uint64_t>(z) * b.size[0] + x;
  const std::uint16_t pal = r.block_indices[idx];
  return r.IsAir(pal);
}

[[nodiscard]] inline bool IsExposedAt(const ir::Region& r,
                                      std::int32_t x,
                                      std::int32_t y,
                                      std::int32_t z) noexcept {
  const auto& b = r.bounds;
  const std::int32_t sx = b.size[0];
  const std::int32_t sy = b.size[1];
  const std::int32_t sz = b.size[2];
  const std::uint64_t y_stride = static_cast<std::uint64_t>(sx) * sz;

  const std::uint64_t idx = static_cast<std::uint64_t>(y) * y_stride +
                            static_cast<std::uint64_t>(z) * sx + x;
  const std::uint16_t* data = r.block_indices.data();

  // Short-circuit evaluation with direct pointer arithmetic (no LinearIndex
  // recompute)
  if (x == 0 || r.IsAir(data[idx - 1])) return true;
  if (x == sx - 1 || r.IsAir(data[idx + 1])) return true;
  if (z == 0 || r.IsAir(data[idx - sx])) return true;
  if (z == sz - 1 || r.IsAir(data[idx + sx])) return true;
  if (y == 0 || r.IsAir(data[idx - y_stride])) return true;
  if (y == sy - 1 || r.IsAir(data[idx + y_stride])) return true;

  return false;
}

[[nodiscard]] inline bool IsFloatingAt(const ir::Region& r,
                                       std::int32_t x,
                                       std::int32_t y,
                                       std::int32_t z) noexcept {
  const auto& b = r.bounds;
  const std::int32_t sx = b.size[0];
  const std::int32_t sy = b.size[1];
  const std::int32_t sz = b.size[2];
  const std::uint64_t y_stride = static_cast<std::uint64_t>(sx) * sz;

  const std::uint64_t idx = static_cast<std::uint64_t>(y) * y_stride +
                            static_cast<std::uint64_t>(z) * sx + x;
  const std::uint16_t* data = r.block_indices.data();

  if (x == 0 || !r.IsAir(data[idx - 1])) return false;
  if (x == sx - 1 || !r.IsAir(data[idx + 1])) return false;
  if (z == 0 || !r.IsAir(data[idx - sx])) return false;
  if (z == sz - 1 || !r.IsAir(data[idx + sx])) return false;
  if (y == 0 || !r.IsAir(data[idx - y_stride])) return false;
  if (y == sy - 1 || !r.IsAir(data[idx + y_stride])) return false;

  return true;
}

[[nodiscard]] inline bool IsAirLikeAt(const ir::Region& r,
                                      std::int32_t x,
                                      std::int32_t y,
                                      std::int32_t z) noexcept {
  const auto& b = r.bounds;
  if (x < 0 || x >= b.size[0] || y < 0 || y >= b.size[1] || z < 0 ||
      z >= b.size[2]) {
    return true;
  }
  const std::uint64_t idx =
      static_cast<std::uint64_t>(y) *
          (static_cast<std::uint64_t>(b.size[0]) * b.size[2]) +
      static_cast<std::uint64_t>(z) * b.size[0] + x;
  const std::uint16_t pal = r.block_indices[idx];
  if (pal >= r.palette.size()) return true;
  if (r.IsAir(pal)) return true;
  const auto& name = r.palette[pal].name;
  return name == "minecraft:water" || name == "minecraft:flowing_water" ||
         name == "minecraft:lava" || name == "minecraft:flowing_lava";
}

[[nodiscard]] inline bool IsSupportCapableAt(const ir::Region& r,
                                             std::int32_t x,
                                             std::int32_t y,
                                             std::int32_t z) noexcept {
  return !IsAirLikeAt(r, x, y, z);
}

}  // namespace fschema::filters::internal

#endif  // FSCHEMA_FILTERS_INTERNAL_NEIGHBORS_H_