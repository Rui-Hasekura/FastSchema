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

#include "parser/litematic/detail/entity.h"

#include <cstddef>
#include <expected>
#include <string>
#include <utility>

#include "parser/nbt/skip.h"

namespace fschema::parser::litematic::detail {

  // Read 3 doubles from List<Double> (Pos/Motion)
  [[nodiscard]] ParseResult<std::array<double, 3>>
    ReadVec3Double(nbt::ByteReader& reader) {
    auto header = reader.ReadListHeader();
    if (!header) {
      return std::unexpected(header.error());
    }
    auto [elem_type, count] = *header;

    std::array<double, 3> result = { 0, 0, 0 };
    if (elem_type == nbt::TagType::End || count == 0) {
      return result;
    }

    if (elem_type != nbt::TagType::Double) {
      return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
    }
    if (count != 3) {
      // Validation/Corruption: length != 3, treat as 0 and skip
      for (std::size_t i = 0; i < count; ++i) {
        auto skip_result = nbt::SkipPayload(reader, nbt::TagType::Double);
        if (!skip_result) {
          return std::unexpected(skip_result.error());
        }
      }
      return result;
    }

    for (std::size_t i = 0; i < 3; ++i) {
      auto value = reader.Read<double>();
      if (!value) {
        return std::unexpected(value.error());
      }
      result[i] = *value;
    }
    return result;
  }

  // Read 2 floats from List<Float> (Rotation: yaw, pitch)
  [[nodiscard]] ParseResult<std::array<float, 2>>
    ReadVec2Float(nbt::ByteReader& reader) {
    auto header = reader.ReadListHeader();
    if (!header) {
      return std::unexpected(header.error());
    }
    auto [elem_type, count] = *header;

    std::array<float, 2> result = { 0, 0 };
    if (elem_type == nbt::TagType::End || count == 0) {
      return result;
    }

    if (elem_type != nbt::TagType::Float) {
      return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
    }
    if (count != 2) {
      for (std::size_t i = 0; i < count; ++i) {
        auto skip_result = nbt::SkipPayload(reader, nbt::TagType::Float);
        if (!skip_result) {
          return std::unexpected(skip_result.error());
        }
      }
      return result;
    }

    for (std::size_t i = 0; i < 2; ++i) {
      auto value = reader.Read<float>();
      if (!value) {
        return std::unexpected(value.error());
      }
      result[i] = *value;
    }
    return result;
  }

  // Parse single Entity Compound
  [[nodiscard]] ParseResult<Entity> ParseEntityCompound(
    nbt::ByteReader& reader) {
    Entity entity;
    entity.position = { 0, 0, 0 };
    entity.motion = { 0, 0, 0 };
    entity.rotation = { 0, 0 };

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

      if ((name == "id" || name == "Id") && *tag_result == nbt::TagType::String) {
        auto value = reader.ReadStringView();
        if (!value) { reader.pop_depth(); return std::unexpected(value.error()); }
        entity.id = *value;
      }
      else if (name == "Pos" && *tag_result == nbt::TagType::List) {
        auto pos_result = ReadVec3Double(reader);
        if (!pos_result) {
          reader.pop_depth();
          return std::unexpected(pos_result.error());
        }
        entity.position = *pos_result;
      }
      else if (name == "Motion" && *tag_result == nbt::TagType::List) {
        auto motion_result = ReadVec3Double(reader);
        if (!motion_result) {
          reader.pop_depth();
          return std::unexpected(motion_result.error());
        }
        entity.motion = *motion_result;
      }
      else if (name == "Rotation" && *tag_result == nbt::TagType::List) {
        auto rot_result = ReadVec2Float(reader);
        if (!rot_result) {
          reader.pop_depth();
          return std::unexpected(rot_result.error());
        }
        entity.rotation = *rot_result;
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
    return entity;
  }

  // Parse Entities List
  [[nodiscard]] ParseResult<void> ParseEntities(
    nbt::ByteReader& reader, std::vector<Entity>& entities) {
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
    if (count > reader.limits().max_entities) {
      return std::unexpected(reader.Error(ParseError::Code::OversizedPayload));
    }

    entities.clear();
    entities.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
      auto entity = ParseEntityCompound(reader);
      if (!entity) {
        return std::unexpected(entity.error());
      }
      entities.push_back(std::move(*entity));
    }

    return {};
  }

}  // namespace fschema::parser::litematic::detail