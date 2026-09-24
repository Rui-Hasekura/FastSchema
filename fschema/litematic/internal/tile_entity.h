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

#ifndef FSCHEMA_LITEMATIC_INTERNAL_TILE_ENTITY_H_
#define FSCHEMA_LITEMATIC_INTERNAL_TILE_ENTITY_H_

#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/litematic/types.h"

namespace fschema::litematic::internal {

  // TileEntities: List<Compound>
  //
  // Per TileEntity Compound's common fields:
  //   Id: String         <- TileEntity id, e.g. "minecraft:chest"
  //   x: Int             <- Block position x (Java NBT naming convention)
  //   y: Int
  //   z: Int
  //   ...remaining fields -> raw_nbt (e.g., Items, Lock, CustomName, ...)
  [[nodiscard]] ParseResult<TileEntity> ParseTileEntityCompound(
    base::ByteReader& reader);

  [[nodiscard]] ParseResult<void> ParseTileEntities(
    base::ByteReader& reader, std::vector<TileEntity>& tile_entities);

}  // namespace fschema::litematic::internal

#endif  // FSCHEMA_LITEMATIC_INTERNAL_TILE_ENTITY_H_