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

#ifndef FSCHEMA_FILTERS_NEIGHBOR_FILTER_H_
#define FSCHEMA_FILTERS_NEIGHBOR_FILTER_H_

#include <cstdint>
#include <utility>

#include "fschema/filters/concepts.h"
#include "fschema/filters/pos.h"
#include "fschema/ir/types.h"

namespace fschema::filters {

/// Selects blocks that have at least one adjacent block
/// (out of the 6 face neighbors) matching the provided filter `F`.
///
/// Contract:
/// 1. The region must be materialized.
/// 2. Blocks outside the region bounds are ignored (not passed to `F`).
template <Filter F>
class NeighborFilter {
 public:
  NeighborFilter(const ir::Region& r, F f) noexcept
      : region_(&r), f_(std::move(f)) {}

  [[nodiscard]] bool operator()(LocalPos p,
                                std::uint16_t /*idx=*/) const noexcept {
    const auto& b = region_->bounds;
    const std::int32_t sx = b.size[0];
    const std::int32_t sy = b.size[1];
    const std::int32_t sz = b.size[2];

    // Precompute linear index strides (YZX layout)
    const std::uint64_t y_stride = static_cast<std::uint64_t>(sx) * sz;
    const std::uint64_t base_idx = static_cast<std::uint64_t>(p.y) * y_stride +
                                   static_cast<std::uint64_t>(p.z) * sx +
                                   static_cast<std::uint64_t>(p.x);

    const std::uint16_t* data = region_->block_indices.data();

    // Check 6 neighbors with short-circuit evaluation via early return.

    // X-1
    if (p.x > 0 && f_({p.x - 1, p.y, p.z}, data[base_idx - 1])) { return true; }
    // X+1
    if (p.x < sx - 1 && f_({p.x + 1, p.y, p.z}, data[base_idx + 1]))
      { return true; }

    // Z-1
    if (p.z > 0 && f_({p.x, p.y, p.z - 1}, data[base_idx - sx])) { return true; }
    // Z+1
    if (p.z < sz - 1 && f_({p.x, p.y, p.z + 1}, data[base_idx + sx]))
      { return true; }

    // Y-1
    if (p.y > 0 && f_({p.x, p.y - 1, p.z}, data[base_idx - y_stride]))
      { return true; }
    // Y+1
    if (p.y < sy - 1 && f_({p.x, p.y + 1, p.z}, data[base_idx + y_stride]))
      { return true; }

    return false;
  }

 private:
  const ir::Region* region_;
  F f_;
};

/// Factory function for type deduction
template <Filter F>
[[nodiscard]] NeighborFilter<F> MakeNeighborFilter(const ir::Region& r,
                                                   F f) noexcept {
  return NeighborFilter<F>(r, std::move(f));
}

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_NEIGHBOR_FILTER_H_