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
#define HWY_TARGET_INCLUDE "parser/litematic/detail/simd_block_states.cc"

#include "hwy/foreach_target.h"
#include "hwy/highway.h"

#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <algorithm>
#include <atomic>
#include <bit>
#include <expected>
#include <span>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include "parser/error.h"
#include "parser/litematic/detail/block_states.h"

HWY_BEFORE_NAMESPACE();
namespace fschema::parser::litematic::detail {
  namespace HWY_NAMESPACE {

    namespace hn = hwy::HWY_NAMESPACE;

    // Golden baseline two-stage kernel. Do not modify.
    template <typename DTag, typename ErrorFn>
    [[nodiscard]] ParseResult<void> UnpackKernel(
      DTag d_tag, std::span<const uint64_t> longs,
      uint32_t bits_per_block, uint64_t volume,
      std::size_t palette_size,
      uint32_t* __restrict out,
      ErrorFn&& error_report) {

      const uint64_t bit_mask = (1ULL << bits_per_block) - 1;
      const size_t lanes = hn::Lanes(d_tag);

      HWY_ALIGN uint64_t shifts_arr[8];
      for (size_t lane_idx = 0; lane_idx < lanes; ++lane_idx) {
        shifts_arr[lane_idx] = lane_idx * bits_per_block;
      }
      const auto vshifts = hn::Load(d_tag, shifts_arr);
      const auto vmask = hn::Set(d_tag, bit_mask);

      uint64_t block_idx = 0;
      for (; block_idx + lanes <= volume; block_idx += lanes) {
        const uint64_t start_off = block_idx * bits_per_block;
        const auto word_idx0 = static_cast<std::size_t>(start_off >> 6);
        const auto word_idx1 = static_cast<std::size_t>(
          ((block_idx + lanes) * bits_per_block - 1) >> 6);

        if (word_idx1 >= longs.size()) [[unlikely]] {
          return std::unexpected(
            error_report(ParseError::Code::BlockStatesTooSmall));
        }

        const auto bit_shift = static_cast<uint32_t>(start_off & 63);
        const uint64_t window =
          (bit_shift == 0)
          ? longs[word_idx0]
          : (longs[word_idx0] >> bit_shift) | (longs[word_idx1] << (64 - bit_shift));

        const auto window_vec = hn::Set(d_tag, window);
        const auto masked_vec = hn::And(hn::Shr(window_vec, vshifts), vmask);

        HWY_ALIGN uint64_t store_buffer[8];
        hn::Store(masked_vec, d_tag, store_buffer);
        for (size_t lane_idx = 0; lane_idx < lanes; ++lane_idx) {
          const auto idx = static_cast<uint32_t>(store_buffer[lane_idx]);
          if (idx >= palette_size) [[unlikely]] {
            return std::unexpected(
              error_report(ParseError::Code::PaletteIndexOutOfRange));
          }
          out[static_cast<std::size_t>(block_idx + lane_idx)] = idx;
        }
      }

      for (; block_idx < volume; ++block_idx) {
        const uint64_t bit_off = block_idx * bits_per_block;
        const auto word_idx0 = static_cast<std::size_t>(bit_off >> 6);
        const auto bit_shift = static_cast<uint32_t>(bit_off & 63);
        uint64_t value = longs[word_idx0] >> bit_shift;
        if (bit_shift + bits_per_block > 64) {
          value |= longs[word_idx0 + 1] << (64 - bit_shift);
        }
        const auto idx = static_cast<uint32_t>(value & bit_mask);
        if (idx >= palette_size) [[unlikely]] {
          return std::unexpected(
            error_report(ParseError::Code::PaletteIndexOutOfRange));
        }
        out[static_cast<std::size_t>(block_idx)] = idx;
      }

      return {};
    }

