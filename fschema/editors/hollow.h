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

#ifndef FSCHEMA_EDITORS_HOLLOW_H_
#define FSCHEMA_EDITORS_HOLLOW_H_

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>

#include <cstdint>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/port.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/view.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"

namespace fschema::editors {

/// Removes all internal blocks, leaving only a shell of thickness 1.
///
/// This is achieved by selecting all blocks that are NOT exposed to
/// air AND NOT air themselves, then setting them to air.
///
/// Uses a pre-calculated `exposed[]` mask to avoid repeated 6-neighbor
/// checks and cascading in-place mutations during the scan.
template <filters::Filter F>
[[nodiscard]] ParseResult<void> Hollow(const filters::BasicView<F>& v,
                                       memory::Arena& arena) {
  ir::Region& r = v.region();
  auto mat_res = ir::EnsureMaterialized(r, arena);
  if (!mat_res) return std::unexpected(mat_res.error());

  const auto& bounds = r.bounds;
  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const std::uint64_t vol = static_cast<std::uint64_t>(sx) * sy * sz;
  if (vol == 0) return {};

  memory::UnInitBuffer<std::uint8_t> exposed(static_cast<std::size_t>(vol));
  std::uint8_t* FSCHEMA_RESTRICT exposed_ptr = exposed.data();

  const std::uint64_t y_stride = static_cast<std::uint64_t>(sx) * sz;
  const std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();
  const std::size_t pal_size = r.palette.size();

  // Build a lookup table for air checks.
  std::vector<std::uint8_t> is_air_lut(pal_size + 1, 0);
  for (std::size_t i = 0; i < pal_size; ++i) {
    if (base::IsAirVariant(r.palette[i].name)) {
      is_air_lut[i] = 1;
    }
  }
  const std::uint8_t* FSCHEMA_RESTRICT is_air = is_air_lut.data();

  constexpr std::uint64_t kParallelThreshold = 1 << 18;  // 256K blocks

  // Compute the exposed mask in parallel.
  auto compute_y = [&](std::int32_t y) {
    const std::uint64_t y_base = static_cast<std::uint64_t>(y) * y_stride;
    for (std::int32_t z = 0; z < sz; ++z) {
      const std::uint64_t z_base = y_base + static_cast<std::uint64_t>(z) * sx;
      for (std::int32_t x = 0; x < sx; ++x) {
        const std::uint64_t li = z_base + x;
        bool is_exp = false;

        // Check 6 neighbors with short-circuit evaluation.
        if (x == 0 || is_air[data[li - 1]])
          is_exp = true;
        else if (x == sx - 1 || is_air[data[li + 1]])
          is_exp = true;
        else if (z == 0 || is_air[data[li - sx]])
          is_exp = true;
        else if (z == sz - 1 || is_air[data[li + sx]])
          is_exp = true;
        else if (y == 0 || is_air[data[li - y_stride]])
          is_exp = true;
        else if (y == sy - 1 || is_air[data[li + y_stride]])
          is_exp = true;

        exposed_ptr[li] = is_exp ? 1 : 0;
      }
    }
  };

  if (vol < kParallelThreshold) {
    for (std::int32_t y = 0; y < sy; ++y) {
      compute_y(y);
    }
  } else {
    tbb::parallel_for(
        tbb::blocked_range<std::int32_t>(0, sy),
        [&](const tbb::blocked_range<std::int32_t>& range) {
          for (std::int32_t y = range.begin(); y < range.end(); ++y) {
            compute_y(y);
          }
        },
        tbb::static_partitioner{});
  }

  const std::uint16_t air_idx = ResolveAir(r);
  std::uint16_t* FSCHEMA_RESTRICT data_mut = r.block_indices.data();

  // Apply deletions in parallel.
  auto apply_y = [&](std::int32_t y) {
    const std::uint64_t y_base = static_cast<std::uint64_t>(y) * y_stride;
    for (std::int32_t z = 0; z < sz; ++z) {
      const std::uint64_t z_base = y_base + static_cast<std::uint64_t>(z) * sx;
      for (std::int32_t x = 0; x < sx; ++x) {
        const std::uint64_t li = z_base + x;
        const std::uint16_t pal = data_mut[li];
        if (pal < pal_size && !is_air[pal] && !exposed_ptr[li]) {
          data_mut[li] = air_idx;
        }
      }
    }
  };

  if (vol < kParallelThreshold) {
    for (std::int32_t y = 0; y < sy; ++y) {
      apply_y(y);
    }
  } else {
    tbb::parallel_for(
        tbb::blocked_range<std::int32_t>(0, sy),
        [&](const tbb::blocked_range<std::int32_t>& range) {
          for (std::int32_t y = range.begin(); y < range.end(); ++y) {
            apply_y(y);
          }
        },
        tbb::static_partitioner{});
  }

  MarkEdited(r);
  return {};
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_HOLLOW_H_