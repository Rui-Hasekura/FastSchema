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

#ifndef FSCHEMA_FILTERS_SURFACE_FILTER_H_
#define FSCHEMA_FILTERS_SURFACE_FILTER_H_

#include <cstdint>
#include <vector>

#include "fschema/base/block_utils.h"
#include "fschema/filters/pos.h"
#include "fschema/ir/types.h"

namespace fschema::filters {

/// Selects blocks that are exposed to air on at least one of their 6 faces.
///
/// Stateful filter checking block exposure against a Region.
/// Treats out-of-bounds neighbors as air.
///
/// Contract:
/// 1. `r.is_materialized == true` must hold before construction.
/// 2. Air blocks are never selected (they are not "surface").
class SurfaceFilter {
 public:
  explicit SurfaceFilter(const ir::Region& r) noexcept : region_(&r) {
    // Precompute thread-safe local LUT to avoid data races in r.IsAir()
    is_air_lut_.assign(r.palette.size(), 0);
    for (std::size_t i = 0; i < r.palette.size(); ++i) {
      if (fschema::base::IsAirVariant(r.palette[i].name)) {
        is_air_lut_[i] = 1;
      }
    }
  }

  [[nodiscard]] bool operator()(LocalPos p, std::uint16_t idx) const noexcept {
    const auto& r = *region_;
    if (idx >= r.palette.size() || is_air_lut_[idx]) return false;

    const auto& b = r.bounds;
    const std::int32_t sx = b.size[0];
    const std::int32_t sy = b.size[1];
    const std::int32_t sz = b.size[2];
    const std::uint64_t y_stride = static_cast<std::uint64_t>(sx) * sz;

    const std::uint64_t li = static_cast<std::uint64_t>(p.y) * y_stride +
                             static_cast<std::uint64_t>(p.z) * sx + p.x;
    const std::uint16_t* data = r.block_indices.data();
    const std::uint8_t* is_air = is_air_lut_.data();

    // Inline 6-neighbor check with pointer arithmetic
    if (p.x == 0 || is_air[data[li - 1]]) return true;
    if (p.x == sx - 1 || is_air[data[li + 1]]) return true;
    if (p.z == 0 || is_air[data[li - sx]]) return true;
    if (p.z == sz - 1 || is_air[data[li + sx]]) return true;
    if (p.y == 0 || is_air[data[li - y_stride]]) return true;
    if (p.y == sy - 1 || is_air[data[li + y_stride]]) return true;

    return false;
  }

 private:
  const ir::Region* region_;
  std::vector<std::uint8_t> is_air_lut_;
};

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_SURFACE_FILTER_H_