    // Common single block extraction with double long window for scalar tails.
    [[nodiscard]] inline uint32_t ExtractBlock(
      const std::byte* const __restrict raw_data,
      std::size_t long_count, uint32_t bits_per_block,
      uint64_t block_idx, uint64_t bit_mask) {
      const uint64_t bit_offset = block_idx * bits_per_block;
      const auto word_idx = static_cast<std::size_t>(bit_offset >> 6);
      uint64_t current_long;
      std::memcpy(&current_long, raw_data + word_idx * 8, 8);
      current_long = std::byteswap(current_long);
      const auto bit_shift = static_cast<uint32_t>(bit_offset & 63);
      uint64_t value = current_long >> bit_shift;
      if (bit_shift + bits_per_block > 64) {
        const auto next_word_idx = std::min(word_idx + 1, long_count - 1);
        uint64_t next_long;
        std::memcpy(&next_long, raw_data + next_word_idx * 8, 8);
        value |= std::byteswap(next_long) << (64 - bit_shift);
      }
      return static_cast<uint32_t>(value & bit_mask);
    }

#if HWY_TARGET == HWY_AVX2
    [[nodiscard]] ParseResult<void> UnpackFusedKernelAvx2(
      const std::byte* const __restrict raw_data,
      std::size_t long_count,
      uint32_t bits_per_block, uint64_t volume,
      std::size_t palette_size,
      uint32_t* const __restrict out) {

      const uint64_t bit_mask = (1ULL << bits_per_block) - 1;

      const __m256i vshifts = _mm256_setr_epi64x(
        0, static_cast<int64_t>(bits_per_block),
        static_cast<int64_t>(2) * bits_per_block,
        static_cast<int64_t>(3) * bits_per_block);
      const __m256i vmask = _mm256_set1_epi64x(static_cast<int64_t>(bit_mask));
      const __m256i vperm = _mm256_setr_epi32(0, 2, 4, 6, 0, 0, 0, 0);
      // vpshufb mask to byteswap 4 × uint64 in one 256-bit op
      const __m256i vbswap = _mm256_setr_epi8(
        7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8,
        7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8);

      uint64_t n_simd = 0;
      {
        const uint64_t safe_longs = (long_count > 5) ? (long_count - 5) : 0;
        const uint64_t safe_blocks = uint64_t(safe_longs) * 64 / bits_per_block;
        const uint64_t block_cap = (safe_blocks < volume) ? safe_blocks : volume;
        n_simd = (block_cap / 16) * 16;
      }

      std::atomic<uint32_t> error_flag{ 0 };
      uint64_t num_simd_iters = n_simd / 16;

      auto process_range = [&](uint64_t range_begin, uint64_t range_end) {
        __m256i vmax0 = _mm256_setzero_si256();
        __m256i vmax1 = _mm256_setzero_si256();
        __m256i vmax2 = _mm256_setzero_si256();
        __m256i vmax3 = _mm256_setzero_si256();

        auto GetWindow = [&](uint64_t bi, const uint64_t* longs, size_t first_long) -> uint64_t {
          const uint64_t boff = bi * bits_per_block;
          const size_t wi = static_cast<size_t>(boff >> 6) - first_long;
          const auto bs = static_cast<uint32_t>(boff & 63);
          const uint64_t high_bits = (bs == 0) ? uint64_t(0) : (longs[wi + 1] << (64 - bs));
          return (longs[wi] >> bs) | high_bits;
          };

        for (uint64_t iter_idx = range_begin; iter_idx < range_end; ++iter_idx) {
          const uint64_t block_idx = iter_idx * 16;

          const uint64_t first_bit = block_idx * bits_per_block;
          const size_t first_long = static_cast<size_t>(first_bit >> 6);

          uint64_t longs[5];
          std::memcpy(longs, raw_data + first_long * 8, 40);
          longs[0] = std::byteswap(longs[0]);
          longs[1] = std::byteswap(longs[1]);
          longs[2] = std::byteswap(longs[2]);
          longs[3] = std::byteswap(longs[3]);
          longs[4] = std::byteswap(longs[4]);

          const uint64_t w0 = GetWindow(block_idx, longs, first_long);
          const uint64_t w1 = GetWindow(block_idx + 4, longs, first_long);
          const uint64_t w2 = GetWindow(block_idx + 8, longs, first_long);
          const uint64_t w3 = GetWindow(block_idx + 12, longs, first_long);

          // slot 0
          const __m256i unpacked_vec0 = _mm256_and_si256(_mm256_srlv_epi64(_mm256_set1_epi64x(static_cast<int64_t>(w0)), vshifts), vmask);
          vmax0 = _mm256_max_epu32(vmax0, unpacked_vec0);
          const __m256i packed_vec0 = _mm256_permutevar8x32_epi32(unpacked_vec0, vperm);

          // slot 1
          const __m256i unpacked_vec1 = _mm256_and_si256(_mm256_srlv_epi64(_mm256_set1_epi64x(static_cast<int64_t>(w1)), vshifts), vmask);
          vmax1 = _mm256_max_epu32(vmax1, unpacked_vec1);
          const __m256i packed_vec1 = _mm256_permutevar8x32_epi32(unpacked_vec1, vperm);

          const __m256i res01 = _mm256_inserti128_si256(packed_vec0, _mm256_castsi256_si128(packed_vec1), 1);
          _mm256_stream_si256(reinterpret_cast<__m256i*>(out + block_idx), res01);

          // slot 2
          const __m256i unpacked_vec2 = _mm256_and_si256(_mm256_srlv_epi64(_mm256_set1_epi64x(static_cast<int64_t>(w2)), vshifts), vmask);
          vmax2 = _mm256_max_epu32(vmax2, unpacked_vec2);
          const __m256i packed_vec2 = _mm256_permutevar8x32_epi32(unpacked_vec2, vperm);

          // slot 3
          const __m256i unpacked_vec3 = _mm256_and_si256(_mm256_srlv_epi64(_mm256_set1_epi64x(static_cast<int64_t>(w3)), vshifts), vmask);
          vmax3 = _mm256_max_epu32(vmax3, unpacked_vec3);
          const __m256i packed_vec3 = _mm256_permutevar8x32_epi32(unpacked_vec3, vperm);

          const __m256i res23 = _mm256_inserti128_si256(packed_vec2, _mm256_castsi256_si128(packed_vec3), 1);
          _mm256_stream_si256(reinterpret_cast<__m256i*>(out + block_idx + 8), res23);
        }

        __m256i max_vec01 = _mm256_max_epu32(vmax0, vmax1);
        __m256i max_vec23 = _mm256_max_epu32(vmax2, vmax3);
        __m256i max_vec = _mm256_max_epu32(max_vec01, max_vec23);

        const __m256i v_palette = _mm256_set1_epi32(static_cast<int32_t>(palette_size));
        const __m256i sign_flip = _mm256_set1_epi32(static_cast<int32_t>(0x80000000));
        const __m256i v_palette_signed = _mm256_xor_si256(v_palette, sign_flip);
        const __m256i max_vec_signed = _mm256_xor_si256(max_vec, sign_flip);
        const __m256i cmp = _mm256_cmpgt_epi32(v_palette_signed, max_vec_signed);
        const bool out_of_range = !_mm256_testc_si256(cmp, _mm256_set1_epi32(-1));

        if (out_of_range) [[unlikely]] {
          HWY_ALIGN uint32_t max_values[8];
          _mm256_store_si256(reinterpret_cast<__m256i*>(max_values), max_vec);
          for (int lane_idx = 0; lane_idx < 8; ++lane_idx) {
            if (max_values[lane_idx] >= palette_size) {
              uint32_t expected = 0;
              error_flag.compare_exchange_strong(expected, max_values[lane_idx], std::memory_order_relaxed);
              break;
            }
          }
        }
        }; // end of process_range

      constexpr uint64_t kParallelThreshold = 65536;
      if (num_simd_iters < 1024) {
        process_range(0, num_simd_iters);
      }
      else {
        tbb::parallel_for(
          tbb::blocked_range<uint64_t>(0, num_simd_iters),
          [&](const tbb::blocked_range<uint64_t>& range) {
            process_range(range.begin(), range.end());
          });
      }

      _mm_sfence();

      for (uint64_t tail_idx = n_simd; tail_idx < volume; ++tail_idx) {
        const auto idx = ExtractBlock(raw_data, long_count, bits_per_block, tail_idx, bit_mask);
        if (idx >= palette_size) [[unlikely]] {
          return std::unexpected(ParseError{
              ParseError::Code::PaletteIndexOutOfRange,
              "BlockStates", 0 });
        }
        out[tail_idx] = idx;
      }

      if (error_flag.load(std::memory_order_relaxed) != 0) [[unlikely]] {
        return std::unexpected(ParseError{
            ParseError::Code::PaletteIndexOutOfRange,
            "BlockStates", 0 });
      }

      return {};
    }
#endif // HWY_TARGET == HWY_AVX2

