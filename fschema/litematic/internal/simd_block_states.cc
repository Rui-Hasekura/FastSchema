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
#define HWY_TARGET_INCLUDE "fschema/litematic/internal/simd_block_states.cc"

#include "hwy/foreach_target.h"
#include "hwy/highway.h"

#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
#  include <immintrin.h>
#endif

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>

#include "fschema/base/error.h"
#include "fschema/base/port.h"
#include "fschema/litematic/internal/block_states.h"

HWY_BEFORE_NAMESPACE();
namespace fschema::litematic::internal {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// Reference Highway kernel. Expects longs pre-bswapped to host order.
// Not dispatched at runtime (the fused kernels below are what actually
// execute), but documents the core algorithm shared by both fused paths.
//
// For each group of `lanes` blocks, builds a 64-bit window (possibly
// straddling two longs), broadcasts it to all lanes, then applies per-lane
// variable right-shift (VPSRLVQ on AVX2, Shr in Highway) and AND with the
// bit mask to extract one block per lane.
template <typename DTag, typename ErrorFn>
[[nodiscard]] ParseResult<void> UnpackKernel(
    DTag d_tag,
    std::span<const std::uint64_t> longs,
    std::uint32_t bits_per_block,
    std::uint64_t volume,
    std::size_t palette_size,
    std::uint16_t* FSCHEMA_RESTRICT out,
    ErrorFn&& error_report) {
  const std::uint64_t bit_mask = (1ULL << bits_per_block) - 1;
  const std::size_t lanes = hn::Lanes(d_tag);

  // Per-lane shift amounts: {0, bpb, 2*bpb, 3*bpb}.
  const auto vshifts =
      hn::Mul(hn::Iota(d_tag, std::uint64_t{0}),
              hn::Set(d_tag, static_cast<std::uint64_t>(bits_per_block)));
  const auto vmask = hn::Set(d_tag, bit_mask);

  std::uint64_t block_idx = 0;
  // Main vectorized loop: processes `lanes` blocks per iteration.
  for (; block_idx + lanes <= volume; block_idx += lanes) {
    const std::uint64_t start_off = block_idx * bits_per_block;
    const auto word_idx0 = static_cast<std::size_t>(start_off >> 6);
    const auto word_idx1 = static_cast<std::size_t>(
        ((block_idx + lanes) * bits_per_block - 1) >> 6);

    if (word_idx1 >= longs.size()) [[unlikely]] {
      return std::unexpected(
          error_report(ParseError::Code::BlockStatesTooSmall));
    }

    // Build a 64-bit window starting at start_off. If the window crosses a
    // long boundary, stitch the low bits of longs[word_idx0] together with
    // the high bits of longs[word_idx1].
    const auto bit_shift = static_cast<std::uint32_t>(start_off & 63);
    const std::uint64_t window =
        (bit_shift == 0) ? longs[word_idx0]
                         : (longs[word_idx0] >> bit_shift) |
                               (longs[word_idx1] << (64 - bit_shift));

    // Broadcast the 64-bit window to all lanes, apply variable right shift,
    // and mask out the target bpb bits.
    const auto window_vec = hn::Set(d_tag, window);
    const auto masked_vec = hn::And(hn::Shr(window_vec, vshifts), vmask);

    HWY_ALIGN std::uint64_t store_buffer[8];
    hn::Store(masked_vec, d_tag, store_buffer);
    for (std::size_t lane_idx = 0; lane_idx < lanes; ++lane_idx) {
      const auto idx = static_cast<std::uint32_t>(store_buffer[lane_idx]);
      if (idx >= palette_size) [[unlikely]] {
        return std::unexpected(
            error_report(ParseError::Code::PaletteIndexOutOfRange));
      }
      out[static_cast<std::size_t>(block_idx + lane_idx)] = idx;
    }
  }

  // Scalar tail: blocks that didn't fill a full lane group.
  for (; block_idx < volume; ++block_idx) {
    const std::uint64_t bit_off = block_idx * bits_per_block;
    const auto word_idx0 = static_cast<std::size_t>(bit_off >> 6);
    const auto bit_shift = static_cast<std::uint32_t>(bit_off & 63);
    std::uint64_t value = longs[word_idx0] >> bit_shift;
    if (bit_shift + bits_per_block > 64) {
      value |= longs[word_idx0 + 1] << (64 - bit_shift);
    }
    const auto idx = static_cast<std::uint32_t>(value & bit_mask);
    if (idx >= palette_size) [[unlikely]] {
      return std::unexpected(
          error_report(ParseError::Code::PaletteIndexOutOfRange));
    }
    out[static_cast<std::size_t>(block_idx)] = idx;
  }

  return {};
}

// Scalar extraction of a single block from the raw big-endian long array.
// Used for tail blocks in both the AVX2 and Highway paths.
//
// Reads the long containing the block's first bit, byte-swaps it (NBT
// stores longs big-endian), and shifts right to align the block. If the
// block straddles into the next long, ORs in the high bits from it. The
// next-long index is clamped to long_count - 1 so the final block in the
// array does not read out of bounds.
[[nodiscard]] inline std::uint32_t ExtractBlock(
    const std::byte* const FSCHEMA_RESTRICT raw_data,
    std::size_t long_count,
    std::uint32_t bits_per_block,
    std::uint64_t block_idx,
    std::uint64_t bit_mask) {
  const std::uint64_t bit_offset = block_idx * bits_per_block;
  const auto word_idx = static_cast<std::size_t>(bit_offset >> 6);

  // Read and swap the current long.
  std::uint64_t current_long;
  std::memcpy(&current_long, raw_data + word_idx * 8, 8);
  current_long = std::byteswap(current_long);
  const auto bit_shift = static_cast<std::uint32_t>(bit_offset & 63);
  std::uint64_t value = current_long >> bit_shift;

  // Handle straddling blocks: if the block extends past the 64-bit boundary,
  // read the next long and OR its high bits into the value.
  if (bit_shift + bits_per_block > 64) {
    // Clamp to long_count - 1 to prevent OOB reads on the final block,
    // even though the high bits are effectively zero in that case.
    const auto next_word_idx = std::min(word_idx + 1, long_count - 1);
    std::uint64_t next_long;
    std::memcpy(&next_long, raw_data + next_word_idx * 8, 8);
    value |= std::byteswap(next_long) << (64 - bit_shift);
  }
  return static_cast<std::uint32_t>(value & bit_mask);
}

#if HWY_TARGET == HWY_AVX2

// AVX2 unpacker: 16 blocks per iteration via four 4-block windows.
//
// Each window is a 64-bit value (possibly straddling two longs) containing
// 4 consecutive blocks. VPSRLVQ shifts each 64-bit lane by a different
// multiple of bpb, then AND with the bit mask isolates one block per lane.
// The four windows are packed (VPERMD + PACKUS + VINSERTI128) into 16 x
// uint16 and written with a non-temporal store.
//
// For inputs above 1024 iterations (16K blocks), the loop is parallelized
// with TBB. The workload is uniform across iterations, so static_partitioner
// avoids work-stealing overhead.
[[nodiscard]] ParseResult<void> UnpackFusedKernelAvx2(
    const std::byte* const FSCHEMA_RESTRICT raw_data,
    std::size_t long_count,
    std::uint32_t bits_per_block,
    std::uint64_t volume,
    std::size_t palette_size,
    std::uint16_t* const FSCHEMA_RESTRICT out) {
  const std::uint64_t bit_mask = (1ULL << bits_per_block) - 1;

  // Per-lane shift amounts for VPSRLVQ: {0, bpb, 2*bpb, 3*bpb}.
  const __m256i vshifts =
      _mm256_setr_epi64x(0,
                         static_cast<std::int64_t>(bits_per_block),
                         static_cast<std::int64_t>(2) * bits_per_block,
                         static_cast<std::int64_t>(3) * bits_per_block);

  // Bit mask replicated across all 64-bit lanes.
  const __m256i vmask = _mm256_set1_epi64x(static_cast<std::int64_t>(bit_mask));

  // Permutation control for VPERMD: gathers the low 32 bits of each 64-bit
  // lane (indices 0, 2, 4, 6) into the lower 128 bits, preparing for PACKUS.
  const __m256i vperm = _mm256_setr_epi32(0, 2, 4, 6, 0, 0, 0, 0);

  std::uint64_t n_simd = 0;
  {
    const std::uint64_t safe_longs = (long_count > 5) ? (long_count - 5) : 0;
    const std::uint64_t safe_blocks =
        std::uint64_t(safe_longs) * 64 / bits_per_block;
    const std::uint64_t block_cap =
        (safe_blocks < volume) ? safe_blocks : volume;
    n_simd = (block_cap / 16) * 16;
  }

  std::atomic<std::uint32_t> error_flag{0};
  std::uint64_t num_simd_iters = n_simd / 16;

  auto process_range = [&](std::uint64_t range_begin, std::uint64_t range_end) {
    // Per-thread max trackers. Initialized to zero and updated with VPMAXUD
    // (_mm256_max_epu32) after each window. Checked against palette_size
    // once after the loop.
    __m256i vmax0 = _mm256_setzero_si256();
    __m256i vmax1 = _mm256_setzero_si256();
    __m256i vmax2 = _mm256_setzero_si256();
    __m256i vmax3 = _mm256_setzero_si256();

    // Extract a 64-bit window starting at block `bi`'s first bit from the
    // pre-bswapped local `longs` array (indexed relative to first_long).
    auto GetWindow = [&](std::uint64_t bi,
                         const std::uint64_t* longs,
                         std::size_t first_long) -> std::uint64_t {
      const std::uint64_t boff = bi * bits_per_block;
      const std::size_t wi = static_cast<std::size_t>(boff >> 6) - first_long;
      const auto bs = static_cast<std::uint32_t>(boff & 63);
      const std::uint64_t high_bits =
          (bs == 0) ? std::uint64_t(0) : (longs[wi + 1] << (64 - bs));
      return (longs[wi] >> bs) | high_bits;
    };

    for (std::uint64_t iter_idx = range_begin; iter_idx < range_end;
         ++iter_idx) {
      const std::uint64_t block_idx = iter_idx * 16;

      const std::uint64_t first_bit = block_idx * bits_per_block;
      const std::size_t first_long = static_cast<std::size_t>(first_bit >> 6);

      // Load and byte-swap the 5 longs that may be touched by this
      // 16-block group. Individual bswap is cheaper than a separate
      // full-array bswap pass and only touches data that will be used.
      std::uint64_t longs[5];
      std::memcpy(longs, raw_data + first_long * 8, 40);
      longs[0] = std::byteswap(longs[0]);
      longs[1] = std::byteswap(longs[1]);
      longs[2] = std::byteswap(longs[2]);
      longs[3] = std::byteswap(longs[3]);
      longs[4] = std::byteswap(longs[4]);

      // Generate 4 windows, each containing 4 blocks.
      const std::uint64_t w0 = GetWindow(block_idx, longs, first_long);
      const std::uint64_t w1 = GetWindow(block_idx + 4, longs, first_long);
      const std::uint64_t w2 = GetWindow(block_idx + 8, longs, first_long);
      const std::uint64_t w3 = GetWindow(block_idx + 12, longs, first_long);

      // Window 0: broadcast, variable-shift, mask, track max.
      const __m256i unpacked_vec0 = _mm256_and_si256(
          _mm256_srlv_epi64(_mm256_set1_epi64x(static_cast<std::int64_t>(w0)),
                            vshifts),
          vmask);
      vmax0 = _mm256_max_epu32(vmax0, unpacked_vec0);
      // VPERMD: gather low-32 of each 64-bit lane into lower 128 bits.
      const __m256i packed_vec0 =
          _mm256_permutevar8x32_epi32(unpacked_vec0, vperm);

      // Window 1.
      const __m256i unpacked_vec1 = _mm256_and_si256(
          _mm256_srlv_epi64(_mm256_set1_epi64x(static_cast<std::int64_t>(w1)),
                            vshifts),
          vmask);
      vmax1 = _mm256_max_epu32(vmax1, unpacked_vec1);
      const __m256i packed_vec1 =
          _mm256_permutevar8x32_epi32(unpacked_vec1, vperm);

      // Window 2.
      const __m256i unpacked_vec2 = _mm256_and_si256(
          _mm256_srlv_epi64(_mm256_set1_epi64x(static_cast<std::int64_t>(w2)),
                            vshifts),
          vmask);
      vmax2 = _mm256_max_epu32(vmax2, unpacked_vec2);
      const __m256i packed_vec2 =
          _mm256_permutevar8x32_epi32(unpacked_vec2, vperm);

      // Window 3
      const __m256i unpacked_vec3 = _mm256_and_si256(
          _mm256_srlv_epi64(_mm256_set1_epi64x(static_cast<std::int64_t>(w3)),
                            vshifts),
          vmask);
      vmax3 = _mm256_max_epu32(vmax3, unpacked_vec3);
      const __m256i packed_vec3 =
          _mm256_permutevar8x32_epi32(unpacked_vec3, vperm);

      // Pack 4x uint32 -> 4x uint16 via PACKUS (unsigned saturation, but
      // values are guaranteed to fit in 16 bits by the palette size limit).
      const __m128i pack01 =
          _mm_packus_epi32(_mm256_castsi256_si128(packed_vec0),
                           _mm256_castsi256_si128(packed_vec1));

      const __m128i pack23 =
          _mm_packus_epi32(_mm256_castsi256_si128(packed_vec2),
                           _mm256_castsi256_si128(packed_vec3));

      // Combine two 128-bit halves into one 256-bit register.
      const __m256i result =
          _mm256_inserti128_si256(_mm256_castsi128_si256(pack01), pack23, 1);

      // Non-temporal store: the output buffer is large (up to 512 MiB) and
      // won't be read back until the caller materializes the region, so
      // polluting the cache would evict useful data.
      _mm256_stream_si256(reinterpret_cast<__m256i*>(out + block_idx), result);
    }

    // Reduce the four per-window max vectors to one.
    __m256i max_vec01 = _mm256_max_epu32(vmax0, vmax1);
    __m256i max_vec23 = _mm256_max_epu32(vmax2, vmax3);
    __m256i max_vec = _mm256_max_epu32(max_vec01, max_vec23);

    // PCMPGTD is signed-only. XOR both operands with 0x80000000 to remap
    // the unsigned range [0, 2^32) to the signed range [-2^31, 2^31),
    // making the signed comparison behave as unsigned.
    const __m256i v_palette =
        _mm256_set1_epi32(static_cast<std::int32_t>(palette_size));
    const __m256i sign_flip =
        _mm256_set1_epi32(static_cast<std::int32_t>(0x80000000));
    const __m256i v_palette_signed = _mm256_xor_si256(v_palette, sign_flip);
    const __m256i max_vec_signed = _mm256_xor_si256(max_vec, sign_flip);
    // v_palette_signed > max_vec_signed (signed) <=> v_palette > max_vec
    // (unsigned). All lanes true means every index is in range.
    const __m256i cmp = _mm256_cmpgt_epi32(v_palette_signed, max_vec_signed);
    // VPTEST with all-ones: returns 1 iff every bit of cmp is set. Negation
    // detects any out-of-range lane.
    const bool out_of_range = !_mm256_testc_si256(cmp, _mm256_set1_epi32(-1));

    if (out_of_range) [[unlikely]] {
      HWY_ALIGN std::uint32_t max_values[8];
      _mm256_store_si256(reinterpret_cast<__m256i*>(max_values), max_vec);
      for (int lane_idx = 0; lane_idx < 8; ++lane_idx) {
        if (max_values[lane_idx] >= palette_size) {
          // First-error-wins. Relaxed ordering is sufficient: TBB's
          // parallel_for provides the happens-before edge for the
          // post-loop load.
          std::uint32_t expected = 0;
          error_flag.compare_exchange_strong(
              expected, max_values[lane_idx], std::memory_order_relaxed);
          break;
        }
      }
    }
  };  // end of process_range

  // Threshold 1024 iterations (16K blocks) to avoid TBB overhead on small
  // inputs.
  if (num_simd_iters < 1024) {
    process_range(0, num_simd_iters);
  } else {
    tbb::parallel_for(
        tbb::blocked_range<std::uint64_t>(0, num_simd_iters),
        [&](const tbb::blocked_range<std::uint64_t>& range) {
          process_range(range.begin(), range.end());
        },
        tbb::static_partitioner{});
  }

  // Non-temporal stores are weakly ordered. Fence before the scalar tail
  // (and before the caller reads the buffer) to ensure global visibility.
  _mm_sfence();

  // Scalar tail: blocks that didn't fill a 16-block group.
  for (std::uint64_t tail_idx = n_simd; tail_idx < volume; ++tail_idx) {
    const auto idx =
        ExtractBlock(raw_data, long_count, bits_per_block, tail_idx, bit_mask);
    if (idx >= palette_size) [[unlikely]] {
      return std::unexpected(ParseError{
          ParseError::Code::PaletteIndexOutOfRange, "BlockStates", 0});
    }
    out[tail_idx] = idx;
  }

  if (error_flag.load(std::memory_order_relaxed) != 0) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::PaletteIndexOutOfRange, "BlockStates", 0});
  }

  return {};
}
#endif  // HWY_TARGET == HWY_AVX2

