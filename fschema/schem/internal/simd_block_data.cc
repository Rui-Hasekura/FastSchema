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

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "fschema/schem/internal/simd_block_data.cc"

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <vector>

#include "hwy/foreach_target.h"
#include "hwy/highway.h"
#include "hwy/cache_control.h"

#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
#  include <immintrin.h>
#endif

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include "fschema/base/error.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/noinit_allocator.h"
#include "fschema/schem/internal/block_data.h"
#include "fschema/schem/types.h"

HWY_BEFORE_NAMESPACE();
namespace fschema::schem::internal {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

#if HWY_TARGET == HWY_AVX2

[[nodiscard]] ParseResult<NoInitVector<std::uint16_t>> DecodeSingleByteFastImpl(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena) {
  const auto n = static_cast<std::size_t>(volume);
  const auto* src = reinterpret_cast<const std::uint8_t*>(data.data());

  NoInitVector<std::uint16_t> out(n,
                                  memory::NoInitAllocator<std::uint16_t>{&arena});
  auto* dst = out.data();

  const std::size_t n32 = (n / 32) * 32;
  const std::size_t num_iters = n32 / 32;

  std::atomic<std::uint32_t> error_flag{0};

  auto process_range = [&](std::size_t range_begin, std::size_t range_end) {
    const __m256i v_msb_mask = _mm256_set1_epi8(static_cast<char>(0x80));
    __m256i v_msb_acc = _mm256_setzero_si256();
    __m256i v_max_acc = _mm256_setzero_si256();

    const auto* s = src + range_begin * 32;
    auto* d = dst + range_begin * 32;
    const auto* s_end = src + range_end * 32;

    for (; s < s_end; s += 32, d += 32) {
      __m256i v32 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));

      __m128i lo = _mm256_castsi256_si128(v32);
      __m128i hi = _mm256_extracti128_si256(v32, 1);
      __m256i wide0 = _mm256_cvtepu8_epi16(lo);
      __m256i wide1 = _mm256_cvtepu8_epi16(hi);

      _mm256_stream_si256(reinterpret_cast<__m256i*>(d), wide0);
      _mm256_stream_si256(reinterpret_cast<__m256i*>(d + 16), wide1);

      v_msb_acc = _mm256_or_si256(v_msb_acc, _mm256_and_si256(v32, v_msb_mask));
      v_max_acc = _mm256_max_epu8(v_max_acc, v32);
    }

    if (!_mm256_testz_si256(v_msb_acc, v_msb_acc)) {
      std::uint32_t expected = 0;
      error_flag.compare_exchange_strong(
          expected, 1, std::memory_order_relaxed);
    }

    if (palette_size <= 256) {
      __m128i max_lo = _mm256_castsi256_si128(v_max_acc);
      __m128i max_hi = _mm256_extracti128_si256(v_max_acc, 1);
      __m128i max128 = _mm_max_epu8(max_lo, max_hi);
      alignas(16) std::uint8_t max_buf[16];
      _mm_store_si128(reinterpret_cast<__m128i*>(max_buf), max128);
      std::uint8_t max_val = 0;
      for (int j = 0; j < 16; ++j) {
        if (max_buf[j] > max_val) max_val = max_buf[j];
      }
      if (max_val >= palette_size) {
        std::uint32_t expected = 0;
        error_flag.compare_exchange_strong(
            expected, 2, std::memory_order_relaxed);
      }
    }
  };

  if (num_iters < 512) {
    process_range(0, num_iters);
  } else {
    tbb::parallel_for(
        tbb::blocked_range<std::size_t>(0, num_iters),
        [&](const tbb::blocked_range<std::size_t>& range) {
          process_range(range.begin(), range.end());
        },
        tbb::static_partitioner{});
  }

  _mm_sfence();

  for (std::size_t i = n32; i < n; ++i) {
    if (src[i] & 0x80) {
      error_flag.store(1, std::memory_order_relaxed);
    }
    dst[i] = static_cast<std::uint16_t>(src[i]);
  }

  std::uint32_t err = error_flag.load(std::memory_order_relaxed);
  if (err != 0) [[unlikely]] {
    return std::unexpected(
        ParseError{err == 1 ? ParseError::Code::VarintOverflow
                            : ParseError::Code::PaletteIndexOutOfRange,
                   "BlockData",
                   0});
  }

  return out;
}

#else  // Generic Highway path