    template <typename D64Tag>
    [[nodiscard]] ParseResult<void> UnpackFusedKernelHwy(
      D64Tag d_tag,
      const std::byte* const __restrict raw_data,
      std::size_t long_count,
      uint32_t bits_per_block, uint64_t volume,
      std::size_t palette_size,
      uint32_t* const __restrict out) {

      const uint64_t bit_mask = (1ULL << bits_per_block) - 1;
      const size_t lanes = hn::Lanes(d_tag);

      HWY_ALIGN uint64_t shift_arr[8];
      for (size_t lane_idx = 0; lane_idx < lanes; ++lane_idx) {
        shift_arr[lane_idx] = uint64_t(lane_idx) * bits_per_block;
      }
      const auto vshifts = hn::Load(d_tag, shift_arr);
      const auto vmask = hn::Set(d_tag, bit_mask);

      auto Window = [&](uint64_t block_idx) -> uint64_t {
        const uint64_t bit_offset = block_idx * bits_per_block;
        const auto word_idx = static_cast<std::size_t>(bit_offset >> 6);
        const auto next_word_idx = word_idx + 1;
        uint64_t current_long, next_long;
        std::memcpy(&current_long, raw_data + word_idx * 8, 8);
        std::memcpy(&next_long, raw_data + next_word_idx * 8, 8);
        current_long = std::byteswap(current_long);
        next_long = std::byteswap(next_long);
        const auto bit_shift = static_cast<uint32_t>(bit_offset & 63);
        const uint64_t high_bits = (bit_shift == 0) ? uint64_t(0) : next_long << (64 - bit_shift);
        return (current_long >> bit_shift) | high_bits;
        };

      uint64_t n_simd = 0;
      {
        const uint64_t safe_blocks = uint64_t(long_count) * 64 / bits_per_block;
        const uint64_t block_cap = (safe_blocks < volume) ? safe_blocks : volume;
        n_simd = (block_cap / lanes) * lanes;
      }

      auto max_vec = hn::Zero(d_tag);

      for (uint64_t block_idx = 0; block_idx < n_simd; block_idx += lanes) {
        const uint64_t window_val = Window(block_idx);
        const auto window_vec = hn::Set(d_tag, window_val);
        const auto masked_vec = hn::And(hn::Shr(window_vec, vshifts), vmask);

        max_vec = hn::Max(max_vec, masked_vec);

        HWY_ALIGN uint64_t store_buffer[8];
        hn::Store(masked_vec, d_tag, store_buffer);
        for (size_t lane_idx = 0; lane_idx < lanes; ++lane_idx) {
          out[block_idx + lane_idx] = static_cast<uint32_t>(store_buffer[lane_idx]);
        }
      }

      HWY_ALIGN uint64_t max_values[8];
      hn::Store(max_vec, d_tag, max_values);
      for (size_t lane_idx = 0; lane_idx < lanes; ++lane_idx) {
        if (max_values[lane_idx] >= palette_size) [[unlikely]] {
          return std::unexpected(ParseError{
              ParseError::Code::PaletteIndexOutOfRange,
              "BlockStates", 0 });
        }
      }

      for (uint64_t tail_idx = n_simd; tail_idx < volume; ++tail_idx) {
        const auto idx = ExtractBlock(raw_data, long_count, bits_per_block, tail_idx, bit_mask);
        if (idx >= palette_size) [[unlikely]] {
          return std::unexpected(ParseError{
              ParseError::Code::PaletteIndexOutOfRange,
              "BlockStates", 0 });
        }
        out[tail_idx] = idx;
      }

      return {};
    }

