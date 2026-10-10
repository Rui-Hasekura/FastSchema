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

#ifndef FSCHEMA_INSPECTORS_COUNT_H_
#define FSCHEMA_INSPECTORS_COUNT_H_

#include <tbb/blocked_range.h>
#include <tbb/parallel_reduce.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "hwy/highway.h"

#if HWY_TARGET == HWY_AVX2
#  include <immintrin.h>
#endif

#include "fschema/filters/view.h"

namespace fschema::inspectors {

template <filters::Filter F>
[[nodiscard]] std::size_t Count(const filters::BasicView<F>& v,
                                std::string_view name) {
  const ir::Region& r = v.region();
  if (r.palette.empty()) return 0;

  // Extra slot so clamped indices map to a zero entry.
  std::vector<std::uint8_t> is_target(r.palette.size() + 1, 0);
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (r.palette[i].name == name) is_target[i] = 1;
  }

  const auto& bounds = r.bounds;
  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const std::uint16_t* data = r.block_indices.data();
  const F& filter = v.filter();

  const std::uint64_t vol = static_cast<std::uint64_t>(sx) * sy * sz;
  constexpr std::uint64_t kParallelThreshold = 1 << 18;  // 256K blocks
  constexpr std::size_t kBlockSize = 16;

  auto process_y = [&](std::int32_t y) -> std::size_t {
    std::size_t local_count = 0;
    const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
    for (std::int32_t z = 0; z < sz; ++z) {
      const std::uint64_t z_base = y_base + static_cast<std::uint64_t>(z) * sx;
      std::int32_t x = 0;

      for (; x + kBlockSize <= sx; x += kBlockSize) {
        std::uint32_t filter_mask = 0;
        for (std::size_t i = 0; i < kBlockSize; ++i) {
          if (filter({x + static_cast<std::int32_t>(i), y, z},
                     data[z_base + x + i])) {
            filter_mask |= (1u << i);
          }
        }
        if (filter_mask == 0) continue;

#if HWY_TARGET == HWY_AVX2
        const __m256i v_pal_raw = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(data + z_base + x));

        // Clamp out-of-bounds indices to the padding slot.
        __m256i v_pal = v_pal_raw;
        if (r.palette.size() < 65536) {
          const __m256i v_max =
              _mm256_set1_epi16(static_cast<short>(r.palette.size()));
          v_pal = _mm256_min_epu16(v_pal_raw, v_max);
        }

        const __m128i v_pal_lo = _mm256_castsi256_si128(v_pal);
        const __m128i v_pal_hi = _mm256_extracti128_si256(v_pal, 1);
        const __m256i v_idx0 = _mm256_cvtepu16_epi32(v_pal_lo);
        const __m256i v_idx1 = _mm256_cvtepu16_epi32(v_pal_hi);

        const std::uint8_t* lut = is_target.data();
        const __m256i v_mask_byte = _mm256_set1_epi32(0xFF);

        // Gather LUT values for 16 blocks. AND with 0xFF isolates the
        // target byte; shift left by 7 sets the MSB.
        __m256i v_t0 = _mm256_i32gather_epi32(
            reinterpret_cast<const int*>(lut), v_idx0, 1);
        v_t0 = _mm256_and_si256(v_t0, v_mask_byte);
        v_t0 = _mm256_slli_epi32(v_t0, 7);

        __m256i v_t1 = _mm256_i32gather_epi32(
            reinterpret_cast<const int*>(lut), v_idx1, 1);
        v_t1 = _mm256_and_si256(v_t1, v_mask_byte);
        v_t1 = _mm256_slli_epi32(v_t1, 7);

        // Extract the MSB of each byte into a 32-bit mask.
        std::uint32_t target_mask0 = _mm256_movemask_epi8(v_t0);
        std::uint32_t target_mask1 = _mm256_movemask_epi8(v_t1);

        // Expand the 8-bit mask by multiplying by 0x11111111 to match
        // the 4-bit stride of the VPMOVMSKB output.
        std::uint32_t fm_low = filter_mask & 0xFF;
        std::uint32_t fm_high = (filter_mask >> 8) & 0xFF;
        std::uint32_t fm_expanded0 = fm_low * 0x11111111u;
        std::uint32_t fm_expanded1 = fm_high * 0x11111111u;

        std::uint32_t final_mask0 = target_mask0 & fm_expanded0;
        std::uint32_t final_mask1 = target_mask1 & fm_expanded1;

        local_count += std::popcount(final_mask0) + std::popcount(final_mask1);
#else
        for (std::size_t i = 0; i < kBlockSize; ++i) {
          if (filter_mask & (1u << i)) {
            std::uint16_t pal = data[z_base + x + i];
            if (pal < is_target.size() && is_target[pal]) local_count++;
          }
        }
#endif
      }

      // Handle remaining elements.
      for (; x < sx; ++x) {
        std::uint16_t pal = data[z_base + x];
        if (filter({x, y, z}, pal) && pal < is_target.size() &&
            is_target[pal]) {
          local_count++;
        }
      }
    }
    return local_count;
  };

  if (vol < kParallelThreshold) {
    std::size_t total_count = 0;
    for (std::int32_t y = 0; y < sy; ++y) {
      total_count += process_y(y);
    }
    return total_count;
  } else {
    return tbb::parallel_reduce(
        tbb::blocked_range<std::int32_t>(0, sy),
        std::size_t{0},
        [&](const tbb::blocked_range<std::int32_t>& range, std::size_t init) {
          for (std::int32_t y = range.begin(); y < range.end(); ++y) {
            init += process_y(y);
          }
          return init;
        },
        [](std::size_t a, std::size_t b) { return a + b; },
        tbb::static_partitioner{});
  }
}

}  // namespace fschema::inspectors

#endif  // FSCHEMA_INSPECTORS_COUNT_H_