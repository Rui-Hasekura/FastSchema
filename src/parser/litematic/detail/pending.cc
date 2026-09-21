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

#include "parser/litematic/detail/pending.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>

#include "parser/nbt/skip.h"
#include "parser/nbt/tag.h"

namespace fschema::parser::litematic::detail {

  [[nodiscard]] ParseResult<void> ParsePendingTicks(
    nbt::ByteReader& reader, std::vector<PendingTick>& out) {
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
    if (count > reader.limits().max_pending_ticks) {
      return std::unexpected(reader.Error(ParseError::Code::OversizedPayload));
    }

    out.clear();
    out.reserve(count);

    reader.push_depth();
    for (std::size_t i = 0; i < count; ++i) {
      PendingTick tick;
      bool have_block = false;

      for (;;) {
        std::string_view field_name;
        auto tag_result = reader.ReadCompoundEntryHeaderView(field_name);
        if (!tag_result) {
          reader.pop_depth();
          return std::unexpected(tag_result.error());
        }
        if (*tag_result == nbt::TagType::End) {
          break;
        }

        if (field_name == "Block" && *tag_result == nbt::TagType::String) {
          auto value = reader.ReadStringView();
          if (!value) {
            reader.pop_depth();
            return std::unexpected(value.error());
          }
          tick.block = *value;
          have_block = true;
        }
        else if (field_name == "SubTick" && *tag_result == nbt::TagType::Long) {
          auto value = reader.Read<std::int64_t>();
          if (!value) {
            reader.pop_depth();
            return std::unexpected(value.error());
          }
          tick.sub_tick = *value;
        }
        else if (field_name == "Priority" && *tag_result == nbt::TagType::Int) {
          auto value = reader.Read<std::int32_t>();
          if (!value) {
            reader.pop_depth();
            return std::unexpected(value.error());
          }
          tick.priority = *value;
        }
        else if (field_name == "Time" && *tag_result == nbt::TagType::Int) {
          auto value = reader.Read<std::int32_t>();
          if (!value) {
            reader.pop_depth();
            return std::unexpected(value.error());
          }
          tick.time = *value;
        }
        else if (field_name == "x" && *tag_result == nbt::TagType::Int) {
          auto value = reader.Read<std::int32_t>();
          if (!value) {
            reader.pop_depth();
            return std::unexpected(value.error());
          }
          tick.pos[0] = *value;
        }
        else if (field_name == "y" && *tag_result == nbt::TagType::Int) {
          auto value = reader.Read<std::int32_t>();
          if (!value) {
            reader.pop_depth();
            return std::unexpected(value.error());
          }
          tick.pos[1] = *value;
        }
        else if (field_name == "z" && *tag_result == nbt::TagType::Int) {
          auto value = reader.Read<std::int32_t>();
          if (!value) {
            reader.pop_depth();
            return std::unexpected(value.error());
          }
          tick.pos[2] = *value;
        }
        else {
          auto skip_result = nbt::SkipPayload(reader, *tag_result);
          if (!skip_result) {
            reader.pop_depth();
            return std::unexpected(skip_result.error());
          }
        }
      }

      if (!have_block) {
        reader.pop_depth();
        return std::unexpected(reader.Error(ParseError::Code::MissingField));
      }
      out.push_back(std::move(tick));
    }
    reader.pop_depth();
    return {};
  }

}  // namespace fschema::parser::litematic::detail