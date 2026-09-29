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

#include "fschema/ir/internal/bit_pack.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__AVX2__)
#  include <immintrin.h>
#endif

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

namespace fschema::ir::internal {

namespace {

constexpr std::size_t kMinBlocksForParallel = 1 << 18;  // 256K blocks

void PackBpb8Longs(const std::uint16_t* src,
                   std::size_t block_start,
                   std::size_t block_end,
                   std::uint64_t* dst) {
  std::size_t i = block_start;
  std::size_t j = block_start / 8;  // 8 blocks per long

#if defined(__AVX2__)
  // 32 blocks → 4 longs per iter
  for (; i + 32 <= block_end; i += 32, j += 4) {
    __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
    __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 8));
    __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 16));
    __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 24));
    __m128i ab = _mm_packus_epi16(a, b);  // [a0..a7 | b0..b7] = 2 longs
    __m128i cd = _mm_packus_epi16(c, d);  // [c0..c7 | d0..d7] = 2 longs
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + j), ab);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + j + 2), cd);
  }
  for (; i + 8 <= block_end; i += 8, ++j) {
    __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
    __m128i packed = _mm_packus_epi16(a, _mm_setzero_si128());
    _mm_storel_epi64(reinterpret_cast<__m128i*>(dst + j), packed);
  }
#else
  for (; i + 8 <= block_end; i += 8, ++j) {
    dst[j] = (static_cast<std::uint64_t>(src[i + 0] & 0xFF)) |
             (static_cast<std::uint64_t>(src[i + 1] & 0xFF) << 8) |
             (static_cast<std::uint64_t>(src[i + 2] & 0xFF) << 16) |
             (static_cast<std::uint64_t>(src[i + 3] & 0xFF) << 24) |
             (static_cast<std::uint64_t>(src[i + 4] & 0xFF) << 32) |
             (static_cast<std::uint64_t>(src[i + 5] & 0xFF) << 40) |
             (static_cast<std::uint64_t>(src[i + 6] & 0xFF) << 48) |
             (static_cast<std::uint64_t>(src[i + 7] & 0xFF) << 56);
  }
#endif
}

void PackBpb4Longs(const std::uint16_t* src,
                   std::size_t block_start,
                   std::size_t block_end,
                   std::uint64_t* dst) {
  std::size_t i = block_start;
  std::size_t j = block_start / 16;  // 16 blocks per long

  for (; i + 16 <= block_end; i += 16, ++j) {
    dst[j] = (static_cast<std::uint64_t>(src[i + 0] & 0xF)) |
             (static_cast<std::uint64_t>(src[i + 1] & 0xF) << 4) |
             (static_cast<std::uint64_t>(src[i + 2] & 0xF) << 8) |
             (static_cast<std::uint64_t>(src[i + 3] & 0xF) << 12) |
             (static_cast<std::uint64_t>(src[i + 4] & 0xF) << 16) |
             (static_cast<std::uint64_t>(src[i + 5] & 0xF) << 20) |
             (static_cast<std::uint64_t>(src[i + 6] & 0xF) << 24) |
             (static_cast<std::uint64_t>(src[i + 7] & 0xF) << 28) |
             (static_cast<std::uint64_t>(src[i + 8] & 0xF) << 32) |
             (static_cast<std::uint64_t>(src[i + 9] & 0xF) << 36) |
             (static_cast<std::uint64_t>(src[i + 10] & 0xF) << 40) |
             (static_cast<std::uint64_t>(src[i + 11] & 0xF) << 44) |
             (static_cast<std::uint64_t>(src[i + 12] & 0xF) << 48) |
             (static_cast<std::uint64_t>(src[i + 13] & 0xF) << 52) |
             (static_cast<std::uint64_t>(src[i + 14] & 0xF) << 56) |
             (static_cast<std::uint64_t>(src[i + 15] & 0xF) << 60);
  }
}

void PackBpb2Longs(const std::uint16_t* src,
                   std::size_t block_start,
                   std::size_t block_end,
                   std::uint64_t* dst) {
  std::size_t i = block_start;
  std::size_t j = block_start / 32;  // 32 blocks per long

  for (; i + 32 <= block_end; i += 32, ++j) {
    std::uint64_t acc = 0;
#pragma unroll
    for (int k = 0; k < 32; ++k) {
      acc |= static_cast<std::uint64_t>(src[i + k] & 0x3) << (k * 2);
    }
    dst[j] = acc;
  }
}

