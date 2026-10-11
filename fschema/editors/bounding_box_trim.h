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

#ifndef FSCHEMA_EDITORS_BOUNDING_BOX_TRIM_H_
#define FSCHEMA_EDITORS_BOUNDING_BOX_TRIM_H_

#include <tbb/blocked_range.h>
#include <tbb/parallel_reduce.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>

#include "hwy/highway.h"

#if HWY_TARGET == HWY_AVX2
#  include <immintrin.h>
#endif

#include "fschema/base/block_utils.h"
#include "fschema/base/error.h"
#include "fschema/editors/extract.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"

namespace fschema::editors {

namespace internal {

struct LocalBounds {
  bool any_non_air = false;
  std::int32_t min_x = std::numeric_limits<std::int32_t>::max();
  std::int32_t min_y = std::numeric_limits<std::int32_t>::max();
  std::int32_t min_z = std::numeric_limits<std::int32_t>::max();
  std::int32_t max_x = std::numeric_limits<std::int32_t>::min();
  std::int32_t max_y = std::numeric_limits<std::int32_t>::min();
  std::int32_t max_z = std::numeric_limits<std::int32_t>::min();
};

}  // namespace internal

[[nodiscard]] inline ParseResult<ir::Region> BoundingBoxTrim(
    ir::Region& r,
    memory::Arena& arena) {
  auto mat_res = ir::EnsureMaterialized(r, arena);
  if (!mat_res) return std::unexpected(mat_res.error());

  const std::int32_t sx = r.bounds.size[0];
  const std::int32_t sy = r.bounds.size[1];
  const std::int32_t sz = r.bounds.size[2];
  const std::uint64_t vol = static_cast<std::uint64_t>(sx) * sy * sz;
  if (vol == 0) {
    ir::Region empty;
    empty.name = "trimmed_empty";
    empty.bounds = {{0, 0, 0}, {0, 0, 0}};
    empty.position = {0, 0, 0};
    empty.size = {0, 0, 0};
    empty.is_materialized = true;
    return empty;
  }

  // Extra slot so clamped indices map to a zero entry.
  const std::size_t pal_size = r.palette.size();
  std::vector<std::uint8_t> is_air(pal_size + 1, 0);
  for (std::size_t i = 0; i < pal_size; ++i) {
    if (base::IsAirVariant(r.palette[i].name)) {
      is_air[i] = 1;
    }
  }

  const std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();
  constexpr std::uint64_t kParallelThreshold = 1 << 18;  // 256K blocks

  auto process_y = [&](std::int32_t y, internal::LocalBounds& local) {
    const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
    for (std::int32_t z = 0; z < sz; ++z) {
      const std::uint64_t z_base = y_base + static_cast<std::uint64_t>(z) * sx;
      std::int32_t x = 0;

#if HWY_TARGET == HWY_AVX2
      const __m256i v_max_pal = _mm256_set1_epi16(static_cast<short>(pal_size));
      const __m256i mask16 = _mm256_set1_epi32(0x0000FFFF);
      const __m256i v_zero = _mm256_setzero_si256();
      const std::uint8_t* lut = is_air.data();

      for (; x + 16 <= sx; x += 16) {
        const __m256i v_pal_raw = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(data + z_base + x));

        // Clamp out-of-bounds indices to the padding slot.
        __m256i v_pal = v_pal_raw;
        if (pal_size < 65536) {
          v_pal = _mm256_min_epu16(v_pal_raw, v_max_pal);
        }

        const __m128i p_lo = _mm256_castsi256_si128(v_pal);
        const __m128i p_hi = _mm256_extracti128_si256(v_pal, 1);
        const __m256i v_idx_lo = _mm256_cvtepu16_epi32(p_lo);
        const __m256i v_idx_hi = _mm256_cvtepu16_epi32(p_hi);

        // Gather is_air flags for the 16 blocks.
        __m256i v_ia_lo = _mm256_i32gather_epi32(
            reinterpret_cast<const int*>(lut), v_idx_lo, 1);
        v_ia_lo = _mm256_and_si256(v_ia_lo, _mm256_set1_epi32(0xFF));
        __m256i v_ia_hi = _mm256_i32gather_epi32(
            reinterpret_cast<const int*>(lut), v_idx_hi, 1);
        v_ia_hi = _mm256_and_si256(v_ia_hi, _mm256_set1_epi32(0xFF));

        // 0 if air, -1 if non-air.
        __m256i v_non_air_lo = _mm256_cmpeq_epi32(v_ia_lo, v_zero);
        __m256i v_non_air_hi = _mm256_cmpeq_epi32(v_ia_hi, v_zero);

        std::uint32_t mmask_lo =
            static_cast<std::uint32_t>(_mm256_movemask_epi8(v_non_air_lo));
        std::uint32_t mmask_hi =
            static_cast<std::uint32_t>(_mm256_movemask_epi8(v_non_air_hi));

        if (mmask_lo != 0) {
          std::uint32_t first = std::countr_zero(mmask_lo) / 4;
          std::uint32_t last = (31 - std::countl_zero(mmask_lo)) / 4;
          local.min_x =
              std::min(local.min_x, x + static_cast<std::int32_t>(first));
          local.max_x =
              std::max(local.max_x, x + static_cast<std::int32_t>(last));
          local.min_z = std::min(local.min_z, z);
          local.max_z = z;
          local.any_non_air = true;
        }
        if (mmask_hi != 0) {
          std::uint32_t first = std::countr_zero(mmask_hi) / 4;
          std::uint32_t last = (31 - std::countl_zero(mmask_hi)) / 4;
          local.min_x =
              std::min(local.min_x, x + 8 + static_cast<std::int32_t>(first));
          local.max_x =
              std::max(local.max_x, x + 8 + static_cast<std::int32_t>(last));
          local.min_z = std::min(local.min_z, z);
          local.max_z = z;
          local.any_non_air = true;
        }
      }
#endif

      // Handle remaining elements.
      for (; x < sx; ++x) {
        std::uint16_t pal = data[z_base + x];
        if (pal < pal_size && !is_air[pal]) {
          local.min_x = std::min(local.min_x, x);
          local.max_x = std::max(local.max_x, x);
          local.min_z = std::min(local.min_z, z);
          local.max_z = z;
          local.any_non_air = true;
        }
      }
    }

    if (local.any_non_air) {
      local.min_y = std::min(local.min_y, y);
      local.max_y = std::max(local.max_y, y);
    }
  };

