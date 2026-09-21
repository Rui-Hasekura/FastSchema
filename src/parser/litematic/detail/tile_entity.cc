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

#include "parser/litematic/detail/tile_entity.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>

#include "parser/nbt/skip.h"
#include "parser/nbt/tag.h"

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
    nbt::ByteReader& reader) {
    TileEntity tile_entity;
    tile_entity.block_position = { 0, 0, 0 };

    reader.push_depth();

    for (;;) {
      std::string_view name;
      auto tag_result = reader.ReadCompoundEntryHeaderView(name);
      if (!tag_result) {
        reader.pop_depth();
        return std::unexpected(tag_result.error());
      }
      if (*tag_result == nbt::TagType::End) {
        break;
      }

      if ((name == "id" || name == "Id") &&
        *tag_result == nbt::TagType::String) {
        auto value = reader.ReadStringView();
        if (!value) { reader.pop_depth(); return std::unexpected(value.error()); }
        tile_entity.id = *value;
      }
      else if (name == "x" && *tag_result == nbt::TagType::Int) {
        auto value = reader.Read<std::int32_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        tile_entity.block_position[0] = *value;
      }
      else if (name == "y" && *tag_result == nbt::TagType::Int) {
        auto value = reader.Read<std::int32_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        tile_entity.block_position[1] = *value;
      }
      else if (name == "z" && *tag_result == nbt::TagType::Int) {
        auto value = reader.Read<std::int32_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        tile_entity.block_position[2] = *value;
      }
      else {
        auto skip_result = nbt::SkipPayload(reader, *tag_result);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
      }
    }

    reader.pop_depth();
    return tile_entity;
  }

  [[nodiscard]] ParseResult<void> ParseTileEntities(
    nbt::ByteReader& reader, std::vector<TileEntity>& tile_entities) {
    auto header = reader.ReadListHeader();
    if (!header) {
      return std::unexpected(header.error());
    }
    auto [elem_type, count] = *header;

    if (elem_type == nbt::TagType::End || count == 0) {
      return {};
    }
    if (elem_type != nbt::TagType::Compound) {
      return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
    }
    if (count > reader.limits().max_tile_entities) {
      return std::unexpected(reader.Error(ParseError::Code::OversizedPayload));
    }

    tile_entities.clear();
    tile_entities.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
      auto tile_entity = ParseTileEntityCompound(reader);
      if (!tile_entity) {
        return std::unexpected(tile_entity.error());
      }
      tile_entities.push_back(std::move(*tile_entity));
    }

    return {};
  }

}  // namespace fschema::parser::litematic::detail