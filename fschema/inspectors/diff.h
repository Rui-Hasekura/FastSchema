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

#ifndef FSCHEMA_INSPECTORS_DIFF_H_
#define FSCHEMA_INSPECTORS_DIFF_H_

#include <tbb/blocked_range.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include "hwy/highway.h"

#if HWY_TARGET == HWY_AVX2
#  include <immintrin.h>
#endif

#include "absl/container/flat_hash_map.h"
#include "fschema/base/error.h"
#include "fschema/base/hash.h"
#include "fschema/filters/pos.h"
#include "fschema/ir/types.h"

namespace fschema::inspectors {

struct DiffEntry {
  filters::LocalPos pos;
  std::string_view from_block;
  std::string_view to_block;
};

/// Placeholder for a palette entry in r2 with no match in r1.
constexpr std::uint16_t kUnmappedPalette = 0xFFFF;

namespace {

struct BlockStateKey {
  std::string_view name;
  std::span<const std::byte> props;
};

struct BlockStateKeyHash {
  std::size_t operator()(const BlockStateKey& k) const noexcept {
    std::uint64_t h1 = base::Hash64WithSeed(k.name.data(), k.name.size());
    std::uint64_t h2 = base::Hash64WithSeed(k.props.data(), k.props.size(), h1);
    return static_cast<std::size_t>(h2);
  }
};

struct BlockStateKeyEq {
  bool operator()(const BlockStateKey& a,
                  const BlockStateKey& b) const noexcept {
    if (a.name != b.name) return false;
    if (a.props.size() != b.props.size()) return false;
    return std::equal(a.props.begin(), a.props.end(), b.props.begin());
  }
};

}  // anonymous namespace

[[nodiscard]] inline std::vector<std::uint16_t> BuildBToAMap(
    const ir::Region& ra,
    const ir::Region& rb) {
  // Extra slot for indices clamped to map_size by the SIMD path.
  std::vector<std::uint16_t> b_to_a(rb.palette.size() + 1, kUnmappedPalette);

  absl::flat_hash_map<BlockStateKey,
                      std::uint16_t,
                      BlockStateKeyHash,
                      BlockStateKeyEq>
      map_a;
  map_a.reserve(ra.palette.size());
  for (std::size_t i = 0; i < ra.palette.size(); ++i) {
    map_a.insert({{ra.palette[i].name, ra.palette[i].raw_properties},
                  static_cast<std::uint16_t>(i)});
  }

  for (std::size_t j = 0; j < rb.palette.size(); ++j) {
    auto it = map_a.find({rb.palette[j].name, rb.palette[j].raw_properties});
    if (it != map_a.end()) {
      b_to_a[j] = it->second;
    }
  }

  return b_to_a;
}

/// Compares two regions and returns a list of differing blocks.
///
/// Both regions must have the exact same bounding box (origin and size).
/// Matching is by (name, raw_properties), not by raw palette index, so the
/// result is robust to palette reordering. A block in r2 whose palette entry
/// has no match in r1 (`kUnmappedPalette`) is always reported as a difference.
/// Property encodings (kNbt / kString) must match; cross-format comparisons
/// require normalizing properties first.
[[nodiscard]] inline ParseResult<std::vector<DiffEntry>> Diff(
    const ir::Region& r1,
    const ir::Region& r2) {
  for (int i = 0; i < 3; ++i) {
    if (r1.bounds.size[i] != r2.bounds.size[i] ||
        r1.bounds.origin[i] != r2.bounds.origin[i]) {
      return std::unexpected(ParseError{
          ParseError::Code::kInvalidTagId,
          "DiffInspector: Regions must have the same size and origin",
          0});
    }
  }

  const auto b_to_a = BuildBToAMap(r1, r2);

  tbb::concurrent_vector<DiffEntry> concurrent_result;

  const auto& bounds = r1.bounds;
  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const std::uint64_t vol = static_cast<std::uint64_t>(sx) *
                            static_cast<std::uint64_t>(sy) *
                            static_cast<std::uint64_t>(sz);

  const std::uint16_t* r1_data = r1.block_indices.data();
  const std::uint16_t* r2_data = r2.block_indices.data();
  const std::uint16_t* map_data = b_to_a.data();
  const std::size_t map_size = b_to_a.size();

  constexpr std::uint64_t kParallelThreshold = 1 << 18;  // 256K blocks

#if HWY_TARGET == HWY_AVX2
  const __m256i mask16 = _mm256_set1_epi32(0x0000FFFF);
  // uint16_t p2 is always <= 65535. Clamp is only needed if map_size < 65536.
  const bool needs_clamp = map_size < 65536;
  const __m256i v_max_pal =
      needs_clamp ? _mm256_set1_epi16(static_cast<short>(map_size))
                  : _mm256_setzero_si256();
#endif

  auto process_y = [&](std::int32_t y) {
    const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
    for (std::int32_t z = 0; z < sz; ++z) {
      const std::uint64_t z_base = y_base + static_cast<std::uint64_t>(z) * sx;
      std::int32_t x = 0;

#if HWY_TARGET == HWY_AVX2
      for (; x + 16 <= sx; x += 16) {
        const __m256i v_p1 = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(r1_data + z_base + x));
        __m256i v_p2 = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(r2_data + z_base + x));

        // Cap p2 at map_size; the extra slot holds kUnmappedPalette.
        if (needs_clamp) {
          v_p2 = _mm256_min_epu16(v_p2, v_max_pal);
        }

        const __m128i p1_lo = _mm256_castsi256_si128(v_p1);
        const __m128i p1_hi = _mm256_extracti128_si256(v_p1, 1);
        const __m128i p2_lo = _mm256_castsi256_si128(v_p2);
        const __m128i p2_hi = _mm256_extracti128_si256(v_p2, 1);

        const __m256i v_idx_lo = _mm256_cvtepu16_epi32(p2_lo);
        const __m256i v_idx_hi = _mm256_cvtepu16_epi32(p2_hi);

        // Map r2 palette indices through b_to_a.
        __m256i v_map_lo = _mm256_i32gather_epi32(
            reinterpret_cast<const int*>(map_data), v_idx_lo, 2);
        __m256i v_map_hi = _mm256_i32gather_epi32(
            reinterpret_cast<const int*>(map_data), v_idx_hi, 2);

        // Mask out high 16 bits since source is uint16_t.
        v_map_lo = _mm256_and_si256(v_map_lo, mask16);
        v_map_hi = _mm256_and_si256(v_map_hi, mask16);

        const __m256i v_p1_lo = _mm256_cvtepu16_epi32(p1_lo);
        const __m256i v_p1_hi = _mm256_cvtepu16_epi32(p1_hi);

        const __m256i v_cmp_lo = _mm256_cmpeq_epi32(v_p1_lo, v_map_lo);
        const __m256i v_cmp_hi = _mm256_cmpeq_epi32(v_p1_hi, v_map_hi);

        std::uint32_t mmask_lo =
            static_cast<std::uint32_t>(_mm256_movemask_epi8(v_cmp_lo));
        std::uint32_t mmask_hi =
            static_cast<std::uint32_t>(_mm256_movemask_epi8(v_cmp_hi));

        // If either half has mismatches, fall back to scalar extraction.
        if (mmask_lo != 0xFFFFFFFF || mmask_hi != 0xFFFFFFFF) {
          for (std::size_t i = 0; i < 16; ++i) {
            const std::uint16_t p1 = r1_data[z_base + x + i];
            const std::uint16_t p2 = r2_data[z_base + x + i];
            const std::uint16_t p2_mapped =
                (p2 < map_size) ? map_data[p2] : kUnmappedPalette;
            if (p1 != p2_mapped) {
              const std::string_view n1 =
                  (p1 < r1.palette.size()) ? r1.palette[p1].name : "unknown";
              const std::string_view n2 =
                  (p2 < r2.palette.size()) ? r2.palette[p2].name : "unknown";
              concurrent_result.push_back(
                  {{static_cast<std::int32_t>(x + i), y, z}, n1, n2});
            }
          }
        }
      }
#endif

      for (; x < sx; ++x) {
        const std::uint16_t p1 = r1_data[z_base + x];
        const std::uint16_t p2 = r2_data[z_base + x];
        const std::uint16_t p2_mapped =
            (p2 < map_size) ? map_data[p2] : kUnmappedPalette;
        if (p1 != p2_mapped) {
          const std::string_view n1 =
              (p1 < r1.palette.size()) ? r1.palette[p1].name : "unknown";
          const std::string_view n2 =
              (p2 < r2.palette.size()) ? r2.palette[p2].name : "unknown";
          concurrent_result.push_back({{x, y, z}, n1, n2});
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
  }

  std::vector<DiffEntry> result;
  result.reserve(concurrent_result.size());
  result.insert(
      result.end(), concurrent_result.begin(), concurrent_result.end());
  return result;
}

}  // namespace fschema::inspectors

#endif  // FSCHEMA_INSPECTORS_DIFF_H_