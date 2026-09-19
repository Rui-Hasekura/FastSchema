/*
 * Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FSCHEMA_PARSER_LITEMATIC_DETAIL_TILE_ENTITY_H_
#define FSCHEMA_PARSER_LITEMATIC_DETAIL_TILE_ENTITY_H_

#include <expected>
#include <memory>
#include <vector>

#include "parser/error.h"
#include "parser/litematic/types.h"
#include "parser/nbt/reader.h"

namespace fschema::parser::litematic::detail {

  // TileEntities: List<Compound>
  //
  // Per TileEntity Compound's common fields:
  //   Id: String         <- TileEntity id, e.g. "minecraft:chest"
  //   x: Int             <- Block position x (Java NBT naming convention)
  //   y: Int
  //   z: Int
  //   ...remaining fields -> raw_nbt (e.g., Items, Lock, CustomName, ...)
  [[nodiscard]] ParseResult<TileEntity> ParseTileEntityCompound(
    nbt::ByteReader& reader);

  [[nodiscard]] ParseResult<void> ParseTileEntities(
    nbt::ByteReader& reader, std::vector<TileEntity>& tile_entities,
    std::unique_ptr<std::vector<std::byte>>& /*owner*/);

} // namespace fschema::parser::litematic::detail

#endif // FSCHEMA_PARSER_LITEMATIC_DETAIL_TILE_ENTITY_H_