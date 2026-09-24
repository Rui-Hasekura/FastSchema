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

#ifndef FSCHEMA_SCHEM_INTERNAL_BLOCK_DATA_H_
#define FSCHEMA_SCHEM_INTERNAL_BLOCK_DATA_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "fschema/memory/arena.h"
#include "fschema/base/error.h"
#include "fschema/schem/types.h"

namespace fschema::schem::internal {

  // Varint decoders
  [[nodiscard]] ParseResult<NoInitVector<std::uint16_t>>
    DecodeVarintArray(
      std::span<const std::byte> data,
      std::uint64_t volume,
      std::size_t palette_size,
      memory::Arena& arena);

  [[nodiscard]] ParseResult<NoInitVector<std::uint16_t>>
    DecodeVarintScalar(
      std::span<const std::byte> data,
      std::uint64_t volume,
      std::size_t palette_size,
      memory::Arena& arena);

  [[nodiscard]] ParseResult<NoInitVector<std::uint16_t>>
    DecodeSingleByteFast(
      std::span<const std::byte> data,
      std::uint64_t volume,
      std::size_t palette_size,
      memory::Arena& arena);

  // SIMD varint boundary counter (Phase 1 of parallel decode)
  [[nodiscard]] std::pair<std::vector<std::uint64_t>, std::size_t>
    BuildVarintCumulativeCount(std::span<const std::byte> data);

  // Parallel varint decoder (Phase 1 + 2 + 3)
  [[nodiscard]] ParseResult<NoInitVector<std::uint16_t>>
    DecodeVarintParallel(
      std::span<const std::byte> data,
      std::uint64_t volume,
      std::size_t palette_size,
      memory::Arena& arena);

  // Dispatched chunk-decode kernel wrapper (impl in simd_block_data.cc).
  // Decodes all varints across pre-computed chunks using the best available
  // SIMD target. Thread-safe: internal TBB parallel_for.
  void DecodeVarintChunks(const std::uint8_t* const* chunk_ptrs,
                          const std::size_t* chunk_offsets,
                          const std::size_t* chunk_counts,
                          std::size_t num_chunks,
                          std::uint16_t* out,
                          const std::uint8_t* data_end,
                          std::size_t palette_size,
                          std::atomic<std::uint32_t>& error_flag);

}  // namespace fschema::schem::internal

#endif  // FSCHEMA_SCHEM_INTERNAL_BLOCK_DATA_H_