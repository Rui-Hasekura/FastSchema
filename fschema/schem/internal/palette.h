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

#ifndef FSCHEMA_SCHEM_INTERNAL_PALETTE_H_
#define FSCHEMA_SCHEM_INTERNAL_PALETTE_H_

#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/schem/types.h"

namespace fschema::schem::internal {

  // Parses a block palette Compound.
  // Keys are blockstate strings, values are Int indices.
  //   { "minecraft:air": 0, "minecraft:stone_button[face=floor]": 1, ... }
  // Indices may be non-contiguous; gaps are left as default BlockState.
  // Blockstate strings are parsed into name + properties.
  [[nodiscard]] ParseResult<void> ParseBlockPalette(
    base::ByteReader& reader,
    std::vector<BlockState>& palette);

  // Parses a biome palette Compound.
  // Keys are biome resource location strings, values are Int indices.
  [[nodiscard]] ParseResult<void> ParseBiomePalette(
    base::ByteReader& reader,
    std::vector<std::string_view>& palette);

}  // namespace fschema::schem::internal

#endif  // FSCHEMA_SCHEM_INTERNAL_PALETTE_H_