// Highway fallback kernel for non-AVX2 targets. Same windowing algorithm as
// the AVX2 path but processes 4 blocks per iteration (one window) and runs
// single-threaded. CappedTag<u64, 4> pins the lane count to 4 so the
// 4-block window always fits in one 64-bit word regardless of the target's
// native vector width.
template <typename D64Tag>
[[nodiscard]] ParseResult<void> UnpackFusedKernelHwy(
    D64Tag d_tag,
    const std::byte* const FSCHEMA_RESTRICT raw_data,
    std::size_t long_count,
    std::uint32_t bits_per_block,
    std::uint64_t volume,
    std::size_t palette_size,
    std::uint16_t* const FSCHEMA_RESTRICT out) {
  const std::uint64_t bit_mask = (1ULL << bits_per_block) - 1;
  const std::size_t lanes = hn::Lanes(d_tag);

  const auto vshifts =
      hn::Mul(hn::Iota(d_tag, std::uint64_t{0}),
              hn::Set(d_tag, static_cast<std::uint64_t>(bits_per_block)));
  const auto vmask = hn::Set(d_tag, bit_mask);

  // Build a 64-bit window starting at block_idx's first bit. The next-long
  // read is guarded because the last block in the array may not have a
  // successor long.
  auto Window = [&](std::uint64_t block_idx) -> std::uint64_t {
    const std::uint64_t bit_offset = block_idx * bits_per_block;
    const auto word_idx = static_cast<std::size_t>(bit_offset >> 6);
    const auto next_word_idx = word_idx + 1;
    std::uint64_t current_long;
    std::uint64_t next_long = 0;
    std::memcpy(&current_long, raw_data + word_idx * 8, 8);
    current_long = std::byteswap(current_long);
    // OOB check for next_long
    if (next_word_idx < long_count) {
      std::memcpy(&next_long, raw_data + next_word_idx * 8, 8);
      next_long = std::byteswap(next_long);
    }
    const auto bit_shift = static_cast<std::uint32_t>(bit_offset & 63);
    const std::uint64_t high_bits =
        (bit_shift == 0) ? std::uint64_t(0) : next_long << (64 - bit_shift);
    return (current_long >> bit_shift) | high_bits;
  };

  // safe_blocks counts blocks whose first bit falls within the long array.
  // The Window lambda guards the next-long read for the last block.
  std::uint64_t n_simd = 0;
  {
    const std::uint64_t safe_blocks =
        std::uint64_t(long_count) * 64 / bits_per_block;
    const std::uint64_t block_cap =
        (safe_blocks < volume) ? safe_blocks : volume;
    n_simd = (block_cap / lanes) * lanes;
  }

  auto max_vec = hn::Zero(d_tag);

  for (std::uint64_t block_idx = 0; block_idx < n_simd; block_idx += lanes) {
    const std::uint64_t window_val = Window(block_idx);
    const auto window_vec = hn::Set(d_tag, window_val);
    const auto masked_vec = hn::And(hn::Shr(window_vec, vshifts), vmask);

    max_vec = hn::Max(max_vec, masked_vec);

    HWY_ALIGN std::uint64_t store_buffer[8];
    hn::Store(masked_vec, d_tag, store_buffer);
    for (std::size_t lane_idx = 0; lane_idx < lanes; ++lane_idx) {
      out[block_idx + lane_idx] =
          static_cast<std::uint16_t>(store_buffer[lane_idx]);
    }
  }

  // Deferred range check: extract the max and compare once.
  HWY_ALIGN std::uint64_t max_values[8];
  hn::Store(max_vec, d_tag, max_values);
  for (std::size_t lane_idx = 0; lane_idx < lanes; ++lane_idx) {
    if (max_values[lane_idx] >= palette_size) [[unlikely]] {
      return std::unexpected(ParseError{
          ParseError::Code::PaletteIndexOutOfRange, "BlockStates", 0});
    }
  }

  // Scalar tail.
  for (std::uint64_t tail_idx = n_simd; tail_idx < volume; ++tail_idx) {
    const auto idx =
        ExtractBlock(raw_data, long_count, bits_per_block, tail_idx, bit_mask);
    if (idx >= palette_size) [[unlikely]] {
      return std::unexpected(ParseError{
          ParseError::Code::PaletteIndexOutOfRange, "BlockStates", 0});
    }
    out[tail_idx] = static_cast<std::uint16_t>(idx);
  }

  return {};
}