[[nodiscard]] ParseResult<NoInitVector<std::uint16_t>> DecodeSingleByteFastImpl(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena) {
  const auto n = static_cast<std::size_t>(volume);
  const auto* src = reinterpret_cast<const std::uint8_t*>(data.data());

  NoInitVector<std::uint16_t> out(n,
                                  memory::NoInitAllocator<std::uint16_t>{&arena});
  auto* dst = out.data();

  const hn::ScalableTag<std::uint8_t> d8;
  const hn::Repartition<std::uint16_t, decltype(d8)> d16;
  const std::size_t lanes8 = hn::Lanes(d8);
  const std::size_t lanes16 = hn::Lanes(d16);

  const auto v_msb_mask = hn::Set(d8, static_cast<std::uint8_t>(0x80));
  auto v_msb_acc = hn::Zero(d8);
  auto v_max_acc = hn::Zero(d8);

  const std::size_t n_simd = (n / lanes8) * lanes8;
  std::size_t i = 0;

  for (; i < n_simd; i += lanes8) {
    const auto v = hn::LoadU(d8, src + i);
    const auto lo16 = hn::PromoteLowerTo(d16, v);
    const auto hi16 = hn::PromoteUpperTo(d16, v);
    hn::StoreU(lo16, d16, dst + i);
    hn::StoreU(hi16, d16, dst + i + lanes16);

    v_msb_acc = hn::Or(v_msb_acc, hn::And(v, v_msb_mask));
    v_max_acc = hn::Max(v_max_acc, v);
  }

  const std::uint8_t msb_max = hn::GetLane(hn::MaxOfLanes(d8, v_msb_acc));
  std::uint8_t max_val = hn::GetLane(hn::MaxOfLanes(d8, v_max_acc));

  std::uint8_t msb_or = (msb_max != 0) ? 0x80 : 0;
  for (; i < n; ++i) {
    if (src[i] & 0x80) msb_or |= 0x80;
    if (src[i] > max_val) max_val = src[i];
    dst[i] = static_cast<std::uint16_t>(src[i]);
  }

  if (msb_or & 0x80) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::VarintOverflow, "BlockData", 0});
  }
  if (palette_size <= 256 && max_val >= palette_size) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::PaletteIndexOutOfRange, "BlockData", 0});
  }

  return out;
}

#endif  // HWY_TARGET == HWY_AVX2

[[nodiscard]] std::pair<std::vector<std::uint64_t>, std::size_t>
BuildVarintCumulativeCountImpl(std::span<const std::byte> data) {
  const auto* p = reinterpret_cast<const std::uint8_t*>(data.data());
  const auto n = data.size();

  constexpr std::size_t kGranularity = 4096;

  const std::size_t num_gran_blocks = n / kGranularity;
  std::vector<std::uint64_t> cumsum(num_gran_blocks + 2);
  cumsum[0] = 0;

  const hn::ScalableTag<std::uint8_t> d8;
  const std::size_t lanes = hn::Lanes(d8);
  const std::size_t vecs_per_gran = kGranularity / lanes;

  std::uint64_t running = 0;

#if HWY_TARGET == HWY_AVX2
  const __m256i avx_msb = _mm256_set1_epi8(static_cast<char>(0x80));
  const __m256i avx_zero = _mm256_setzero_si256();

  for (std::size_t g = 0; g < num_gran_blocks; ++g) {
    const __m256i* block =
        reinterpret_cast<const __m256i*>(p + g * kGranularity);
    std::uint64_t local = 0;
    for (std::size_t v = 0; v < vecs_per_gran; ++v) {
      __m256i vec = _mm256_loadu_si256(block + v);
      __m256i hi = _mm256_and_si256(vec, avx_msb);
      __m256i is_end = _mm256_cmpeq_epi8(hi, avx_zero);
      std::uint32_t mask =
          static_cast<std::uint32_t>(_mm256_movemask_epi8(is_end));
      local += std::popcount(mask);
    }
    running += local;
    cumsum[g + 1] = running;
  }
#else
  const auto v_msb = hn::Set(d8, static_cast<std::uint8_t>(0x80));
  const auto v_zero = hn::Zero(d8);

  for (std::size_t g = 0; g < num_gran_blocks; ++g) {
    const std::uint8_t* block = p + g * kGranularity;
    std::uint64_t local = 0;
    for (std::size_t v = 0; v < vecs_per_gran; ++v) {
      const auto vec = hn::LoadU(d8, block + v * lanes);
      const auto hi = hn::And(vec, v_msb);
      const auto is_end = hn::Eq(hi, v_zero);
      local += hn::CountTrue(d8, is_end);
    }
    running += local;
    cumsum[g + 1] = running;
  }
#endif

  for (std::size_t i = num_gran_blocks * kGranularity; i < n; ++i) {
    if (!(p[i] & 0x80)) ++running;
  }
  cumsum[num_gran_blocks + 1] = running;

  return {std::move(cumsum), kGranularity};
}