  internal::LocalBounds global_bounds;

  if (vol < kParallelThreshold) {
    for (std::int32_t y = 0; y < sy; ++y) {
      process_y(y, global_bounds);
    }
  } else {
    global_bounds = tbb::parallel_reduce(
        tbb::blocked_range<std::int32_t>(0, sy),
        internal::LocalBounds{},
        [&](const tbb::blocked_range<std::int32_t>& range,
            internal::LocalBounds local) {
          for (std::int32_t y = range.begin(); y < range.end(); ++y) {
            process_y(y, local);
          }
          return local;
        },
        [](internal::LocalBounds a, const internal::LocalBounds& b) {
          if (!b.any_non_air) return a;
          if (!a.any_non_air) return b;
          a.min_x = std::min(a.min_x, b.min_x);
          a.max_x = std::max(a.max_x, b.max_x);
          a.min_y = std::min(a.min_y, b.min_y);
          a.max_y = std::max(a.max_y, b.max_y);
          a.min_z = std::min(a.min_z, b.min_z);
          a.max_z = std::max(a.max_z, b.max_z);
          return a;
        });
  }

  if (!global_bounds.any_non_air) {
    ir::Region empty;
    empty.name = "trimmed_empty";
    empty.bounds = {{0, 0, 0}, {0, 0, 0}};
    empty.position = {0, 0, 0};
    empty.size = {0, 0, 0};
    empty.is_materialized = true;
    return empty;
  }

  filters::BoxFilter box{global_bounds.min_x,
                         global_bounds.min_y,
                         global_bounds.min_z,
                         global_bounds.max_x,
                         global_bounds.max_y,
                         global_bounds.max_z};
  auto view_res = filters::MakeView(r, box, arena);
  if (!view_res) return std::unexpected(view_res.error());

  return Extract(*view_res, arena);
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_BOUNDING_BOX_TRIM_H_