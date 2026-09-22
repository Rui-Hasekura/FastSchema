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

#ifndef FSCHEMA_PARSER_SCHEM_DETAIL_BLOCK_DATA_H_
#define FSCHEMA_PARSER_SCHEM_DETAIL_BLOCK_DATA_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <vector>

#include "parser/arena.h"
#include "parser/error.h"
#include "parser/nbt/noinit_allocator.h"
#include "parser/nbt/reader.h"
#include "parser/schem/types.h"

namespace fschema::parser::schem::detail {

  // Varint decoders
  [[nodiscard]] ParseResult<NoInitVector<std::uint16_t>>
    DecodeVarintArray(
      std::span<const std::byte> data,
      std::uint64_t volume,
      std::size_t palette_size,
      Arena& arena);

  [[nodiscard]] ParseResult<NoInitVector<std::uint16_t>>
    DecodeVarintScalar(
      std::span<const std::byte> data,
      std::uint64_t volume,
      std::size_t palette_size,
      Arena& arena);

  [[nodiscard]] ParseResult<NoInitVector<std::uint16_t>>
    DecodeSingleByteFast(
      std::span<const std::byte> data,
      std::uint64_t volume,
      std::size_t palette_size,
      Arena& arena);

  // SIMD varint boundary counter (Phase 1 of parallel decode)
  [[nodiscard]] std::pair<std::vector<std::uint64_t>, std::size_t>
    BuildVarintCumulativeCount(std::span<const std::byte> data);

  // Parallel varint decoder (Phase 1 + 2 + 3)
  [[nodiscard]] ParseResult<NoInitVector<std::uint16_t>>
    DecodeVarintParallel(
      std::span<const std::byte> data,
      std::uint64_t volume,
      std::size_t palette_size,
      Arena& arena);

}  // namespace fschema::parser::schem::detail

#endif  // FSCHEMA_PARSER_SCHEM_DETAIL_BLOCK_DATA_H_