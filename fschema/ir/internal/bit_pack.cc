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
#include <cstring>

namespace fschema::ir::internal {

memory::NoInitVector<std::uint64_t> PackIndicesLitematic(
    std::span<const std::uint16_t> indices,
    std::uint32_t bits_per_block) {
  if (indices.empty() || bits_per_block == 0) return {};

  const std::uint64_t bit_mask = (1ULL << bits_per_block) - 1;
  const std::uint64_t total_bits = indices.size() * bits_per_block;
  const std::size_t long_count = (total_bits + 63) / 64;

  memory::NoInitVector<std::uint64_t> longs(long_count);
  // Must zero-initialize because we use bitwise OR
  std::memset(longs.data(), 0, long_count * 8);

  for (std::size_t i = 0; i < indices.size(); ++i) {
    const std::uint64_t idx = indices[i] & bit_mask;
    const std::uint64_t bit_offset = i * bits_per_block;
    const std::size_t word_idx = bit_offset / 64;
    const std::uint32_t bit_shift = bit_offset % 64;

    longs[word_idx] |= (idx << bit_shift);

    // Handle 64-bit boundary crossing
    if (bit_shift + bits_per_block > 64) {
      longs[word_idx + 1] |= (idx >> (64 - bit_shift));
    }
  }
  return longs;
}

}  // namespace fschema::ir::internal