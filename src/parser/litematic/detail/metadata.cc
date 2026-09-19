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

#include "parser/litematic/detail/metadata.h"

#include <cstdint>
#include <expected>
#include <string>
#include <utility>

#include "parser/nbt/skip.h"
#include "parser/nbt/tag.h"

namespace fschema::parser::litematic::detail {

  // Metadata Compound:
  //   Name: String
  //   Author: String
  //   Description: String
  //   RegionCount: Int
  //   TotalBlocks: Int
  //   TotalVolume: Int
  //   EnclosingSize: Compound { x,y,z: Int }
  //   TimeCreated: Long
  //   TimeModified: Long
  //   PreviewData: IntArray (Optional, 1.13+)
  [[nodiscard]] ParseResult<void> ParseMetadata(
    nbt::ByteReader& reader, Litematic& out) {
    reader.push_depth();
    auto& meta = out.metadata;

    for (;;) {
      std::string name;
      auto tag_result = reader.ReadCompoundEntryHeader(name);
      if (!tag_result) {
        reader.pop_depth();
        return std::unexpected(tag_result.error());
      }
      if (*tag_result == nbt::TagType::End) {
        break;
      }

      reader.set_path("Metadata/" + name);

      if (name == "Name" && *tag_result == nbt::TagType::String) {
        auto value = reader.ReadString();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.name = std::move(*value);
      }
      else if (name == "Author" && *tag_result == nbt::TagType::String) {
        auto value = reader.ReadString();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.author = std::move(*value);
      }
      else if (name == "Description" && *tag_result == nbt::TagType::String) {
        auto value = reader.ReadString();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.description = std::move(*value);
      }
      else if (name == "RegionCount" && *tag_result == nbt::TagType::Int) {
        auto value = reader.Read<std::int32_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.region_count = *value;
      }
      else if (name == "TotalBlocks" && *tag_result == nbt::TagType::Int) {
        auto value = reader.Read<std::int32_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.total_blocks = *value;
      }
      else if (name == "TotalVolume" && *tag_result == nbt::TagType::Int) {
        auto value = reader.Read<std::int32_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.total_volume = *value;
      }
      else if (name == "EnclosingSize" && *tag_result == nbt::TagType::Compound) {
        // Sub Compound: { x: Int, y: Int, z: Int }
        reader.push_depth();
        for (;;) {
          std::string axis_name;
          auto axis_tag = reader.ReadCompoundEntryHeader(axis_name);
          if (!axis_tag) {
            reader.pop_depth();
            reader.pop_depth();
            return std::unexpected(axis_tag.error());
          }
          if (*axis_tag == nbt::TagType::End) {
            break;
          }

          if (axis_name == "x" && *axis_tag == nbt::TagType::Int) {
            auto value = reader.Read<std::int32_t>();
            if (!value) {
              reader.pop_depth();
              reader.pop_depth();
              return std::unexpected(value.error());
            }
            meta.enclosing_size[0] = *value;
          }
          else if (axis_name == "y" && *axis_tag == nbt::TagType::Int) {
            auto value = reader.Read<std::int32_t>();
            if (!value) {
              reader.pop_depth();
              reader.pop_depth();
              return std::unexpected(value.error());
            }
            meta.enclosing_size[1] = *value;
          }
          else if (axis_name == "z" && *axis_tag == nbt::TagType::Int) {
            auto value = reader.Read<std::int32_t>();
            if (!value) {
              reader.pop_depth();
              reader.pop_depth();
              return std::unexpected(value.error());
            }
            meta.enclosing_size[2] = *value;
          }
          else {
            auto skip_result = nbt::SkipPayload(reader, *axis_tag);
            if (!skip_result) {
              reader.pop_depth();
              reader.pop_depth();
              return std::unexpected(skip_result.error());
            }
          }
        }
        reader.pop_depth();
      }
      else if (name == "TimeCreated" && *tag_result == nbt::TagType::Long) {
        auto value = reader.Read<std::int64_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.time_created = *value;
      }
      else if (name == "TimeModified" && *tag_result == nbt::TagType::Long) {
        auto value = reader.Read<std::int64_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        meta.time_modified = *value;
      }
      else if (name == "PreviewData" && *tag_result == nbt::TagType::IntArray) {
        // Reserved raw span, it might be too large, don't materialize it
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
        auto skip_result = nbt::SkipPayload(reader, *tag_result);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
      }
    }

    reader.pop_depth();
    return {};
  }

} // namespace fschema::parser::litematic::detail