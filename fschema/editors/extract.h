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

#ifndef FSCHEMA_EDITORS_EXTRACT_H_
#define FSCHEMA_EDITORS_EXTRACT_H_

#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/port.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"

namespace fschema::editors {

/// Scans the view and computes the bounding box of the selected blocks.
template <filters::Filter F>
[[nodiscard]] std::optional<ir::BoundingBox> ComputeSelectionBounds(
    const filters::BasicView<F>& v) {
  const ir::Region& r = v.region();
  const auto& bounds = r.bounds;
  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const std::uint16_t* data = r.block_indices.data();
  const F& filter = v.filter();
  const std::uint64_t vol = static_cast<std::uint64_t>(sx) * sy * sz;
  constexpr std::uint64_t kParallelThreshold = 1 << 18;

  struct LocalBounds {
    bool any = false;
    std::int32_t min_x = std::numeric_limits<std::int32_t>::max();
    std::int32_t min_y = std::numeric_limits<std::int32_t>::max();
    std::int32_t min_z = std::numeric_limits<std::int32_t>::max();
    std::int32_t max_x = std::numeric_limits<std::int32_t>::min();
    std::int32_t max_y = std::numeric_limits<std::int32_t>::min();
    std::int32_t max_z = std::numeric_limits<std::int32_t>::min();
  };

  LocalBounds global_bounds;
  if (vol < kParallelThreshold) {
    for (std::int32_t y = 0; y < sy; ++y) {
      const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
      for (std::int32_t z = 0; z < sz; ++z) {
        const std::uint64_t z_base =
            y_base + static_cast<std::uint64_t>(z) * sx;
        for (std::int32_t x = 0; x < sx; ++x) {
          if (filter({x, y, z}, data[z_base + x])) {
            global_bounds.any = true;
            if (x < global_bounds.min_x) global_bounds.min_x = x;
            if (y < global_bounds.min_y) global_bounds.min_y = y;
            if (z < global_bounds.min_z) global_bounds.min_z = z;
            if (x > global_bounds.max_x) global_bounds.max_x = x;
            if (y > global_bounds.max_y) global_bounds.max_y = y;
            if (z > global_bounds.max_z) global_bounds.max_z = z;
          }
        }
      }
    }
  } else {
    global_bounds = tbb::parallel_reduce(
        tbb::blocked_range<std::int32_t>(0, sy),
        LocalBounds{},
        [&](const tbb::blocked_range<std::int32_t>& range, LocalBounds local) {
          for (std::int32_t y = range.begin(); y < range.end(); ++y) {
            const std::uint64_t y_base =
                static_cast<std::uint64_t>(y) * sx * sz;
            for (std::int32_t z = 0; z < sz; ++z) {
              const std::uint64_t z_base =
                  y_base + static_cast<std::uint64_t>(z) * sx;
              for (std::int32_t x = 0; x < sx; ++x) {
                if (filter({x, y, z}, data[z_base + x])) {
                  local.any = true;
                  if (x < local.min_x) local.min_x = x;
                  if (y < local.min_y) local.min_y = y;
                  if (z < local.min_z) local.min_z = z;
                  if (x > local.max_x) local.max_x = x;
                  if (y > local.max_y) local.max_y = y;
                  if (z > local.max_z) local.max_z = z;
                }
              }
            }
          }
          return local;
        },
        [](LocalBounds a, const LocalBounds& b) {
          if (!b.any) return a;
          if (!a.any) return b;
          a.any = true;
          a.min_x = std::min(a.min_x, b.min_x);
          a.min_y = std::min(a.min_y, b.min_y);
          a.min_z = std::min(a.min_z, b.min_z);
          a.max_x = std::max(a.max_x, b.max_x);
          a.max_y = std::max(a.max_y, b.max_y);
          a.max_z = std::max(a.max_z, b.max_z);
          return a;
        });
  }

  if (!global_bounds.any) return std::nullopt;

  ir::BoundingBox bb;
  bb.origin[0] = r.bounds.origin[0] + global_bounds.min_x;
  bb.origin[1] = r.bounds.origin[1] + global_bounds.min_y;
  bb.origin[2] = r.bounds.origin[2] + global_bounds.min_z;
  bb.size[0] = global_bounds.max_x - global_bounds.min_x + 1;
  bb.size[1] = global_bounds.max_y - global_bounds.min_y + 1;
  bb.size[2] = global_bounds.max_z - global_bounds.min_z + 1;
  return bb;
}

/// Deep-copies the selected blocks into a new, compact `Region`.
template <filters::Filter F>
[[nodiscard]] ParseResult<ir::Region> Extract(const filters::BasicView<F>& v,
                                              memory::Arena& arena) {
  const ir::Region& src = v.region();
  const auto& bounds = src.bounds;
  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const std::uint64_t vol = static_cast<std::uint64_t>(sx) * sy * sz;

  const std::uint16_t* data = src.block_indices.data();
  const F& filter = v.filter();
  const std::size_t pal_size = src.palette.size();

  ir::BoundingBox bb;
  bool empty = false;

  // BoxFilter already exposes its bounds, so the selection bounds can be
  // derived without scanning the region.
  if constexpr (std::is_same_v<F, filters::BoxFilter>) {
    if (filter.min_x > filter.max_x || filter.min_y > filter.max_y ||
        filter.min_z > filter.max_z) {
      empty = true;
    } else {
      bb.origin[0] = src.bounds.origin[0] + filter.min_x;
      bb.origin[1] = src.bounds.origin[1] + filter.min_y;
      bb.origin[2] = src.bounds.origin[2] + filter.min_z;
      bb.size[0] = filter.max_x - filter.min_x + 1;
      bb.size[1] = filter.max_y - filter.min_y + 1;
      bb.size[2] = filter.max_z - filter.min_z + 1;
    }
  } else {
    auto bounds_res = ComputeSelectionBounds(v);
    if (!bounds_res) {
      empty = true;
    } else {
      bb = *bounds_res;
    }
  }

  if (empty) {
    ir::Region out;
    out.name = "extract";
    out.bounds = {{0, 0, 0}, {0, 0, 0}};
    out.position = {0, 0, 0};
    out.size = {0, 0, 0};
    out.is_materialized = true;
    return out;
  }

  const std::int32_t off_x = bb.origin[0] - src.bounds.origin[0];
  const std::int32_t off_y = bb.origin[1] - src.bounds.origin[1];
  const std::int32_t off_z = bb.origin[2] - src.bounds.origin[2];

  ir::Region out;
  out.name = "extract";
  out.bounds = bb;
  out.position = {bb.origin[0], bb.origin[1], bb.origin[2]};
  out.size = {bb.size[0], bb.size[1], bb.size[2]};

  const std::uint64_t dst_vol = static_cast<std::uint64_t>(bb.size[0]) *
                                static_cast<std::uint64_t>(bb.size[1]) *
                                static_cast<std::uint64_t>(bb.size[2]);
  out.block_indices =
      memory::UnInitBuffer<std::uint16_t>(static_cast<std::size_t>(dst_vol));

  const std::uint64_t dst_sx = bb.size[0];
  const std::uint64_t dst_sz = bb.size[2];
  const std::uint64_t dst_y_stride = dst_sx * dst_sz;
  std::uint16_t* FSCHEMA_RESTRICT dst_data = out.block_indices.data();

  constexpr std::uint64_t kParallelThreshold = 1 << 18;

  if constexpr (std::is_same_v<F, filters::BoxFilter>) {
    // BoxFilter fast path: no per-block filter checks or palette compaction.
    // Rows inside the box are copied directly.
    out.palette = src.palette;
    if (dst_vol > 0) {
      std::fill_n(dst_data, dst_vol, static_cast<std::uint16_t>(0));
    }

    auto copy_y = [&](std::int32_t y) {
      const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
      const std::uint64_t dst_y_base =
          static_cast<std::uint64_t>(y - off_y) * dst_y_stride;
      for (std::int32_t z = filter.min_z; z <= filter.max_z; ++z) {
        const std::uint64_t z_base =
            y_base + static_cast<std::uint64_t>(z) * sx;
        const std::uint64_t dst_z_base =
            dst_y_base + static_cast<std::uint64_t>(z - off_z) * dst_sx;
        const std::size_t row_size = (filter.max_x - filter.min_x + 1);
        std::memcpy(dst_data + dst_z_base,
                    data + z_base + filter.min_x,
                    row_size * sizeof(std::uint16_t));
      }
    };

    if (vol < kParallelThreshold) {
      for (std::int32_t y = filter.min_y; y <= filter.max_y; ++y) copy_y(y);
    } else {
      tbb::parallel_for(
          tbb::blocked_range<std::int32_t>(filter.min_y, filter.max_y + 1),
          [&](const tbb::blocked_range<std::int32_t>& range) {
            for (std::int32_t y = range.begin(); y < range.end(); ++y)
              copy_y(y);
          },
          tbb::static_partitioner{});
    }
  } else {
    // Generic path: evaluate the filter while copying, then compact the
    // palette to only the entries that were actually written.
    if (dst_vol > 0 && !out.palette.empty()) {
      std::fill_n(dst_data, dst_vol, static_cast<std::uint16_t>(0));
    }

    tbb::enumerable_thread_specific<std::vector<std::uint8_t>> tls_used(
        [&]() { return std::vector<std::uint8_t>(pal_size, 0); });

    auto copy_y = [&](std::int32_t y) {
      auto& local_used = tls_used.local();
      const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
      const std::uint64_t dst_y_base =
          static_cast<std::uint64_t>(y - off_y) * dst_y_stride;
      for (std::int32_t z = 0; z < sz; ++z) {
        const std::uint64_t z_base =
            y_base + static_cast<std::uint64_t>(z) * sx;
        const std::uint64_t dst_z_base =
            dst_y_base + static_cast<std::uint64_t>(z - off_z) * dst_sx;
        for (std::int32_t x = 0; x < sx; ++x) {
          const std::uint64_t li = z_base + x;
          const std::uint16_t pal = data[li];
          if (filter({x, y, z}, pal)) {
            dst_data[dst_z_base + (x - off_x)] = pal;
            if (pal < pal_size) local_used[pal] = 1;
          }
        }
      }
    };

    if (vol < kParallelThreshold) {
      for (std::int32_t y = 0; y < sy; ++y) copy_y(y);
    } else {
      tbb::parallel_for(
          tbb::blocked_range<std::int32_t>(0, sy),
          [&](const tbb::blocked_range<std::int32_t>& range) {
            for (std::int32_t y = range.begin(); y < range.end(); ++y)
              copy_y(y);
          },
          tbb::static_partitioner{});
    }

    // Merge thread-local used-palette masks into a global remap.
    std::vector<std::uint8_t> global_used(pal_size, 0);
    for (auto it = tls_used.begin(); it != tls_used.end(); ++it) {
      for (std::size_t i = 0; i < pal_size; ++i) {
        global_used[i] |= (*it)[i];
      }
    }

    std::vector<std::uint16_t> remap(pal_size, 0);
    std::vector<ir::BlockState> new_palette;
    new_palette.reserve(pal_size);
    for (std::size_t i = 0; i < pal_size; ++i) {
      if (global_used[i]) {
        remap[i] = static_cast<std::uint16_t>(new_palette.size());
        new_palette.push_back(src.palette[i]);
      }
    }
    out.palette = std::move(new_palette);

    // Rewrite destination indices to the compacted palette.
    if (dst_vol >= kParallelThreshold) {
      tbb::parallel_for(
          tbb::blocked_range<std::size_t>(
              0, static_cast<std::size_t>(dst_vol), 1 << 16),
          [&](const tbb::blocked_range<std::size_t>& range) {
            for (std::size_t i = range.begin(); i < range.end(); ++i) {
              std::uint16_t p = dst_data[i];
              dst_data[i] = (p < pal_size) ? remap[p] : 0;
            }
          },
          tbb::static_partitioner{});
    } else {
      for (std::size_t i = 0; i < dst_vol; ++i) {
        std::uint16_t p = dst_data[i];
        dst_data[i] = (p < pal_size) ? remap[p] : 0;
      }
    }
  }

  out.is_materialized = true;
  return out;
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_EXTRACT_H_