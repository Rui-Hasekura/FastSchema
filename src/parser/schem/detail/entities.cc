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

#include "parser/schem/detail/entities.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <utility>

#include "parser/error.h"
#include "parser/nbt/reader.h"
#include "parser/nbt/skip.h"
#include "parser/nbt/tag.h"
#include "parser/schem/types.h"

namespace fschema::parser::schem::detail {

  [[nodiscard]] static ParseResult<std::array<double, 3>>
    ReadPos3Double(nbt::ByteReader& reader) {
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
      return std::unexpected(
        reader.Error(ParseError::Code::InvalidTagId));
    }
    if (count != 3) {
      for (std::size_t i = 0; i < count; ++i) {
        auto skip_result =
          nbt::SkipPayload(reader, nbt::TagType::Double);
        if (!skip_result) {
          return std::unexpected(skip_result.error());
        }
      }
      return result;
    }

    for (int i = 0; i < 3; ++i) {
      auto v = reader.Read<double>();
      if (!v) {
        return std::unexpected(v.error());
      }
      result[i] = *v;
    }
    return result;
  }

  [[nodiscard]] ParseResult<Entity>
    ParseEntityCompound(
      nbt::ByteReader& reader, bool is_v3) {
    Entity ent;
    ent.pos = { 0, 0, 0 };

    const auto start = reader.pos();

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

      if (name == "Id" && *tag_result == nbt::TagType::String) {
        auto value = reader.ReadStringView();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        ent.id = *value;
      }
      else if (name == "Pos" &&
        *tag_result == nbt::TagType::List) {
        auto pos_result = ReadPos3Double(reader);
        if (!pos_result) {
          reader.pop_depth();
          return std::unexpected(pos_result.error());
        }
        ent.pos = *pos_result;
      }
      else if (is_v3 && name == "Data" &&
        *tag_result == nbt::TagType::Compound) {
        const auto data_start = reader.pos();
        auto skip_result =
          nbt::SkipPayload(reader, nbt::TagType::Compound);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
        ent.data = reader.SpanFrom(data_start);
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

    if (!is_v3) {
      ent.data = reader.SpanFrom(start);
    }

    return ent;
  }

  [[nodiscard]] ParseResult<void> ParseEntities(
    nbt::ByteReader& reader,
    std::vector<Entity>& out,
    bool is_v3) {
    auto header = reader.ReadListHeader();
    if (!header) {
      return std::unexpected(header.error());
    }
    auto [elem_type, count] = *header;

    if (elem_type == nbt::TagType::End || count == 0) {
      return {};
    }
    if (elem_type != nbt::TagType::Compound) {
      return std::unexpected(
        reader.Error(ParseError::Code::InvalidTagId));
    }
    if (count > reader.limits().max_entities) {
      return std::unexpected(
        reader.Error(ParseError::Code::OversizedPayload));
    }

    out.clear();
    out.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
      auto ent = ParseEntityCompound(reader, is_v3);
      if (!ent) {
        return std::unexpected(ent.error());
      }
      out.push_back(std::move(*ent));
    }

    return {};
  }

}  // namespace fschema::parser::schem::detail