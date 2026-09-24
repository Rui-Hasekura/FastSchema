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

#include "fschema/schem/internal/palette.h"

#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/nbt_skip.h"
#include "fschema/base/nbt_tag.h"
#include "fschema/schem/internal/block_state_string.h"
#include "fschema/schem/types.h"

namespace fschema::schem::internal {

  struct PaletteEntry {
    std::string_view key;
    std::int32_t index;
  };

  [[nodiscard]] ParseResult<void> ParseBlockPalette(
    base::ByteReader& reader,
    std::vector<BlockState>& palette) {
    reader.push_depth();

    std::vector<PaletteEntry> entries;
    entries.reserve(64);
    std::int32_t max_index = -1;

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

      if (*tag_result != base::TagType::Int) {
        auto skip_result = base::SkipPayload(reader, *tag_result);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
        continue;
      }

      auto value = reader.Read<std::int32_t>();
      if (!value) {
        reader.pop_depth();
        return std::unexpected(value.error());
      }

      if (*value < 0) [[unlikely]] {
        reader.pop_depth();
        return std::unexpected(
          reader.Error(ParseError::Code::NegativeLength));
      }

      entries.push_back({ name, *value });
      if (*value > max_index) {
        max_index = *value;
      }
    }

    reader.pop_depth();

    if (max_index < 0) {
      palette.clear();
      return {};
    }

    if (static_cast<std::size_t>(max_index + 1) >
      reader.limits().max_palette_size) [[unlikely]] {
      return std::unexpected(ParseError{
          ParseError::Code::OversizedPayload,
          "Palette", 0 });
    }

    palette.resize(static_cast<std::size_t>(max_index + 1));
    for (const auto& entry : entries) {
      auto& bs = palette[static_cast<std::size_t>(entry.index)];
      ParseBlockStateString(entry.key, bs);
    }

    return {};
  }

  [[nodiscard]] ParseResult<void> ParseBiomePalette(
    base::ByteReader& reader,
    std::vector<std::string_view>& palette) {
    reader.push_depth();

    std::vector<PaletteEntry> entries;
    entries.reserve(32);
    std::int32_t max_index = -1;

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

      if (*tag_result != base::TagType::Int) {
        auto skip_result = base::SkipPayload(reader, *tag_result);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
        continue;
      }

      auto value = reader.Read<std::int32_t>();
      if (!value) {
        reader.pop_depth();
        return std::unexpected(value.error());
      }

      if (*value < 0) [[unlikely]] {
        reader.pop_depth();
        return std::unexpected(
          reader.Error(ParseError::Code::NegativeLength));
      }

      entries.push_back({ name, *value });
      if (*value > max_index) {
        max_index = *value;
      }
    }

    reader.pop_depth();

    if (max_index < 0) {
      palette.clear();
      return {};
    }

    if (static_cast<std::size_t>(max_index + 1) >
      reader.limits().max_palette_size) [[unlikely]] {
      return std::unexpected(ParseError{
          ParseError::Code::OversizedPayload,
          "BiomePalette", 0 });
    }

    palette.resize(static_cast<std::size_t>(max_index + 1));
    for (const auto& entry : entries) {
      palette[static_cast<std::size_t>(entry.index)] = entry.key;
    }

    return {};
  }

}  // namespace fschema::schem::internal