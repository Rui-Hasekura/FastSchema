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

#ifndef FSCHEMA_EDITORS_REPLACE_H_
#define FSCHEMA_EDITORS_REPLACE_H_

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <cstdint>
#include <string_view>
#include <vector>

#include "hwy/highway.h"

#if HWY_TARGET == HWY_AVX2
#  include <immintrin.h>
#endif

#include "fschema/base/error.h"
#include "fschema/base/port.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/view.h"

namespace fschema::editors {

/// Replaces all blocks whose palette name matches `from` with `to`.
///
/// Builds a boolean LUT of matching source palette indices during setup to
/// avoid string comparisons entirely.
template <filters::Filter F>
[[nodiscard]] ParseResult<void> ReplaceWith(filters::BasicView<F> v,
                                            std::string_view from,
                                            std::string_view to) {
  ir::Region& r = v.region();
  if (r.palette.empty()) return {};

  // Extra slot so clamped indices map to a zero entry.
  std::vector<std::uint8_t> is_from(r.palette.size() + 1, 0);
  bool any_from = false;
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (r.palette[i].name == from) {
      is_from[i] = 1;
      any_from = true;
    }
  }
  if (!any_from) return {};

  const std::uint16_t to_idx = ResolveOrAppend(r, to);
  if (is_from.size() <= to_idx) {
    is_from.resize(r.palette.size() + 1, 0);
  }

  std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();
  const auto& bounds = r.bounds;
  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const F& filter = v.filter();

  const std::uint64_t vol = static_cast<std::uint64_t>(sx) * sy * sz;
  constexpr std::uint64_t kParallelThreshold = 1 << 18;  // 256K blocks
  constexpr std::size_t kBlockSize = 16;

  auto process_y = [&](std::int32_t y) {
    const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
    for (std::int32_t z = 0; z < sz; ++z) {
      const std::uint64_t z_base = y_base + static_cast<std::uint64_t>(z) * sx;
      std::int32_t x = 0;

      // Skip the row prefix to align streaming stores.
      const std::size_t align_bytes = kBlockSize * sizeof(std::uint16_t);
      std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(data + z_base);
      std::size_t unalign = addr % align_bytes;
      std::size_t skip =
          unalign == 0 ? 0 : (align_bytes - unalign) / sizeof(std::uint16_t);

      while (x < sx && static_cast<std::size_t>(x) < skip) {
        std::uint16_t pal = data[z_base + x];
        if (filter({x, y, z}, pal) && pal < is_from.size() && is_from[pal]) {
          data[z_base + x] = to_idx;
        }
        ++x;
      }

      for (; x + kBlockSize <= sx; x += kBlockSize) {
        std::uint32_t filter_mask = 0;
        for (std::size_t i = 0; i < kBlockSize; ++i) {
          if (filter({x + static_cast<std::int32_t>(i), y, z},
                     data[z_base + x + i])) {
            filter_mask |= (1u << i);
          }
        }
        if (filter_mask == 0) continue;

        std::uint16_t final_mask = 0;
        for (std::size_t i = 0; i < kBlockSize; ++i) {
          if (filter_mask & (1u << i)) {
            std::uint16_t pal = data[z_base + x + i];
            if (pal < is_from.size() && is_from[pal]) {
              final_mask |= (1u << i);
            }
          }
        }

        if (final_mask == 0xFFFF) {
#if HWY_TARGET == HWY_AVX2
          _mm256_stream_si256(reinterpret_cast<__m256i*>(data + z_base + x),
                              _mm256_set1_epi16(static_cast<short>(to_idx)));
#else
          // All lanes match; overwrite the whole block.
          for (std::size_t i = 0; i < kBlockSize; ++i) {
            data[z_base + x + i] = to_idx;
          }
#endif
        } else if (final_mask != 0) {
          // Mixed lanes; update only the matching elements.
          for (std::size_t i = 0; i < kBlockSize; ++i) {
            if (final_mask & (1u << i)) {
              data[z_base + x + i] = to_idx;
            }
          }
        }
      }

      // Handle remaining elements.
      for (; x < sx; ++x) {
        std::uint16_t pal = data[z_base + x];
        if (filter({x, y, z}, pal) && pal < is_from.size() && is_from[pal]) {
          data[z_base + x] = to_idx;
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

#if HWY_TARGET == HWY_AVX2
    // Publish non-temporal stores before later reads.
    std::atomic_thread_fence(std::memory_order_release);
#endif
  }

  MarkEdited(r);
  return {};
}

/// Generic replacement via a mapper callable.
///
/// @param mapper A callable taking `std::uint16_t` (old palette idx) and
///               returning `std::uint16_t` (new palette idx). Return the same
///               value to skip.
///
/// Use this when matching is more complex than name equality, e.g. matching
/// by properties, by a regex on names, or by palette index ranges.
template <filters::Filter F, class Mapper>
  requires std::regular_invocable<const Mapper&, std::uint16_t> &&
           std::convertible_to<
               std::invoke_result_t<const Mapper&, std::uint16_t>,
               std::uint16_t>
[[nodiscard]] ParseResult<void> Replace(filters::BasicView<F> v,
                                        Mapper mapper) {
  ir::Region& r = v.region();
  std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();
  v.for_each_linear([&](std::uint64_t linear_idx, std::uint16_t pal) {
    const std::uint16_t new_pal = mapper(pal);
    if (new_pal != pal) {
      data[linear_idx] = new_pal;
    }
  });
  MarkEdited(r);
  return {};
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_REPLACE_H_