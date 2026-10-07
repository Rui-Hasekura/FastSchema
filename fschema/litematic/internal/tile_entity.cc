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

#include "fschema/litematic/internal/tile_entity.h"

#include "fschema/base/nbt/scope.h"
#include "fschema/base/nbt/skip.h"
#include "fschema/base/nbt/tag.h"

namespace fschema::litematic::internal {

[[nodiscard]] ParseResult<TileEntity> ParseTileEntityCompound(
    base::ByteReader& reader) {
  TileEntity te;
  te.block_position = {0, 0, 0};

  const auto start = reader.pos();

  auto result = ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        if (name == "components" && t == base::TagType::Compound) {
          return ForEachCompoundField(
              reader,
              [&](std::string_view cn, base::TagType ct) -> ParseResult<void> {
                if ((cn == "id" || cn == "Id") && ct == base::TagType::String) {
                  auto v = reader.ReadStringView();
                  if (!v) return std::unexpected(v.error());
                  te.id = *v;
                } else if (cn == "x" && ct == base::TagType::Int) {
                  auto v = reader.Read<std::int32_t>();
                  if (!v) return std::unexpected(v.error());
                  te.block_position[0] = *v;
                } else if (cn == "y" && ct == base::TagType::Int) {
                  auto v = reader.Read<std::int32_t>();
                  if (!v) return std::unexpected(v.error());
                  te.block_position[1] = *v;
                } else if (cn == "z" && ct == base::TagType::Int) {
                  auto v = reader.Read<std::int32_t>();
                  if (!v) return std::unexpected(v.error());
                  te.block_position[2] = *v;
                } else {
                  return base::SkipPayload(reader, ct);
                }
                return {};
              });
        } else if ((name == "id" || name == "Id") &&
                   t == base::TagType::String) {
          auto v = reader.ReadStringView();
          if (!v) return std::unexpected(v.error());
          te.id = *v;
        } else if (name == "x" && t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
          te.block_position[0] = *v;
        } else if (name == "y" && t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
          te.block_position[1] = *v;
        } else if (name == "z" && t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
          te.block_position[2] = *v;
        } else {
          return base::SkipPayload(reader, t);
        }
        return {};
      });
  if (!result) return std::unexpected(result.error());

  te.raw_nbt = reader.SpanFrom(start);
  return te;
}

[[nodiscard]] ParseResult<void> ParseTileEntities(
    base::ByteReader& reader,
    std::vector<TileEntity>& tile_entities) {
  auto header = reader.ReadListHeader();
  if (!header) return std::unexpected(header.error());
  auto [elem_type, count] = *header;

  if (elem_type == base::TagType::End || count == 0) return {};
  if (elem_type != base::TagType::Compound)
    return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
  if (count > reader.limits().max_tile_entities)
    return std::unexpected(reader.Error(ParseError::Code::OversizedPayload));

  tile_entities.clear();
  tile_entities.reserve(count);

  for (std::size_t i = 0; i < count; ++i) {
    auto te = ParseTileEntityCompound(reader);
    if (!te) return std::unexpected(te.error());
    tile_entities.push_back(std::move(*te));
  }
  return {};
}

}  // namespace fschema::litematic::internal