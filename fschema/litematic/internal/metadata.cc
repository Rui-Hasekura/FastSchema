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

#include "fschema/base/nbt_skip.h"
#include "fschema/base/nbt_tag.h"

namespace fschema::litematic::internal {

  [[nodiscard]] ParseResult<void> ParseMetadata(
    base::ByteReader& reader, Litematic& out) {
    reader.push_depth();
    auto& meta = out.metadata;

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

      if (name == "Name" && *tag_result == base::TagType::String) {
        auto value = reader.ReadStringView();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.name = *value;
      }
      else if (name == "Author" && *tag_result == base::TagType::String) {
        auto value = reader.ReadStringView();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.author = *value;
      }
      else if (name == "Description" && *tag_result == base::TagType::String) {
        auto value = reader.ReadStringView();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.description = *value;
      }
      else if (name == "RegionCount" && *tag_result == base::TagType::Int) {
        auto value = reader.Read<std::int32_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.region_count = *value;
      }
      else if (name == "TotalBlocks" && *tag_result == base::TagType::Int) {
        auto value = reader.Read<std::int32_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.total_blocks = *value;
      }
      else if (name == "TotalVolume" && *tag_result == base::TagType::Int) {
        auto value = reader.Read<std::int32_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.total_volume = *value;
      }
      else if (name == "EnclosingSize" && *tag_result == base::TagType::Compound) {
        reader.push_depth();
        for (;;) {
          std::string_view axis_name;
          auto axis_tag = reader.ReadCompoundEntryHeaderView(axis_name);
          if (!axis_tag) {
            reader.pop_depth();
            reader.pop_depth();
            return std::unexpected(axis_tag.error());
          }
          if (*axis_tag == base::TagType::End) {
            break;
          }

          if (axis_name == "x" && *axis_tag == base::TagType::Int) {
            auto value = reader.Read<std::int32_t>();
            if (!value) {
              reader.pop_depth();
              reader.pop_depth();
              return std::unexpected(value.error());
            }
            meta.enclosing_size[0] = *value;
          }
          else if (axis_name == "y" && *axis_tag == base::TagType::Int) {
            auto value = reader.Read<std::int32_t>();
            if (!value) {
              reader.pop_depth();
              reader.pop_depth();
              return std::unexpected(value.error());
            }
            meta.enclosing_size[1] = *value;
          }
          else if (axis_name == "z" && *axis_tag == base::TagType::Int) {
            auto value = reader.Read<std::int32_t>();
            if (!value) {
              reader.pop_depth();
              reader.pop_depth();
              return std::unexpected(value.error());
            }
            meta.enclosing_size[2] = *value;
          }
          else {
            auto skip_result = base::SkipPayload(reader, *axis_tag);
            if (!skip_result) {
              reader.pop_depth();
              reader.pop_depth();
              return std::unexpected(skip_result.error());
            }
          }
        }
        reader.pop_depth();
      }
      else if (name == "TimeCreated" && *tag_result == base::TagType::Long) {
        auto value = reader.Read<std::int64_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.time_created = *value;
      }
      else if (name == "TimeModified" && *tag_result == base::TagType::Long) {
        auto value = reader.Read<std::int64_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.time_modified = *value;
      }
      else if (name == "PreviewData" && *tag_result == base::TagType::IntArray) {
        auto length = reader.ReadLength(reader.limits().max_array_elements);
        if (!length) {
          reader.pop_depth();
          return std::unexpected(length.error());
        }
        const auto total = (*length) * 4;
        auto span_result = reader.PeekRaw(total);
        if (!span_result) {
          reader.pop_depth();
          return std::unexpected(span_result.error());
        }
        meta.preview_data = *span_result;
        reader.advance(total);
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
    return {};
  }

}  // namespace fschema::litematic::internal