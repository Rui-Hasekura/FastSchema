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

#include "fschema/litematic/internal/metadata.h"

#include <cstdint>
#include <expected>

#include "fschema/base/nbt/scope.h"
#include "fschema/base/nbt/skip.h"
#include "fschema/base/nbt/tag.h"

namespace fschema::litematic::internal {

[[nodiscard]] ParseResult<void> ParseMetadata(base::ByteReader& reader,
                                              Litematic& out) {
  auto& meta = out.metadata;
  const std::size_t compound_start = reader.pos();

  auto result = ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        // Common fields
        if (name == "Name" && t == base::TagType::String) {
          auto v = reader.ReadStringView();
          if (!v) return std::unexpected(v.error());
          meta.name = *v;
          return {};
        }
        if (name == "Author" && t == base::TagType::String) {
          auto v = reader.ReadStringView();
          if (!v) return std::unexpected(v.error());
          meta.author = *v;
          return {};
        }

        // Format-specific fields
        const auto payload_start = reader.pos();

        if (name == "Description" && t == base::TagType::String) {
          auto v = reader.ReadStringView();
          if (!v) return std::unexpected(v.error());
          meta.description = *v;
        } else if (name == "RegionCount" && t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
          meta.region_count = *v;
        } else if (name == "TotalBlocks" && t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
          meta.total_blocks = *v;
        } else if (name == "TotalVolume" && t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
          meta.total_volume = *v;
        } else if (name == "EnclosingSize" && t == base::TagType::Compound) {
          return ForEachCompoundField(
              reader,
              [&](std::string_view axis,
                  base::TagType at) -> ParseResult<void> {
                if (axis == "x" && at == base::TagType::Int) {
                  auto v = reader.Read<std::int32_t>();
                  if (!v) return std::unexpected(v.error());
                  meta.enclosing_size[0] = *v;
                } else if (axis == "y" && at == base::TagType::Int) {
                  auto v = reader.Read<std::int32_t>();
                  if (!v) return std::unexpected(v.error());
                  meta.enclosing_size[1] = *v;
                } else if (axis == "z" && at == base::TagType::Int) {
                  auto v = reader.Read<std::int32_t>();
                  if (!v) return std::unexpected(v.error());
                  meta.enclosing_size[2] = *v;
                } else {
                  return base::SkipPayload(reader, at);
                }
                return {};
              });
        } else if (name == "TimeCreated" && t == base::TagType::Long) {
          auto v = reader.Read<std::int64_t>();
          if (!v) return std::unexpected(v.error());
          meta.time_created = *v;
        } else if (name == "TimeModified" && t == base::TagType::Long) {
          auto v = reader.Read<std::int64_t>();
          if (!v) return std::unexpected(v.error());
          meta.time_modified = *v;
        } else if (name == "PreviewData" && t == base::TagType::IntArray) {
          auto length = reader.ReadLength(reader.limits().max_array_elements);
          if (!length) return std::unexpected(length.error());
          const auto total = (*length) * 4;
          auto span = reader.PeekRaw(total);
          if (!span) return std::unexpected(span.error());
          meta.preview_data = *span;
          reader.advance(total);
        } else {
          return base::SkipPayload(reader, t);
        }
        return {};
      });
  if (!result) return std::unexpected(result.error());
  meta.raw_compound = reader.SpanFrom(compound_start);
  return {};
}

}  // namespace fschema::litematic::internal