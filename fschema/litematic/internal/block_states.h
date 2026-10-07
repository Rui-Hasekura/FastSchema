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
#include "fschema/base/nbt/reader.h"
#include "fschema/memory/noinit_allocator.h"

namespace fschema::litematic::internal {

[[nodiscard]] constexpr std::uint32_t BitsPerBlock(
    std::size_t palette_size) noexcept {
  if (palette_size <= 4) {
    return 2;
  }
  return static_cast<std::uint32_t>(std::bit_width(palette_size - 1));
}

[[nodiscard]] ParseResult<memory::NoInitVector<std::uint16_t>>
UnpackIndicesFused(std::span<const std::byte> raw_longs,
                   std::uint32_t bits_per_block,
                   std::uint64_t volume,
                   std::size_t palette_size);

}  // namespace fschema::litematic::internal

#endif  // FSCHEMA_LITEMATIC_INTERNAL_BLOCK_STATES_H_