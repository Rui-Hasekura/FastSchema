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

#include "fschema/schem/internal/block_entities.h"

#include <array>
#include <cstdint>
#include <expected>
#include <string_view>
#include <utility>

#include "fschema/base/error.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/base/nbt_skip.h"
#include "fschema/base/nbt_tag.h"
#include "fschema/schem/types.h"

namespace fschema::schem::internal {

  [[nodiscard]] ParseResult<BlockEntity>
    ParseBlockEntityCompound(
      base::ByteReader& reader, bool is_v3) {
    BlockEntity be;
    be.pos = { 0, 0, 0 };

    const auto start = reader.pos();

    reader.push_depth();

    for (;;) {
      std::string_view name;
      auto tag_result = reader.ReadCompoundEntryHeaderView(name);
      if (!tag_result) {
        reader.pop_depth();
        return std::unexpected(tag_result.error());
      }
      if (*tag_result == base::TagType::End) {
        break;
      }

      if (name == "Id" && *tag_result == base::TagType::String) {
        auto value = reader.ReadStringView();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        be.id = *value;
      }
      else if (name == "Pos" &&
        *tag_result == base::TagType::IntArray) {
        auto len = reader.ReadLength(3);
        if (!len) {
          reader.pop_depth();
          return std::unexpected(len.error());
        }
        if (*len != 3) [[unlikely]] {
          reader.pop_depth();
          return std::unexpected(
            reader.Error(ParseError::Code::InvalidTagId));
        }
        for (int i = 0; i < 3; ++i) {
          auto v = reader.Read<std::int32_t>();
          if (!v) {
            reader.pop_depth();
            return std::unexpected(v.error());
          }
          be.pos[i] = *v;
        }
      }
      else if (is_v3 && name == "Data" &&
        *tag_result == base::TagType::Compound) {
        const auto data_start = reader.pos();
        auto skip_result =
          base::SkipPayload(reader, base::TagType::Compound);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
        be.data = reader.SpanFrom(data_start);
      }
      else {
        auto skip_result = base::SkipPayload(reader, *tag_result);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
      }
    }

    reader.pop_depth();

    if (!is_v3) {
      be.data = reader.SpanFrom(start);
    }

    return be;
  }

  [[nodiscard]] ParseResult<void> ParseBlockEntities(
    base::ByteReader& reader,
    std::vector<BlockEntity>& out,
    bool is_v3) {
    auto header = reader.ReadListHeader();
    if (!header) {
      return std::unexpected(header.error());
    }
    auto [elem_type, count] = *header;

    if (elem_type == base::TagType::End || count == 0) {
      return {};
    }
    if (elem_type != base::TagType::Compound) {
      return std::unexpected(
        reader.Error(ParseError::Code::InvalidTagId));
    }
    if (count > reader.limits().max_tile_entities) {
      return std::unexpected(
        reader.Error(ParseError::Code::OversizedPayload));
    }

    out.clear();
    out.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
      auto be = ParseBlockEntityCompound(reader, is_v3);
      if (!be) {
        return std::unexpected(be.error());
      }
      out.push_back(std::move(*be));
    }

    return {};
  }

}  // namespace fschema::schem::internal