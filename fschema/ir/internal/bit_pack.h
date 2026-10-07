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

#ifndef FSCHEMA_IR_INTERNAL_BIT_PACK_H_
#define FSCHEMA_IR_INTERNAL_BIT_PACK_H_

#include <cstddef>
#include <cstdint>
#include <span>

#include "fschema/memory/uninit_buffer.h"

namespace fschema::ir::internal {

// Number of int64 longs occupied by the litematica sliding bitstream:
// stream bit j lives at bit (j % 64) of long (j / 64), LSB-first; a block may
// straddle two longs (no per-long padding).
[[nodiscard]] constexpr std::size_t LitematicLongCount(
    std::size_t n,
    std::uint32_t bits_per_block) noexcept {
  if (bits_per_block == 0) return 0;
  return (n * bits_per_block + 63) / 64;
}

[[nodiscard]] std::size_t PackIndicesLitematicInto(
    std::byte* out,
    std::span<const std::uint16_t> indices,
    std::uint32_t bits_per_block);

}  // namespace fschema::ir::internal

#endif  // FSCHEMA_IR_INTERNAL_BIT_PACK_H_