// Per-target chunk-decode kernel. AVX2 path uses streaming stores +
// software prefetch; generic path uses Highway Prefetch + scalar
// varint decode (varint is inherently byte-oriented).
void DecodeVarintChunksKernelImpl(const std::uint8_t* const* chunk_ptrs,
                                  const std::size_t* chunk_offsets,
                                  const std::size_t* chunk_counts,
                                  std::size_t num_chunks,
                                  std::uint16_t* out,
                                  const std::uint8_t* data_end,
                                  std::size_t palette_size,
                                  std::atomic<std::uint32_t>& error_flag) {
  tbb::parallel_for(
      tbb::blocked_range<std::size_t>(0, num_chunks),
      [&](const tbb::blocked_range<std::size_t>& range) {
        for (std::size_t t = range.begin(); t < range.end(); ++t) {
          const std::uint8_t* p = chunk_ptrs[t];
          std::uint16_t* dst = out + chunk_offsets[t];
          const std::size_t cnt = chunk_counts[t];
          

#if HWY_TARGET == HWY_AVX2
          const std::size_t block_offset = chunk_offsets[t];
          const bool aligned = (block_offset % 16) == 0;

          if (aligned && cnt >= 16) [[likely]] {
            const std::size_t aligned_cnt = (cnt / 16) * 16;
            alignas(32) std::uint16_t buf16[16];
            std::size_t buf_count = 0;

            for (std::size_t i = 0; i < aligned_cnt; ++i) {
              if ((i & 31) == 0) [[unlikely]] {
                _mm_prefetch(reinterpret_cast<const char*>(p + 256),
                             _MM_HINT_T0);
              }
              std::uint32_t value = *p++;
              if (value & 0x80) {
                value &= 0x7F;
                std::uint32_t shift = 7;
                do {
                  if (p >= data_end) [[unlikely]] {
                    std::uint32_t expected = 0;
                    error_flag.compare_exchange_strong(
                        expected, 1, std::memory_order_relaxed);
                    return;
                  }
                  std::uint8_t b = *p++;
                  value |= (b & 0x7F) << shift;
                  if (!(b & 0x80)) break;
                  shift += 7;
                  if (shift > 28) [[unlikely]] {
                    std::uint32_t expected = 0;
                    error_flag.compare_exchange_strong(
                        expected, 1, std::memory_order_relaxed);
                    return;
                  }
                } while (true);
              }
              if (value >= palette_size) [[unlikely]] {
                std::uint32_t expected = 0;
                error_flag.compare_exchange_strong(
                    expected, 2, std::memory_order_relaxed);
                return;
              }
              buf16[buf_count++] = static_cast<std::uint16_t>(value);
              if (buf_count == 16) {
                __m256i v =
                    _mm256_load_si256(reinterpret_cast<const __m256i*>(buf16));
                _mm256_stream_si256(reinterpret_cast<__m256i*>(dst + i - 15),
                                    v);
                buf_count = 0;
              }
            }

            for (std::size_t i = aligned_cnt; i < cnt; ++i) {
              std::uint32_t value = *p++;
              if (value & 0x80) {
                value &= 0x7F;
                std::uint32_t shift = 7;
                do {
                  if (p >= data_end) [[unlikely]] {
                    std::uint32_t expected = 0;
                    error_flag.compare_exchange_strong(
                        expected, 1, std::memory_order_relaxed);
                    return;
                  }
                  std::uint8_t b = *p++;
                  value |= (b & 0x7F) << shift;
                  if (!(b & 0x80)) break;
                  shift += 7;
                  if (shift > 28) [[unlikely]] {
                    std::uint32_t expected = 0;
                    error_flag.compare_exchange_strong(
                        expected, 1, std::memory_order_relaxed);
                    return;
                  }
                } while (true);
              }
              if (value >= palette_size) [[unlikely]] {
                std::uint32_t expected = 0;
                error_flag.compare_exchange_strong(
                    expected, 2, std::memory_order_relaxed);
                return;
              }
              dst[i] = static_cast<std::uint16_t>(value);
            }
          } else {
            for (std::size_t i = 0; i < cnt; ++i) {
              if ((i & 31) == 0) [[unlikely]] {
                _mm_prefetch(reinterpret_cast<const char*>(p + 256),
                             _MM_HINT_T0);
              }
              std::uint32_t value = *p++;
              if (value & 0x80) {
                value &= 0x7F;
                std::uint32_t shift = 7;
                do {
                  if (p >= data_end) [[unlikely]] {
                    std::uint32_t expected = 0;
                    error_flag.compare_exchange_strong(
                        expected, 1, std::memory_order_relaxed);
                    return;
                  }
                  std::uint8_t b = *p++;
                  value |= (b & 0x7F) << shift;
                  if (!(b & 0x80)) break;
                  shift += 7;
                  if (shift > 28) [[unlikely]] {
                    std::uint32_t expected = 0;
                    error_flag.compare_exchange_strong(
                        expected, 1, std::memory_order_relaxed);
                    return;
                  }
                } while (true);
              }
              if (value >= palette_size) [[unlikely]] {
                std::uint32_t expected = 0;
                error_flag.compare_exchange_strong(
                    expected, 2, std::memory_order_relaxed);
                return;
              }
              dst[i] = static_cast<std::uint16_t>(value);
            }
          }
#else  // Generic Highway path
          for (std::size_t i = 0; i < cnt; ++i) {
            if ((i & 31) == 0) [[unlikely]] {
              hwy::Prefetch(p + 256);
            }
            std::uint32_t value = *p++;
            if (value & 0x80) {
              value &= 0x7F;
              std::uint32_t shift = 7;
              do {
                if (p >= data_end) [[unlikely]] {
                  std::uint32_t expected = 0;
                  error_flag.compare_exchange_strong(
                      expected, 1, std::memory_order_relaxed);
                  return;
                }
                std::uint8_t b = *p++;
                value |= (b & 0x7F) << shift;
                if (!(b & 0x80)) break;
                shift += 7;
                if (shift > 28) [[unlikely]] {
                  std::uint32_t expected = 0;
                  error_flag.compare_exchange_strong(
                      expected, 1, std::memory_order_relaxed);
                  return;
                }
              } while (true);
            }
            if (value >= palette_size) [[unlikely]] {
              std::uint32_t expected = 0;
              error_flag.compare_exchange_strong(
                  expected, 2, std::memory_order_relaxed);
              return;
            }
            dst[i] = static_cast<std::uint16_t>(value);
          }
#endif
        }
      },
      tbb::static_partitioner{});

#if HWY_TARGET == HWY_AVX2
  _mm_sfence();
#endif
}

}  // namespace HWY_NAMESPACE
}  // namespace fschema::schem::internal
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace fschema::schem::internal {
HWY_EXPORT(DecodeSingleByteFastImpl);
HWY_EXPORT(BuildVarintCumulativeCountImpl);
HWY_EXPORT(DecodeVarintChunksKernelImpl);

[[nodiscard]] ParseResult<NoInitVector<std::uint16_t>> DecodeSingleByteFast(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena) {
  return HWY_DYNAMIC_DISPATCH(DecodeSingleByteFastImpl)(
      data, volume, palette_size, arena);
}

[[nodiscard]] std::pair<std::vector<std::uint64_t>, std::size_t>
BuildVarintCumulativeCount(std::span<const std::byte> data) {
  return HWY_DYNAMIC_DISPATCH(BuildVarintCumulativeCountImpl)(data);
}

void DecodeVarintChunks(const std::uint8_t* const* chunk_ptrs,
                        const std::size_t* chunk_offsets,
                        const std::size_t* chunk_counts,
                        std::size_t num_chunks,
                        std::uint16_t* out,
                        const std::uint8_t* data_end,
                        std::size_t palette_size,
                        std::atomic<std::uint32_t>& error_flag) {
  HWY_DYNAMIC_DISPATCH(DecodeVarintChunksKernelImpl)(chunk_ptrs,
                                                     chunk_offsets,
                                                     chunk_counts,
                                                     num_chunks,
                                                     out,
                                                     data_end,
                                                     palette_size,
                                                     error_flag);
}

}  // namespace fschema::schem::internal
#endif  // HWY_ONCE