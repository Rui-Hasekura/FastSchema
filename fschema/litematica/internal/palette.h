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

#ifndef FSCHEMA_LITEMATICA_INTERNAL_PALETTE_H_
#define FSCHEMA_LITEMATICA_INTERNAL_PALETTE_H_

#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/nbt/reader.h"
#include "fschema/litematica/types.h"

namespace fschema::litematica::internal {

// BlockStatePalette: List<Compound>
//   PER Compound: {
//     Name: String          <- Necessary, e.g. "minecraft:oak_stairs"
//     Properties: Compound  <- Optional, arbitrary key-value pairs
//   }
// Properties reserves raw span (Open schema, it's impossible to enumerate all
// keys).
[[nodiscard]] ParseResult<void> ParsePalette(
    base::ByteReader& reader,
    std::vector<fschema::litematica::BlockState>& palette);

}  // namespace fschema::litematica::internal

#endif  // FSCHEMA_LITEMATICA_INTERNAL_PALETTE_H_