// Dispatch entry point. Validates inputs, allocates the output buffer, and
// calls the AVX2 or Highway kernel depending on the target selected by
// Highway's dynamic dispatch.
ParseResult<memory::UnInitBuffer<std::uint16_t>> UnpackIndicesFusedImpl(
    std::span<const std::byte> raw_longs,
    std::uint32_t bits_per_block,
    std::uint64_t volume,
    std::size_t palette_size) {
  const auto long_count = raw_longs.size() / 8;
  auto make_error = [](ParseError::Code code) {
    return ParseError{code, "BlockStates", 0};
  };

  if (volume == 0) {
    return memory::UnInitBuffer<std::uint16_t>{};
  }

  // Degenerate palette: every block is index 0. Skip the kernel entirely
  // and memset the output.
  if (palette_size < 2 || bits_per_block < 2) {
    if (palette_size != 1) {
      return std::unexpected(
          make_error(ParseError::Code::PaletteIndexOutOfRange));
    }
    memory::UnInitBuffer<std::uint16_t> out(static_cast<std::size_t>(volume));
    std::memset(out.data(),
                0,
                static_cast<std::size_t>(volume) * sizeof(std::uint16_t));
    return out;
  }

  const std::uint64_t min_longs = (volume * bits_per_block + 63) / 64;
  if (long_count < min_longs) {
    return std::unexpected(make_error(ParseError::Code::BlockStatesTooSmall));
  }
  if (palette_size == 0) {
    return std::unexpected(
        make_error(ParseError::Code::PaletteIndexOutOfRange));
  }

  const std::byte* raw_data = raw_longs.data();
  ParseResult<void> kernel_result{};
  // UnInitBuffer allocates uninitialized memory to skip zeroing for large
  // buffers, as the kernels will overwrite it completely.
  memory::UnInitBuffer<std::uint16_t> out(static_cast<std::size_t>(volume));

  // bpb is clamped to [2, 16] by BitsPerBlock. The SIMD paths require this
  // range: 4 lanes * 16 bpb = 64 bits, the width of one window. bpb > 16
  // would overflow the window.
  if (bits_per_block >= 2 && bits_per_block <= 16) {
#if HWY_TARGET == HWY_AVX2
    kernel_result = UnpackFusedKernelAvx2(
        raw_data, long_count, bits_per_block, volume, palette_size, out.data());
#else
    const hn::CappedTag<std::uint64_t, 4> d_tag;
    kernel_result = UnpackFusedKernelHwy(d_tag,
                                         raw_data,
                                         long_count,
                                         bits_per_block,
                                         volume,
                                         palette_size,
                                         out.data());
#endif
  } else {
    kernel_result =
        std::unexpected(make_error(ParseError::Code::PaletteIndexOutOfRange));
  }

  if (!kernel_result) return std::unexpected(kernel_result.error());
  return out;
}

}  // namespace HWY_NAMESPACE
}  // namespace fschema::litematic::internal
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace fschema::litematic::internal {
HWY_EXPORT(UnpackIndicesFusedImpl);

[[nodiscard]] ParseResult<memory::UnInitBuffer<std::uint16_t>>
UnpackIndicesFused(std::span<const std::byte> raw_longs,
                   std::uint32_t bits_per_block,
                   std::uint64_t volume,
                   std::size_t palette_size) {
  return HWY_DYNAMIC_DISPATCH(UnpackIndicesFusedImpl)(
      raw_longs, bits_per_block, volume, palette_size);
}

}  // namespace fschema::litematic::internal
#endif  // HWY_ONCE