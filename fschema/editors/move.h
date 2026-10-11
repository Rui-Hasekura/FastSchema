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
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include <cstdint>
#include <vector>

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
/// To support overlapping moves, selected blocks are collected
/// before any writes.
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
  if (vol == 0) return {};

  // Four uint16_t fields, 8 bytes total.
  struct MoveEntry {
    std::uint16_t x;
    std::uint16_t y;
    std::uint16_t z;
    std::uint16_t pal;
  };

  const F& filter = v.filter();
  const std::uint16_t* data = r.block_indices.data();

  auto collect_range =
      [&](auto& target_vec, std::int32_t y_begin, std::int32_t y_end) {
        for (std::int32_t y = y_begin; y < y_end; ++y) {
          const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
          for (std::int32_t z = 0; z < sz; ++z) {
            const std::uint64_t z_base =
                y_base + static_cast<std::uint64_t>(z) * sx;
            for (std::int32_t x = 0; x < sx; ++x) {
              const std::uint64_t src_linear = z_base + x;
              const std::uint16_t pal = data[src_linear];
              if (filter({x, y, z}, pal)) {
                target_vec.push_back({static_cast<std::uint16_t>(x),
                                      static_cast<std::uint16_t>(y),
                                      static_cast<std::uint16_t>(z),
                                      pal});
              }
            }
          }
        }
      };

  const std::uint16_t air_idx = ResolveAir(r);
  std::uint16_t* FSCHEMA_RESTRICT data_mut = r.block_indices.data();

  constexpr std::uint64_t kParallelThreshold = 1 << 18;  // 256K blocks

  if (vol < kParallelThreshold) {
    std::vector<MoveEntry> entries;
    collect_range(entries, 0, sy);

    // Pass 1: Clear source cells.
    for (const auto& e : entries) {
      const std::uint64_t src_linear =
          static_cast<std::uint64_t>(e.y) * sx * sz +
          static_cast<std::uint64_t>(e.z) * sx + e.x;
      data_mut[src_linear] = air_idx;
    }

    // Pass 2: Write shifted copies to in-bounds destinations.
    for (const auto& e : entries) {
      const std::int32_t dst_x = e.x + dx;
      const std::int32_t dst_y = e.y + dy;
      const std::int32_t dst_z = e.z + dz;
      if (dst_x >= 0 && dst_x < sx && dst_y >= 0 && dst_y < sy && dst_z >= 0 &&
          dst_z < sz) {
        const std::uint64_t dst_linear =
            static_cast<std::uint64_t>(dst_y) * sx * sz +
            static_cast<std::uint64_t>(dst_z) * sx + dst_x;
        data_mut[dst_linear] = e.pal;
      }
    }
  } else {
    tbb::enumerable_thread_specific<std::vector<MoveEntry>> tls;

    // Pass 1: Collect matching cells into thread-local vectors.
    tbb::parallel_for(
        tbb::blocked_range<std::int32_t>(0, sy),
        [&](const tbb::blocked_range<std::int32_t>& range) {
          auto& local = tls.local();
          local.reserve(static_cast<std::size_t>(range.size()) * sx * sz / 4 +
                        16);
          collect_range(local, range.begin(), range.end());
        },
        tbb::static_partitioner{});

    // Pass 2: Clear source cells.
    // Keep this pass serial: it is memory-bound, and parallel iteration would
    // add TLS-vector traversal overhead without reducing the scattered writes.
    for (auto it = tls.begin(); it != tls.end(); ++it) {
      const auto& vec = *it;
      for (const auto& e : vec) {
        const std::uint64_t src_linear =
            static_cast<std::uint64_t>(e.y) * sx * sz +
            static_cast<std::uint64_t>(e.z) * sx + e.x;
        data_mut[src_linear] = air_idx;
      }
    }

    // Pass 3: Write shifted copies to in-bounds destinations.
    for (auto it = tls.begin(); it != tls.end(); ++it) {
      const auto& vec = *it;
      for (const auto& e : vec) {
        const std::int32_t dst_x = e.x + dx;
        const std::int32_t dst_y = e.y + dy;
        const std::int32_t dst_z = e.z + dz;
        if (dst_x >= 0 && dst_x < sx && dst_y >= 0 && dst_y < sy &&
            dst_z >= 0 && dst_z < sz) {
          const std::uint64_t dst_linear =
              static_cast<std::uint64_t>(dst_y) * sx * sz +
              static_cast<std::uint64_t>(dst_z) * sx + dst_x;
          data_mut[dst_linear] = e.pal;
        }
      }
    }
  }

  MarkEdited(r);
  return {};
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_MOVE_H_