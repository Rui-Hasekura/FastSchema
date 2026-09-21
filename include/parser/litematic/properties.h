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

#ifndef FSCHEMA_PARSER_LITEMATIC_PROPERTIES_H_
#define FSCHEMA_PARSER_LITEMATIC_PROPERTIES_H_

#include <expected>
#include <span>
#include <string>
#include <string_view>

#include "parser/error.h"
#include "parser/limits.h"
#include "parser/nbt/reader.h"

namespace fschema::parser::litematic {

  template <typename F>
  [[nodiscard]] ParseResult<void> ForEachProperty(
    std::span<const std::byte> properties,
    const DecodeLimits& limits,
    F&& callback) {
    if (properties.empty()) {
      return {};
    }
    nbt::ByteReader reader(properties, limits);
    reader.push_depth();
    for (;;) {
      std::string_view key;
      auto tag_result = reader.ReadCompoundEntryHeaderView(key);
      if (!tag_result) {
        reader.pop_depth();
        return std::unexpected(tag_result.error());
      }
      if (*tag_result == nbt::TagType::End) {
        break;
      }
      if (*tag_result != nbt::TagType::String) {
        reader.pop_depth();
        return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
      }
      auto value_result = reader.ReadStringView();
      if (!value_result) {
        reader.pop_depth();
        return std::unexpected(value_result.error());
      }
      callback(key, *value_result);
    }
    reader.pop_depth();
    return {};
  }

}  // namespace fschema::parser::litematic

#endif  // FSCHEMA_PARSER_LITEMATIC_PROPERTIES_H_