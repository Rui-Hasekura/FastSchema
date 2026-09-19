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

#include "parser/litematic/detail/palette.h"

#include <cstddef>
#include <expected>
#include <string>
#include <utility>

#include "parser/nbt/skip.h"
#include "parser/nbt/tag.h"

namespace fschema::parser::litematic::detail {

  // BlockStatePalette: List<Compound>
  //   PER Compound: {
  //     Name: String          <- Necessary, e.g. "minecraft:oak_stairs"
  //     Properties: Compound  <- Optional, arbitrary key-value pairs
  //   }
  //
  // Properties reserves raw span (Open schema, it's impossible to enumerate all keys).
  [[nodiscard]] ParseResult<void> ParsePalette(
    nbt::ByteReader& reader, std::vector<BlockState>& palette) {
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
    if (count > reader.limits().max_palette_size) {
      return std::unexpected(reader.Error(ParseError::Code::OversizedPayload));
    }

    palette.clear();
    palette.reserve(count);

    reader.push_depth();

    for (std::size_t i = 0; i < count; ++i) {
      BlockState entry;
      bool have_name = false;

      reader.push_depth();
      for (;;) {
        std::string name;
        auto tag_result = reader.ReadCompoundEntryHeader(name);
        if (!tag_result) {
          reader.pop_depth();
          reader.pop_depth();
          return std::unexpected(tag_result.error());
        }
        if (*tag_result == nbt::TagType::End) {
          break;
        }

        if (name == "Name" && *tag_result == nbt::TagType::String) {
          auto value = reader.ReadString();
          if (!value) {
            reader.pop_depth();
            reader.pop_depth();
            return std::unexpected(value.error());
          }
          entry.name = std::move(*value);
          have_name = true;
        }
        else if (name == "Properties" && *tag_result == nbt::TagType::Compound) {
          const auto start = reader.pos();
          auto skip_result = nbt::SkipPayload(reader, nbt::TagType::Compound);
          if (!skip_result) {
            reader.pop_depth();
            reader.pop_depth();
            return std::unexpected(skip_result.error());
          }
          entry.properties = reader.SpanFrom(start);
        }
        else {
          auto skip_result = nbt::SkipPayload(reader, *tag_result);
          if (!skip_result) {
            reader.pop_depth();
            reader.pop_depth();
            return std::unexpected(skip_result.error());
          }
        }
      }
      reader.pop_depth();

      if (!have_name) {
        reader.pop_depth();
        return std::unexpected(
          reader.Error(ParseError::Code::MissingField));
      }
      palette.push_back(std::move(entry));
    }

    reader.pop_depth();
    return {};
  }

} // namespace fschema::parser::litematic::detail