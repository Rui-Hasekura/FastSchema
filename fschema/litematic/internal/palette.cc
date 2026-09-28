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

#include "fschema/litematic/internal/palette.h"

#include <expected>
#include <string_view>
#include <utility>

#include "fschema/base/nbt_scope.h"
#include "fschema/base/nbt_skip.h"
#include "fschema/base/nbt_tag.h"
#include "fschema/litematic/types.h"

namespace fschema::litematic::internal {

[[nodiscard]] ParseResult<void> ParsePalette(base::ByteReader& reader,
                                             std::vector<BlockState>& palette) {
  auto header = reader.ReadListHeader();
  if (!header) return std::unexpected(header.error());
  auto [elem_type, count] = *header;

  if (elem_type == base::TagType::End || count == 0) return {};
  if (elem_type != base::TagType::Compound)
    return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
  if (count > reader.limits().max_palette_size)
    return std::unexpected(reader.Error(ParseError::Code::OversizedPayload));

  palette.clear();
  palette.reserve(count);

  // List scope (counts toward NBT depth).
  base::DepthGuard list_guard(reader);
  if (reader.depth() > reader.limits().max_nbt_depth) [[unlikely]] {
    return std::unexpected(reader.Error(ParseError::Code::DepthLimitExceeded));
  }

  for (std::size_t i = 0; i < count; ++i) {
    BlockState entry;
    bool have_name = false;

    auto result = ForEachCompoundField(
        reader,
        [&](std::string_view name, base::TagType t) -> ParseResult<void> {
          if (name == "Name" && t == base::TagType::String) {
            auto v = reader.ReadStringView();
            if (!v) return std::unexpected(v.error());
            entry.name = *v;
            have_name = true;
          } else if (name == "Properties" && t == base::TagType::Compound) {
            const auto start = reader.pos();
            auto s = base::SkipPayload(reader, base::TagType::Compound);
            if (!s) return std::unexpected(s.error());
            entry.properties = reader.SpanFrom(start);
          } else {
            return base::SkipPayload(reader, t);
          }
          return {};
        });
    if (!result) return std::unexpected(result.error());

    if (!have_name) [[unlikely]] {
      return std::unexpected(reader.Error(ParseError::Code::MissingField));
    }
    palette.push_back(std::move(entry));
  }
  return {};
}

}  // namespace fschema::litematic::internal