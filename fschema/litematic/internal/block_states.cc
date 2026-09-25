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

#include "fschema/litematic/internal/block_states.h"

#include <cstdint>
#include <expected>
#include <span>
#include <utility>

#include "fschema/base/error.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/litematic/types.h"

namespace fschema::litematic::internal {

  std::expected<void, ParseError> ParseBlockStates(base::ByteReader& reader,
    Region& region) {
    const std::uint64_t volume = VolumeOf(region.size);
    const std::uint32_t bits_per_block = BitsPerBlock(region.palette.size());

    const std::uint64_t min_long_count = (volume * bits_per_block + 63) / 64;

    // Materialize longs FIRST
    auto long_array_result = ReadLongArrayBe(reader, min_long_count);
    if (!long_array_result) {
      return std::unexpected(long_array_result.error());
    }

    auto indices_result = UnpackIndicesHwy(*long_array_result, bits_per_block, volume,
      region.palette.size());
    if (!indices_result) {
      return std::unexpected(indices_result.error());
    }

    region.block_indices = std::move(*indices_result);
    return {};
  }

  // Scalar impl of Two step ver
  ParseResult<memory::NoInitVector<std::uint64_t>> ReadLongArrayBe(
    base::ByteReader& reader, std::uint64_t expected_longs) {
    // Read int32 length(elements)
    auto raw_len = reader.Read<std::int32_t>();
    if (!raw_len) {
      return std::unexpected(raw_len.error());
    }
    const auto length = static_cast<std::int64_t>(*raw_len);

    if (length < 0) {
      return std::unexpected(reader.Error(ParseError::Code::NegativeLength));
    }
    if (static_cast<std::uint64_t>(length) < expected_longs) {
      return std::unexpected(reader.Error(ParseError::Code::BlockStatesTooSmall));
    }
    if (static_cast<std::uint64_t>(length) > reader.limits().max_array_elements) {
      return std::unexpected(reader.Error(ParseError::Code::OversizedPayload));
    }

    memory::NoInitVector<std::uint64_t> result(static_cast<std::size_t>(length));
    auto bulk_result = reader.ReadBulk<std::uint64_t>(
      static_cast<std::size_t>(length),
      std::span<std::uint64_t>(result.data(), result.size()));
    if (!bulk_result) {
      return std::unexpected(bulk_result.error());
    }
    return result;
  }

  ParseResult<memory::NoInitVector<std::uint16_t>> UnpackIndicesScalar(
    std::span<const std::uint64_t> longs,
    std::uint32_t bits_per_block, std::uint64_t volume,
    std::size_t palette_size) {
    const std::uint64_t bit_mask = (1ULL << bits_per_block) - 1;
    memory::NoInitVector<std::uint16_t> indices(static_cast<std::size_t>(volume));

    for (std::uint64_t block_idx = 0; block_idx < volume; ++block_idx) {
      const std::uint64_t bit_offset = block_idx * bits_per_block;
      const auto word_index = static_cast<std::size_t>(bit_offset >> 6);
      const auto bit_shift = static_cast<std::uint32_t>(bit_offset & 63);

      std::uint64_t value = longs[word_index] >> bit_shift;
      // Have to splice when cross 64b border.
      if (bit_shift + bits_per_block > 64) {
        value |= longs[word_index + 1] << (64 - bit_shift);
      }
      const std::uint32_t index = static_cast<std::uint32_t>(value & bit_mask);
      if (index >= palette_size) [[unlikely]] {
        return std::unexpected(ParseError{
            ParseError::Code::PaletteIndexOutOfRange,
            "BlockStates", 0 });
      }
      indices[static_cast<std::size_t>(block_idx)] = static_cast<std::uint16_t>(index);
    }
    return indices;
  }

}  // namespace fschema::litematic::internal