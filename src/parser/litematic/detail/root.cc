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

#include "parser/litematic/detail/root.h"

#include <cstdint>
#include <expected>
#include <string>

#include "parser/litematic/detail/metadata.h"
#include "parser/litematic/detail/region.h"
#include "parser/nbt/skip.h"
#include "parser/nbt/tag.h"

namespace fschema::parser::litematic::detail {

  // Litematica root Compound fields (Java write order is fixed):
  //   Version: Int          <- Necessary
  //   SubVersion: Int       <- v6+ Optional, Skip
  //   DataVersion: Int      <- v5+ Necessary
  //   Metadata: Compound    <- Necessary
  //   Regions: Compound     <- Necessary
  //
  // Unknown field -> SkipPayload
  [[nodiscard]] ParseResult<void> ParseRoot(
    nbt::ByteReader& reader, Litematic& out) {
    // NBT root: TagID(1) + name + Compound payload
    // The first byte must be TAG_Compound(10)
      {
        auto root_tag = reader.Read<std::uint8_t>();
        if (!root_tag) {
          return std::unexpected(root_tag.error());
        }
        if (*root_tag != static_cast<std::uint8_t>(nbt::TagType::Compound)) {
          return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
        }
        auto root_name = reader.ReadString();
        if (!root_name) {
          return std::unexpected(root_name.error());
        }
        // Usually the root name is an empty string, ignore its content
      }

      reader.push_depth();

      // Parse root Compound's entries
      bool have_version = false;
      bool have_metadata = false;
      bool have_regions = false;
      bool have_data_version = false;

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

        reader.set_path(name);

        if (name == "Version" && *tag_result == nbt::TagType::Int) {
          auto value = reader.Read<std::int32_t>();
          if (!value) {
            reader.pop_depth();
            return std::unexpected(value.error());
          }
          if (*value < 5 || *value > 7) {
            reader.pop_depth();
            return std::unexpected(
              reader.Error(ParseError::Code::UnsupportedVersion));
          }
          out.version = static_cast<Version>(*value);
          have_version = true;
        }
        else if ((name == "DataVersion" || name == "MinecraftDataVersion") &&
          *tag_result == nbt::TagType::Int) {
          auto value = reader.Read<std::int32_t>();
          if (!value) {
            reader.pop_depth();
            return std::unexpected(value.error());
          }
          out.data_version = *value;
          have_data_version = true;
        }
        else if (name == "SubVersion" && *tag_result == nbt::TagType::Int) {
          // Decorative field, read and discard
          auto value = reader.Read<std::int32_t>();
          if (!value) {
            reader.pop_depth();
            return std::unexpected(value.error());
          }
        }
        else if (name == "Metadata" && *tag_result == nbt::TagType::Compound) {
          auto meta_result = ParseMetadata(reader, out);
          if (!meta_result) {
            reader.pop_depth();
            return std::unexpected(meta_result.error());
          }
          have_metadata = true;
        }
        else if (name == "Regions" && *tag_result == nbt::TagType::Compound) {
          auto regions_result = ParseRegions(reader, out);
          if (!regions_result) {
            reader.pop_depth();
            return std::unexpected(regions_result.error());
          }
          have_regions = true;
        }
        else {
          // Unknown field -> SkipPayload
          auto skip_result = nbt::SkipPayload(reader, *tag_result);
          if (!skip_result) {
            reader.pop_depth();
            return std::unexpected(skip_result.error());
          }
        }
      }

      reader.pop_depth();

      // Necessary fields check
      if (!have_version) {
        return std::unexpected(
          ParseError::At(ParseError::Code::MissingField, "Version", reader.pos()));
      }
      if (!have_data_version) {
        return std::unexpected(
          ParseError::At(ParseError::Code::MissingField, "DataVersion", reader.pos()));
      }
      if (!have_metadata) {
        return std::unexpected(
          ParseError::At(ParseError::Code::MissingField, "Metadata", reader.pos()));
      }
      if (!have_regions) {
        return std::unexpected(
          ParseError::At(ParseError::Code::MissingField, "Regions", reader.pos()));
      }

      return {};
  }

} // namespace fschema::parser::litematic::detail