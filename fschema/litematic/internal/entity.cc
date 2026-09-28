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

#include "fschema/litematic/internal/entity.h"

#include <array>
#include <expected>
#include <utility>

#include "fschema/base/nbt_primitives.h"
#include "fschema/base/nbt_scope.h"
#include "fschema/base/nbt_skip.h"
#include "fschema/base/nbt_tag.h"

namespace fschema::litematic::internal {

[[nodiscard]] ParseResult<Entity> ParseEntityCompound(
    base::ByteReader& reader) {
  Entity entity;
  entity.position = {0, 0, 0};
  entity.motion = {0, 0, 0};
  entity.rotation = {0, 0};

  const auto start = reader.pos();

  auto result = ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        if ((name == "id" || name == "Id") && t == base::TagType::String) {
          auto v = reader.ReadStringView();
          if (!v) return std::unexpected(v.error());
          entity.id = *v;
        } else if (name == "Pos" && t == base::TagType::List) {
          auto r = base::ReadVec3DoubleList(reader);
          if (!r) return std::unexpected(r.error());
          entity.position = *r;
        } else if (name == "Motion" && t == base::TagType::List) {
          auto r = base::ReadVec3DoubleList(reader);
          if (!r) return std::unexpected(r.error());
          entity.motion = *r;
        } else if (name == "Rotation" && t == base::TagType::List) {
          auto r = base::ReadVec2FloatList(reader);
          if (!r) return std::unexpected(r.error());
          entity.rotation = *r;
        } else {
          return base::SkipPayload(reader, t);
        }
        return {};
      });
  if (!result) return std::unexpected(result.error());

  entity.raw_nbt = reader.SpanFrom(start);
  return entity;
}

[[nodiscard]] ParseResult<void> ParseEntities(base::ByteReader& reader,
                                              std::vector<Entity>& entities) {
  auto header = reader.ReadListHeader();
  if (!header) return std::unexpected(header.error());
  auto [elem_type, count] = *header;

  if (elem_type == base::TagType::End || count == 0) return {};
  if (elem_type != base::TagType::Compound)
    return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
  if (count > reader.limits().max_entities)
    return std::unexpected(reader.Error(ParseError::Code::OversizedPayload));

  entities.clear();
  entities.reserve(count);

  for (std::size_t i = 0; i < count; ++i) {
    auto entity = ParseEntityCompound(reader);
    if (!entity) return std::unexpected(entity.error());
    entities.push_back(std::move(*entity));
  }
  return {};
}

}  // namespace fschema::litematic::internal