    ParseResult<NoInitVector<uint32_t>> UnpackIndicesFusedImpl(
      std::span<const std::byte> raw_longs,
      uint32_t bits_per_block, uint64_t volume,
      std::size_t palette_size) {

      const auto long_count = raw_longs.size() / 8;
      auto make_error = [](ParseError::Code code) {
        return ParseError{ code, "BlockStates", 0 };
        };

      if (volume == 0) return NoInitVector<uint32_t>{};

      if (palette_size < 2 || bits_per_block < 2) {
        return std::unexpected(make_error(ParseError::Code::PaletteIndexOutOfRange));
      }

      const uint64_t min_longs = (volume * bits_per_block + 63) / 64;
      if (long_count < min_longs) {
        return std::unexpected(make_error(ParseError::Code::BlockStatesTooSmall));
      }
      if (palette_size == 0) {
        return std::unexpected(make_error(ParseError::Code::PaletteIndexOutOfRange));
      }

      NoInitVector<uint32_t> out(static_cast<std::size_t>(volume));
      const std::byte* raw_data = raw_longs.data();
      ParseResult<void> kernel_result{};

      if (bits_per_block >= 2 && bits_per_block <= 16) {
#if HWY_TARGET == HWY_AVX2
        kernel_result = UnpackFusedKernelAvx2(
          raw_data, long_count, bits_per_block, volume, palette_size, out.data());
#else
        const hn::CappedTag<uint64_t, 4> d_tag;
        kernel_result = UnpackFusedKernelHwy(
          d_tag, raw_data, long_count, bits_per_block, volume, palette_size, out.data());
#endif
      }
      else {
        kernel_result = std::unexpected(make_error(ParseError::Code::PaletteIndexOutOfRange));
      }

      if (!kernel_result) return std::unexpected(kernel_result.error());
      return out;
    }

