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

#include "fschema/geometry/internal/cull.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "fschema/ir/types.h"
#include "hwy/highway.h"

namespace fschema::geometry::internal {

void BlockGroupTable::BuildFrom(const ir::Region& region,
                                std::span<const std::uint8_t> palette_groups) {
  // EnsureAirLut() must have run single-threaded; this catches a stale LUT.
  assert(region.is_air_lut.size() == region.palette.size());

  bytes.resize(region.block_indices.size());
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    const auto pal = region.block_indices[i];
    if (pal < region.is_air_lut.size() && region.is_air_lut[pal] != 0) {
      bytes[i] = 0;  // air
      continue;
    }
    // Out-of-range palette entries fall back to opaque, mirroring the
    // defensive read in the air LUT above.
    bytes[i] = (pal < palette_groups.size()) ? palette_groups[pal] : 1;
  }
}

namespace {

namespace hn = hwy::HWY_NAMESPACE;

// +1 for +X, -1 for -X.
template <int kDirection>
void ComputeXAxisNeighborGroupBatchImpl(const BlockGroupTable& table,
                                        std::size_t base,
                                        std::size_t count,
                                        std::uint32_t row_width,
                                        std::uint8_t* out_groups) {
  const std::ptrdiff_t neighbor_offset = kDirection;
  const std::uint8_t* base_ptr = table.bytes.data();

  const hn::ScalableTag<std::uint8_t> d;
  const std::size_t N = hn::Lanes(d);

  // Is the neighbor of block `block_idx` in the same X row?
  //   +X: (block_idx % row_width) < row_width - 1
  //   -X: (block_idx % row_width) > 0
  auto SameRow = [&](std::size_t block_idx) -> bool {
    if (row_width == 0) return false;
    const std::size_t col = block_idx % row_width;
    if constexpr (kDirection > 0) {
      return col < row_width - 1;
    } else {
      return col > 0;
    }
  };

  std::size_t i = 0;
  for (; i + N <= count; i += N) {
    const std::ptrdiff_t neighbor_idx =
        static_cast<std::ptrdiff_t>(base + i) + neighbor_offset;
    bool in_bounds;
    if constexpr (kDirection > 0) {
      in_bounds = neighbor_idx + static_cast<std::ptrdiff_t>(N) <=
                  static_cast<std::ptrdiff_t>(table.bytes.size());
    } else {
      in_bounds = neighbor_idx >= 0;
    }
    if (!in_bounds) {
      // Scalar fallback for this batch.
      for (std::size_t j = 0; j < N && i + j < count; ++j) {
        const std::size_t block_idx = base + i + j;
        const std::ptrdiff_t nb_idx =
            static_cast<std::ptrdiff_t>(block_idx) + neighbor_offset;
        if (!SameRow(block_idx) || nb_idx < 0 ||
            nb_idx >= static_cast<std::ptrdiff_t>(table.bytes.size())) {
          out_groups[i + j] = 0;
        } else {
          out_groups[i + j] = table.bytes[nb_idx];
        }
      }
      continue;
    }

    const auto v = hn::LoadU(d, base_ptr + neighbor_idx);
    hn::Store(v, d, out_groups + i);

    // Zero lanes whose neighbor crossed a row boundary; only the row-end
    // (+X) or row-start (-X) element can be affected.
    for (std::size_t j = 0; j < N; ++j) {
      const std::size_t block_idx = base + i + j;
      if (!SameRow(block_idx)) {
        out_groups[i + j] = 0;
      }
    }
  }
  // Scalar tail for the last partial batch.
  for (; i < count; ++i) {
    const std::size_t block_idx = base + i;
    const std::ptrdiff_t nb_idx =
        static_cast<std::ptrdiff_t>(block_idx) + neighbor_offset;
    if (!SameRow(block_idx) || nb_idx < 0 ||
        nb_idx >= static_cast<std::ptrdiff_t>(table.bytes.size())) {
      out_groups[i] = 0;
    } else {
      out_groups[i] = table.bytes[nb_idx];
    }
  }
}

}  // namespace

void ComputePosXNeighborGroupBatch(const BlockGroupTable& table,
                                   std::size_t base,
                                   std::size_t count,
                                   std::uint32_t row_width,
                                   std::uint8_t* out_groups) {
  ComputeXAxisNeighborGroupBatchImpl<+1>(table, base, count, row_width,
                                         out_groups);
}

void ComputeNegXNeighborGroupBatch(const BlockGroupTable& table,
                                   std::size_t base,
                                   std::size_t count,
                                   std::uint32_t row_width,
                                   std::uint8_t* out_groups) {
  ComputeXAxisNeighborGroupBatchImpl<-1>(table, base, count, row_width,
                                         out_groups);
}

}  // namespace fschema::geometry::internal
