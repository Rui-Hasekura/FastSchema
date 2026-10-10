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

#ifndef FSCHEMA_EDITORS_FILL_H_
#define FSCHEMA_EDITORS_FILL_H_

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "hwy/highway.h"

#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
#  include <immintrin.h>
#endif

#include "fschema/base/error.h"
#include "fschema/base/port.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/view.h"

namespace fschema::editors {

namespace internal {

namespace hn = hwy::HWY_NAMESPACE;

template <filters::Filter F>
[[nodiscard]] ParseResult<void> FillInternal(filters::BasicView<F> v,
                                             std::uint16_t target_idx) {
  ir::Region& r = v.region();
  std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();
  const auto& bounds = r.bounds;
  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const F& filter = v.filter();

  const std::uint64_t vol = static_cast<std::uint64_t>(sx) * sy * sz;
  constexpr std::uint64_t kParallelThreshold = 1 << 18;  // 256K blocks

  const hn::CappedTag<std::uint16_t, 16> d16;
  const std::size_t N = hn::Lanes(d16);
  const auto target_vec = hn::Set(d16, target_idx);
  const std::uint64_t all_mask = (N == 64) ? ~0ULL : ((1ULL << N) - 1);

  auto process_y = [&](std::int32_t y) {
    const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
    for (std::int32_t z = 0; z < sz; ++z) {
      const std::uint64_t z_base = y_base + static_cast<std::uint64_t>(z) * sx;
      std::int32_t x = 0;

      // Skip the row prefix to align streaming stores.
      const std::size_t align_bytes = N * sizeof(std::uint16_t);
      std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(data + z_base);
      std::size_t unalign = addr % align_bytes;
      std::size_t skip =
          unalign == 0 ? 0 : (align_bytes - unalign) / sizeof(std::uint16_t);

      while (x < sx && static_cast<std::size_t>(x) < skip) {
        if (filter({x, y, z}, data[z_base + x])) {
          data[z_base + x] = target_idx;
        }
        ++x;
      }

      for (; x + N <= sx; x += N) {
        std::uint64_t mask = 0;
        for (std::size_t i = 0; i < N; ++i) {
          if (filter({x + static_cast<std::int32_t>(i), y, z},
                     data[z_base + x + i])) {
            mask |= (1ULL << i);
          }
        }

        if (mask == all_mask) {
#if HWY_TARGET == HWY_AVX2
          _mm256_stream_si256(
              reinterpret_cast<__m256i*>(data + z_base + x),
              _mm256_set1_epi16(static_cast<short>(target_idx)));
#else
          // All lanes match; bypass cache with a non-temporal store.
          hn::Stream(target_vec, d16, data + z_base + x);
#endif
        } else if (mask != 0) {
#if HWY_TARGET == HWY_AVX2
          alignas(32) std::uint8_t blend_mask[32];
          for (std::size_t i = 0; i < 16; ++i) {
            std::uint8_t m = (mask & (1ULL << i)) ? 0xFF : 0x00;
            blend_mask[i * 2] = m;
            blend_mask[i * 2 + 1] = m;
          }
          __m256i v_mask =
              _mm256_load_si256(reinterpret_cast<const __m256i*>(blend_mask));
          __m256i v_data = _mm256_load_si256(
              reinterpret_cast<const __m256i*>(data + z_base + x));
          __m256i v_target = _mm256_set1_epi16(static_cast<short>(target_idx));
          __m256i v_res = _mm256_blendv_epi8(v_data, v_target, v_mask);
          _mm256_store_si256(reinterpret_cast<__m256i*>(data + z_base + x),
                             v_res);
#else
          // Mixed lanes; update only the matching elements.
          for (std::size_t i = 0; i < N; ++i) {
            if (mask & (1ULL << i)) {
              data[z_base + x + i] = target_idx;
            }
          }
#endif
        }
      }

      for (; x < sx; ++x) {
        if (filter({x, y, z}, data[z_base + x])) {
          data[z_base + x] = target_idx;
        }
      }
    }
  };

  if (vol < kParallelThreshold) {
    for (std::int32_t y = 0; y < sy; ++y) {
      process_y(y);
    }
  } else {
    tbb::parallel_for(
        tbb::blocked_range<std::int32_t>(0, sy),
        [&](const tbb::blocked_range<std::int32_t>& range) {
          for (std::int32_t y = range.begin(); y < range.end(); ++y) {
            process_y(y);
          }
        },
        tbb::static_partitioner{});

    // Publish non-temporal stores before later reads.
    std::atomic_thread_fence(std::memory_order_release);
  }

  MarkEdited(r);
  return {};
}

}  // namespace internal

template <filters::Filter F>
[[nodiscard]] ParseResult<void> Fill(filters::BasicView<F> v,
                                     std::string_view block_name) {
  ir::Region& r = v.region();
  const std::uint16_t target_idx = ResolveOrAppend(r, block_name);
  return internal::FillInternal(v, target_idx);
}

template <filters::Filter F>
[[nodiscard]] ParseResult<void> Delete(filters::BasicView<F> v) {
  ir::Region& r = v.region();
  const std::uint16_t air_idx = ResolveAir(r);
  return internal::FillInternal(v, air_idx);
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_FILL_H_