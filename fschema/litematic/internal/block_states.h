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

#ifndef FSCHEMA_LITEMATIC_INTERNAL_BLOCK_STATES_H_
#define FSCHEMA_LITEMATIC_INTERNAL_BLOCK_STATES_H_

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "fschema/base/error.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/litematic/types.h"

namespace fschema::litematic::internal {

  // BlockStates: LongArray -> BitUnpack -> block_indices
  //
  //   bit_offset = i * bits_per_block
  //   word_idx   = bit_offset >> 6
  //   shift      = bit_offset & 63
  //   value      = (longs[word_idx] >> shift) | (longs[word_idx+1] << (64-shift))
  //   palette_idx = value & ((1 << bits_per_block) - 1)
  //
  // Fused kernel v4 (main path, simd_block_states.cc):
  //   bits_per_block in [2, 8]   -> 8 blocks/iter, double VPSRLVQ
  //   bits_per_block in [9, 16]  -> 4 blocks/iter, single VPSRLVQ
  //   (BitsPerBlock actual range is [2,16], scalar branch is purely defensive)
  // Structure: v2 sliding window (exactly one load+BSWAP per long)
  //            + branchless funnel + vmax range check outside loop.
  // Historical lesson (do not revert):
  //   v3 unconditional double load -> BSWAP x3.55 -> 350ms slower than v2.

  // palette -> bits_per_block
  // Litematica special case: palette <= 4 uses fixed 2 bits
  [[nodiscard]] constexpr std::uint32_t BitsPerBlock(
    std::size_t palette_size) noexcept {
    if (palette_size <= 4) {
      return 2;
    }
    return static_cast<std::uint32_t>(std::bit_width(palette_size - 1));
  }

  [[nodiscard]] ParseResult<memory::NoInitVector<std::uint16_t>> UnpackIndicesFused(
    std::span<const std::byte> raw_longs,
    std::uint32_t bits_per_block, std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena);

  [[nodiscard]] ParseResult<memory::NoInitVector<std::uint16_t>>
  UnpackIndicesHwy(
    std::span<const std::uint64_t> longs,
    std::uint32_t bits_per_block, std::uint64_t volume,
    std::size_t palette_size);

  [[nodiscard]] ParseResult<memory::NoInitVector<std::uint16_t>>
  UnpackIndicesScalar(
    std::span<const std::uint64_t> longs,
    std::uint32_t bits_per_block, std::uint64_t volume,
    std::size_t palette_size);

  [[nodiscard]] ParseResult<memory::NoInitVector<std::uint64_t>>
  ReadLongArrayBe(
    base::ByteReader& reader, std::uint64_t expected_longs);

}  // namespace fschema::litematic::internal

#endif  // FSCHEMA_LITEMATIC_INTERNAL_BLOCK_STATES_H_