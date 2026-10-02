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

#ifndef FSCHEMA_IR_INTERNAL_VARINT_PACK_H_
#define FSCHEMA_IR_INTERNAL_VARINT_PACK_H_

#include <cstddef>
#include <cstdint>
#include <span>

namespace fschema::ir::internal {

// Upper bound on the varint-encoded size of `n` palette indices.
// Width depends only on the palette size (indices are guaranteed < palette
// size by the decoder):
//   palette <= 0x80   -> 1 byte per index
//   palette <= 0x4000 -> 2 bytes per index
//   otherwise         -> 3 bytes per index (16-bit values)
[[nodiscard]] constexpr std::size_t VarintUpperBound(
    std::size_t n,
    std::size_t palette_size) noexcept {
  const std::size_t per =
      palette_size <= 0x80 ? 1 : (palette_size <= 0x4000 ? 2 : 3);
  return n * per;
}

// Packs palette indices as Sponge-style varints (7 bits per byte, low group
// first, MSB = continuation) directly into out.
// out MUST have room for VarintUpperBound(indices.size(), palette_size)
// bytes. out needs no alignment. Chunk-parallel (TBB) for large inputs.
// Returns the exact number of bytes written.
[[nodiscard]] std::size_t PackVarintInto(std::byte* out,
                                         std::span<const std::uint16_t> indices,
                                         std::size_t palette_size);

}  // namespace fschema::ir::internal

#endif  // FSCHEMA_IR_INTERNAL_VARINT_PACK_H_