    // Golden baseline two-stage entry point. Do not modify.
    ParseResult<NoInitVector<uint32_t>> UnpackIndicesHwyImpl(
      std::span<const uint64_t> longs,
      uint32_t bits_per_block, uint64_t volume,
      std::size_t palette_size) {

      NoInitVector<uint32_t> out(static_cast<std::size_t>(volume));
      auto make_error = [](ParseError::Code code) {
        return ParseError{ code, "BlockStates", 0 };
        };

      if (bits_per_block >= 1 && bits_per_block <= 8) {
        const hn::ScalableTag<uint64_t> d_tag;
        auto result = UnpackKernel(d_tag, longs, bits_per_block, volume,
          palette_size, out.data(), make_error);
        if (!result) return std::unexpected(result.error());
        return out;
      }
      else if (bits_per_block >= 9 && bits_per_block <= 15) {
        const hn::CappedTag<uint64_t, 4> d_tag;
        auto result = UnpackKernel(d_tag, longs, bits_per_block, volume,
          palette_size, out.data(), make_error);
        if (!result) return std::unexpected(result.error());
        return out;
      }
      else {
        return UnpackIndicesScalar(longs, bits_per_block, volume, palette_size);
      }
    }

  } // namespace HWY_NAMESPACE
} // namespace fschema::parser::litematic::detail
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace fschema::parser::litematic::detail {
  HWY_EXPORT(UnpackIndicesFusedImpl);
  HWY_EXPORT(UnpackIndicesHwyImpl);

  [[nodiscard]] ParseResult<NoInitVector<uint32_t>> UnpackIndicesFused(
    std::span<const std::byte> raw_longs,
    uint32_t bits_per_block, uint64_t volume,
    std::size_t palette_size) {
    return HWY_DYNAMIC_DISPATCH(UnpackIndicesFusedImpl)(
      raw_longs, bits_per_block, volume, palette_size);
  }

  [[nodiscard]] ParseResult<NoInitVector<uint32_t>> UnpackIndicesHwy(
    std::span<const uint64_t> longs,
    uint32_t bits_per_block, uint64_t volume,
    std::size_t palette_size) {
    return HWY_DYNAMIC_DISPATCH(UnpackIndicesHwyImpl)(
      longs, bits_per_block, volume, palette_size);
  }

} // namespace fschema::parser::litematic::detail
#endif // HWY_ONCE