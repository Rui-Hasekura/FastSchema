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

#ifndef FSCHEMA_EDITORS_MOVE_H_
#define FSCHEMA_EDITORS_MOVE_H_

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <cstdint>

#include "fschema/base/error.h"
#include "fschema/base/port.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/view.h"

namespace fschema::editors {

/// Clears the source selection (sets to air) and writes the original blocks
/// to their new positions shifted by (dx, dy, dz).
///
/// Out-of-bounds destinations are discarded (the block vanishes),
/// but the source is still cleared for those blocks.
///
/// To support overlapping moves without extra buffer allocation,
/// we traverse the region in reverse order of the shift direction.
template <filters::Filter F>
[[nodiscard]] ParseResult<void> Move(filters::BasicView<F> v,
                                     std::int32_t dx,
                                     std::int32_t dy,
                                     std::int32_t dz) {
  ir::Region& r = v.region();
  const auto& bounds = r.bounds;

  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const std::uint64_t vol = static_cast<std::uint64_t>(sx) * sy * sz;
  if (vol == 0 || (dx == 0 && dy == 0 && dz == 0)) return {};

  const F& filter = v.filter();
  std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();
  const std::uint16_t air_idx = ResolveAir(r);

  const std::uint64_t y_stride = static_cast<std::uint64_t>(sx) * sz;

  auto in_bounds = [&](std::int32_t x, std::int32_t y, std::int32_t z) {
    return x >= 0 && x < sx && y >= 0 && y < sy && z >= 0 && z < sz;
  };

  int x_begin = (dx <= 0) ? 0 : sx - 1;
  int x_end = (dx <= 0) ? sx : -1;
  int x_step = (dx <= 0) ? 1 : -1;

  int y_begin = (dy <= 0) ? 0 : sy - 1;
  int y_end = (dy <= 0) ? sy : -1;
  int y_step = (dy <= 0) ? 1 : -1;

  int z_begin = (dz <= 0) ? 0 : sz - 1;
  int z_end = (dz <= 0) ? sz : -1;
  int z_step = (dz <= 0) ? 1 : -1;

  constexpr std::uint64_t kParallelThreshold = 1 << 18;  // 256K blocks

  // Core execution logic for a given slab.
  // X is the parallel axis if dx == 0.
  auto process_x_slab = [&](std::int32_t x) {
    for (std::int32_t y = y_begin; y != y_end; y += y_step) {
      const std::uint64_t y_base = static_cast<std::uint64_t>(y) * y_stride;
      for (std::int32_t z = z_begin; z != z_end; z += z_step) {
        const std::uint64_t z_base =
            y_base + static_cast<std::uint64_t>(z) * sx;
        const std::uint64_t li = z_base + x;
        const std::uint16_t pal = data[li];
        if (filter({x, y, z}, pal)) {
          data[li] = air_idx;
          if (in_bounds(x + dx, y + dy, z + dz)) {
            const std::uint64_t dst_li =
                static_cast<std::uint64_t>(y + dy) * y_stride +
                static_cast<std::uint64_t>(z + dz) * sx + (x + dx);
            data[dst_li] = pal;
          }
        }
      }
    }
  };

  // Y is the parallel axis if dy == 0.
  auto process_y_slab = [&](std::int32_t y) {
    const std::uint64_t y_base = static_cast<std::uint64_t>(y) * y_stride;
    for (std::int32_t z = z_begin; z != z_end; z += z_step) {
      const std::uint64_t z_base = y_base + static_cast<std::uint64_t>(z) * sx;
      for (std::int32_t x = x_begin; x != x_end; x += x_step) {
        const std::uint64_t li = z_base + x;
        const std::uint16_t pal = data[li];
        if (filter({x, y, z}, pal)) {
          data[li] = air_idx;
          if (in_bounds(x + dx, y + dy, z + dz)) {
            const std::uint64_t dst_li =
                static_cast<std::uint64_t>(y + dy) * y_stride +
                static_cast<std::uint64_t>(z + dz) * sx + (x + dx);
            data[dst_li] = pal;
          }
        }
      }
    }
  };

  // Z is the parallel axis if dz == 0.
  auto process_z_slab = [&](std::int32_t z) {
    for (std::int32_t y = y_begin; y != y_end; y += y_step) {
      const std::uint64_t y_base = static_cast<std::uint64_t>(y) * y_stride;
      const std::uint64_t z_base = y_base + static_cast<std::uint64_t>(z) * sx;
      for (std::int32_t x = x_begin; x != x_end; x += x_step) {
        const std::uint64_t li = z_base + x;
        const std::uint16_t pal = data[li];
        if (filter({x, y, z}, pal)) {
          data[li] = air_idx;
          if (in_bounds(x + dx, y + dy, z + dz)) {
            const std::uint64_t dst_li =
                static_cast<std::uint64_t>(y + dy) * y_stride +
                static_cast<std::uint64_t>(z + dz) * sx + (x + dx);
            data[dst_li] = pal;
          }
        }
      }
    }
  };

  if (vol < kParallelThreshold) {
    if (dy == 0) {
      for (std::int32_t y = 0; y < sy; ++y) process_y_slab(y);
    } else if (dx == 0) {
      for (std::int32_t x = 0; x < sx; ++x) process_x_slab(x);
    } else if (dz == 0) {
      for (std::int32_t z = 0; z < sz; ++z) process_z_slab(z);
    } else {
      // Fallback to serial Y if all axes are shifting.
      for (std::int32_t y = y_begin; y != y_end; y += y_step) process_y_slab(y);
    }
  } else {
    if (dy == 0) {
      tbb::parallel_for(
          tbb::blocked_range<std::int32_t>(0, sy),
          [&](const tbb::blocked_range<std::int32_t>& range) {
            for (std::int32_t y = range.begin(); y < range.end(); ++y)
              process_y_slab(y);
          },
          tbb::static_partitioner{});
    } else if (dx == 0) {
      tbb::parallel_for(
          tbb::blocked_range<std::int32_t>(0, sx),
          [&](const tbb::blocked_range<std::int32_t>& range) {
            for (std::int32_t x = range.begin(); x < range.end(); ++x)
              process_x_slab(x);
          },
          tbb::static_partitioner{});
    } else if (dz == 0) {
      tbb::parallel_for(
          tbb::blocked_range<std::int32_t>(0, sz),
          [&](const tbb::blocked_range<std::int32_t>& range) {
            for (std::int32_t z = range.begin(); z < range.end(); ++z)
              process_z_slab(z);
          },
          tbb::static_partitioner{});
    } else {
      // Fallback to serial Y if all axes are shifting (rare).
      for (std::int32_t y = y_begin; y != y_end; y += y_step) process_y_slab(y);
    }
  }

  MarkEdited(r);
  return {};
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_MOVE_H_