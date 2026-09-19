/*
 * Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FSCHEMA_PARSER_LITEMATIC_DETAIL_BLOCK_STATES_H_
#define FSCHEMA_PARSER_LITEMATIC_DETAIL_BLOCK_STATES_H_

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "parser/error.h"
#include "parser/litematic/types.h"
#include "parser/nbt/reader.h"

namespace fschema::parser::litematic::detail {

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

  // Main path: fused (BE byteswap + bit unpack, single pass, zero intermediate array)
  // raw_longs points directly to the LongArray payload inside the input buffer.
  [[nodiscard]] ParseResult<NoInitVector<std::uint32_t>> UnpackIndicesFused(
    std::span<const std::byte> raw_longs,
    std::uint32_t bits_per_block, std::uint64_t volume,
    std::size_t palette_size);

  // Baseline: two-stage (materialize host-order longs first, then unpack)
  [[nodiscard]] ParseResult<NoInitVector<std::uint32_t>> UnpackIndicesHwy(
    std::span<const std::uint64_t> longs,
    std::uint32_t bits_per_block, std::uint64_t volume,
    std::size_t palette_size);

  // Two-stage support (implemented in block_states.cc, used by baseline and unit tests)
  [[nodiscard]] ParseResult<NoInitVector<std::uint64_t>> ReadLongArrayBe(
    nbt::ByteReader& reader, std::uint64_t expected_longs);

  [[nodiscard]] ParseResult<NoInitVector<std::uint32_t>> UnpackIndicesScalar(
    std::span<const std::uint64_t> longs,
    std::uint32_t bits_per_block, std::uint64_t volume,
    std::size_t palette_size);

} // namespace fschema::parser::litematic::detail

#endif // FSCHEMA_PARSER_LITEMATIC_DETAIL_BLOCK_STATES_H_