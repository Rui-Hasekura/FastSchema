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

#include "fschema/litematic/internal/root.h"

#include <cstdint>
#include <expected>
#include <string_view>
#include <utility>

#include "fschema/base/nbt_scope.h"
#include "fschema/base/nbt_skip.h"
#include "fschema/base/nbt_tag.h"
#include "fschema/litematic/internal/metadata.h"
#include "fschema/litematic/internal/region.h"

namespace fschema::litematic::internal {

[[nodiscard]] ParseResult<void> ParseRoot(base::ByteReader& reader,
                                          Litematic& out) {
  {
    auto root_tag = reader.Read<std::uint8_t>();
    if (!root_tag) return std::unexpected(root_tag.error());
    if (*root_tag != static_cast<std::uint8_t>(base::TagType::Compound)) {
      return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
    }
    auto root_name = reader.ReadStringView();
    if (!root_name) return std::unexpected(root_name.error());
    (void)root_name;
  }

  bool have_version = false;
  bool have_metadata = false;
  bool have_regions = false;
  bool have_data_version = false;

  auto result = ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        if (name == "Version" && t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
          if (*v < 5 || *v > 7) {
            return std::unexpected(
                reader.Error(ParseError::Code::UnsupportedVersion));
          }
          out.version = static_cast<Version>(*v);
          have_version = true;
        } else if ((name == "DataVersion" || name == "MinecraftDataVersion") &&
                   t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
          out.data_version = *v;
          have_data_version = true;
        } else if (name == "SubVersion" && t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
        } else if (name == "Metadata" && t == base::TagType::Compound) {
          auto r = ParseMetadata(reader, out);
          if (!r) return std::unexpected(r.error());
          have_metadata = true;
        } else if (name == "Regions" && t == base::TagType::Compound) {
          auto r = ParseRegions(reader, out);
          if (!r) return std::unexpected(r.error());
          have_regions = true;
        } else {
          return base::SkipPayload(reader, t);
        }
        return {};
      });
  if (!result) return std::unexpected(result.error());

  if (!have_version) [[unlikely]] {
    return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "Version", reader.pos()));
  }
  if (!have_data_version) [[unlikely]] {
    return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "DataVersion", reader.pos()));
  }
  if (!have_metadata) [[unlikely]] {
    return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "Metadata", reader.pos()));
  }
  if (!have_regions) [[unlikely]] {
    return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "Regions", reader.pos()));
  }
  return {};
}

}  // namespace fschema::litematic::internal