void PackBpb16Longs(const std::uint16_t* src,
                    std::size_t block_start,
                    std::size_t block_end,
                    std::uint64_t* dst) {
  std::size_t i = block_start;
  std::size_t j = block_start / 4;  // 4 blocks per long

  for (; i + 4 <= block_end; i += 4, ++j) {
    dst[j] = (static_cast<std::uint64_t>(src[i + 0])) |
             (static_cast<std::uint64_t>(src[i + 1]) << 16) |
             (static_cast<std::uint64_t>(src[i + 2]) << 32) |
             (static_cast<std::uint64_t>(src[i + 3]) << 48);
  }
}

void PackAlignedLongs(const std::uint16_t* src,
                      std::size_t block_start,
                      std::size_t block_end,
                      std::uint64_t* dst,
                      std::uint32_t bpb) {
  switch (bpb) {
    case 8:
      PackBpb8Longs(src, block_start, block_end, dst);
      break;
    case 4:
      PackBpb4Longs(src, block_start, block_end, dst);
      break;
    case 2:
      PackBpb2Longs(src, block_start, block_end, dst);
      break;
    case 16:
      PackBpb16Longs(src, block_start, block_end, dst);
      break;
    default:
      break;  // unreachable for aligned bpb ∈ [2, 16]
  }
}

}  // namespace

memory::NoInitVector<std::uint64_t> PackIndicesLitematic(
    std::span<const std::uint16_t> indices,
    std::uint32_t bits_per_block) {
  if (indices.empty() || bits_per_block == 0) return {};

  const std::uint64_t bit_mask = (1ULL << bits_per_block) - 1;
  const std::uint64_t total_bits = indices.size() * bits_per_block;
  const std::size_t long_count = (total_bits + 63) / 64;

  memory::NoInitVector<std::uint64_t> longs(long_count);
  std::memset(longs.data(), 0, long_count * 8);

  const std::size_t n = indices.size();
  const std::uint16_t* src = indices.data();
  std::uint64_t* dst = longs.data();
  const bool is_aligned = (64 % bits_per_block == 0) && (bits_per_block >= 2) &&
                          (bits_per_block <= 16);

  if (is_aligned) {
    const std::size_t blocks_per_long = 64 / bits_per_block;
    const std::size_t complete_blocks = (n / blocks_per_long) * blocks_per_long;

    if (n >= kMinBlocksForParallel && complete_blocks > 0) {
      constexpr std::size_t kTargetChunkBlocks = 1 << 15;
      const std::size_t chunk_blocks =
          (kTargetChunkBlocks / blocks_per_long) * blocks_per_long;
      const std::size_t num_chunks =
          (complete_blocks + chunk_blocks - 1) / chunk_blocks;

      auto pack_range = [&](const tbb::blocked_range<std::size_t>& range) {
        for (std::size_t c = range.begin(); c < range.end(); ++c) {
          const std::size_t start = c * chunk_blocks;
          const std::size_t end =
              std::min(start + chunk_blocks, complete_blocks);
          PackAlignedLongs(src, start, end, dst, bits_per_block);
        }
      };

      tbb::parallel_for(tbb::blocked_range<std::size_t>(0, num_chunks),
                        pack_range,
                        tbb::static_partitioner{});
    } else {
      PackAlignedLongs(src, 0, complete_blocks, dst, bits_per_block);
    }

    {
      std::uint64_t bit_offset = complete_blocks * bits_per_block;
      for (std::size_t i = complete_blocks; i < n; ++i) {
        const std::uint64_t idx = src[i] & bit_mask;
        const std::size_t word_idx = bit_offset / 64;
        const std::uint32_t bit_shift =
            static_cast<std::uint32_t>(bit_offset % 64);
        dst[word_idx] |= (idx << bit_shift);
        if (bit_shift + bits_per_block > 64) [[unlikely]] {
          dst[word_idx + 1] |= (idx >> (64 - bit_shift));
        }
        bit_offset += bits_per_block;
      }
    }

    return longs;
  }

  for (std::size_t i = 0; i < n; ++i) {
    const std::uint64_t idx = src[i] & bit_mask;
    const std::uint64_t bit_offset = i * bits_per_block;
    const std::size_t word_idx = bit_offset / 64;
    const std::uint32_t bit_shift = static_cast<std::uint32_t>(bit_offset % 64);
    dst[word_idx] |= (idx << bit_shift);
    if (bit_shift + bits_per_block > 64) {
      dst[word_idx + 1] |= (idx >> (64 - bit_shift));
    }
  }
  return longs;
}

}  // namespace fschema::ir::internal