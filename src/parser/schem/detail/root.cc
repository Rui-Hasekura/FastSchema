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

#include "parser/schem/detail/root.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <utility>

#include "parser/arena.h"
#include "parser/error.h"
#include "parser/limits.h"
#include "parser/nbt/reader.h"
#include "parser/nbt/skip.h"
#include "parser/nbt/tag.h"
#include "parser/schem/detail/block_data.h"
#include "parser/schem/detail/block_entities.h"
#include "parser/schem/detail/entities.h"
#include "parser/schem/detail/palette.h"
#include "parser/schem/types.h"

namespace fschema::parser::schem::detail {

  [[nodiscard]] static ParseResult<void>
    ParseMetadata(nbt::ByteReader& reader, Schematic& out) {
    reader.push_depth();
    auto& meta = out.metadata;

    for (;;) {
      std::string_view name;
      auto tag_result = reader.ReadCompoundEntryHeaderView(name);
      if (!tag_result) {
        reader.pop_depth();
        return std::unexpected(tag_result.error());
      }
      if (*tag_result == nbt::TagType::End) {
        break;
      }

      if (name == "Name" && *tag_result == nbt::TagType::String) {
        auto v = reader.ReadStringView();
        if (!v) {
          reader.pop_depth();
          return std::unexpected(v.error());
        }
        meta.name = *v;
      }
      else if (name == "Author" &&
        *tag_result == nbt::TagType::String) {
        auto v = reader.ReadStringView();
        if (!v) {
          reader.pop_depth();
          return std::unexpected(v.error());
        }
        meta.author = *v;
      }
      else if (name == "Date" &&
        *tag_result == nbt::TagType::Long) {
        auto v = reader.Read<std::int64_t>();
        if (!v) {
          reader.pop_depth();
          return std::unexpected(v.error());
        }
        meta.date = *v;
      }
      else if (name == "RequiredMods" &&
        *tag_result == nbt::TagType::List) {
        const auto start = reader.pos();
        auto skip_result = nbt::SkipPayload(reader, nbt::TagType::List);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
        meta.required_mods = reader.SpanFrom(start);
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

  // v3 Blocks container: { Palette, Data, BlockEntities }
  [[nodiscard]] static ParseResult<void>
    ParseBlocksContainer(
      nbt::ByteReader& reader, Schematic& out) {
    reader.push_depth();

    std::span<const std::byte> block_data_raw;
    bool have_palette = false;
    bool have_data = false;

    for (;;) {
      std::string_view name;
      auto tag_result = reader.ReadCompoundEntryHeaderView(name);
      if (!tag_result) {
        reader.pop_depth();
        return std::unexpected(tag_result.error());
      }
      if (*tag_result == nbt::TagType::End) {
        break;
      }

      if (name == "Palette" &&
        *tag_result == nbt::TagType::Compound) {
        auto result = ParseBlockPalette(reader, out.palette);
        if (!result) {
          reader.pop_depth();
          return std::unexpected(result.error());
        }
        have_palette = true;
      }
      else if (name == "Data" &&
        *tag_result == nbt::TagType::ByteArray) {
        auto len = reader.ReadLength(
          reader.limits().max_array_elements);
        if (!len) {
          reader.pop_depth();
          return std::unexpected(len.error());
        }
        auto span_result = reader.PeekRaw(*len);
        if (!span_result) {
          reader.pop_depth();
          return std::unexpected(span_result.error());
        }
        block_data_raw = *span_result;
        reader.advance(*len);
        have_data = true;
      }
      else if (name == "BlockEntities" &&
        *tag_result == nbt::TagType::List) {
        auto result = ParseBlockEntities(
          reader, out.block_entities, /*is_v3=*/true);
        if (!result) {
          reader.pop_depth();
          return std::unexpected(result.error());
        }
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

    if (!have_palette) [[unlikely]] {
      return std::unexpected(ParseError::At(
        ParseError::Code::MissingField,
        "Blocks/Palette", reader.pos()));
    }
    if (!have_data) [[unlikely]] {
      return std::unexpected(ParseError::At(
        ParseError::Code::MissingField,
        "Blocks/Data", reader.pos()));
    }

    // Delayed varint decoding.
    const auto volume = VolumeOf(out);
    auto decode_result = DecodeVarintArray(
      block_data_raw, volume,
      out.palette.size(), *out.arena);
    if (!decode_result) {
      return std::unexpected(decode_result.error());
    }
    out.block_indices = std::move(*decode_result);

    return {};
  }

  // v3 Biomes container: { Palette, Data }
  [[nodiscard]] static ParseResult<void>
    ParseBiomesContainer(
      nbt::ByteReader& reader, Schematic& out) {
    reader.push_depth();

    std::span<const std::byte> biome_data_raw;
    bool have_palette = false;
    bool have_data = false;

    for (;;) {
      std::string_view name;
      auto tag_result = reader.ReadCompoundEntryHeaderView(name);
      if (!tag_result) {
        reader.pop_depth();
        return std::unexpected(tag_result.error());
      }
      if (*tag_result == nbt::TagType::End) {
        break;
      }

      if (name == "Palette" &&
        *tag_result == nbt::TagType::Compound) {
        auto result = ParseBiomePalette(
          reader, out.biome_palette);
        if (!result) {
          reader.pop_depth();
          return std::unexpected(result.error());
        }
        have_palette = true;
      }
      else if (name == "Data" &&
        *tag_result == nbt::TagType::ByteArray) {
        auto len = reader.ReadLength(
          reader.limits().max_array_elements);
        if (!len) {
          reader.pop_depth();
          return std::unexpected(len.error());
        }
        auto span_result = reader.PeekRaw(*len);
        if (!span_result) {
          reader.pop_depth();
          return std::unexpected(span_result.error());
        }
        biome_data_raw = *span_result;
        reader.advance(*len);
        have_data = true;
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

    if (!have_palette || !have_data) {
      // Biomes is optional; incomplete container treated as absent.
      return {};
    }

    // v3: 3D biomes, volume = W * H * L.
    const auto volume = VolumeOf(out);
    auto decode_result = DecodeVarintArray(
      biome_data_raw, volume,
      out.biome_palette.size(), *out.arena);
    if (!decode_result) {
      return std::unexpected(decode_result.error());
    }
    out.biome_indices = std::move(*decode_result);

    return {};
  }

  // Parses schematic fields for both v2 (flat layout) and v3
  // (Blocks/Biomes container layout).
  [[nodiscard]] static ParseResult<void>
    ParseSchematicFields(
      nbt::ByteReader& reader, Schematic& out,
      bool is_v3) {
    reader.push_depth();

    bool have_version = false;
    bool have_data_version = false;
    bool have_width = false;
    bool have_height = false;
    bool have_length = false;

    std::span<const std::byte> v2_block_data_raw;
    std::span<const std::byte> v2_biome_data_raw;
    bool v2_have_block_data = false;

    for (;;) {
      std::string_view name;
      auto tag_result = reader.ReadCompoundEntryHeaderView(name);
      if (!tag_result) {
        reader.pop_depth();
        return std::unexpected(tag_result.error());
      }
      if (*tag_result == nbt::TagType::End) {
        break;
      }

      // Common fields (v2 and v3).
      if (name == "Version" && *tag_result == nbt::TagType::Int) {
        auto v = reader.Read<std::int32_t>();
        if (!v) {
          reader.pop_depth();
          return std::unexpected(v.error());
        }
        if (*v < 2 || *v > 3) {
          reader.pop_depth();
          return std::unexpected(
            reader.Error(ParseError::Code::UnsupportedVersion));
        }
        out.version = static_cast<Version>(*v);
        have_version = true;
      }
      else if (name == "DataVersion" &&
        *tag_result == nbt::TagType::Int) {
        auto v = reader.Read<std::int32_t>();
        if (!v) {
          reader.pop_depth();
          return std::unexpected(v.error());
        }
        out.data_version = *v;
        have_data_version = true;
      }
      else if (name == "Metadata" &&
        *tag_result == nbt::TagType::Compound) {
        auto result = ParseMetadata(reader, out);
        if (!result) {
          reader.pop_depth();
          return std::unexpected(result.error());
        }
      }
      else if (name == "Width" &&
        *tag_result == nbt::TagType::Short) {
        auto v = reader.Read<std::int16_t>();
        if (!v) {
          reader.pop_depth();
          return std::unexpected(v.error());
        }
        out.width = static_cast<std::uint32_t>(
          static_cast<std::uint16_t>(*v));
        have_width = true;
      }
      else if (name == "Height" &&
        *tag_result == nbt::TagType::Short) {
        auto v = reader.Read<std::int16_t>();
        if (!v) {
          reader.pop_depth();
          return std::unexpected(v.error());
        }
        out.height = static_cast<std::uint32_t>(
          static_cast<std::uint16_t>(*v));
        have_height = true;
      }
      else if (name == "Length" &&
        *tag_result == nbt::TagType::Short) {
        auto v = reader.Read<std::int16_t>();
        if (!v) {
          reader.pop_depth();
          return std::unexpected(v.error());
        }
        out.length = static_cast<std::uint32_t>(
          static_cast<std::uint16_t>(*v));
        have_length = true;
      }
      else if (name == "Offset" &&
        *tag_result == nbt::TagType::IntArray) {
        auto len = reader.ReadLength(3);
        if (!len) {
          reader.pop_depth();
          return std::unexpected(len.error());
        }
        if (*len != 3) [[unlikely]] {
          reader.pop_depth();
          return std::unexpected(
            reader.Error(ParseError::Code::InvalidTagId));
        }
        for (int i = 0; i < 3; ++i) {
          auto v = reader.Read<std::int32_t>();
          if (!v) {
            reader.pop_depth();
            return std::unexpected(v.error());
          }
          out.offset[i] = *v;
        }
      }
      // v3: Blocks/Biomes containers.
      else if (is_v3 && name == "Blocks" &&
        *tag_result == nbt::TagType::Compound) {
        auto result = ParseBlocksContainer(reader, out);
        if (!result) {
          reader.pop_depth();
          return std::unexpected(result.error());
        }
      }
      else if (is_v3 && name == "Biomes" &&
        *tag_result == nbt::TagType::Compound) {
        auto result = ParseBiomesContainer(reader, out);
        if (!result) {
          reader.pop_depth();
          return std::unexpected(result.error());
        }
      }
      // v2: flat palette and block data.
      else if (!is_v3 && name == "Palette" &&
        *tag_result == nbt::TagType::Compound) {
        auto result = ParseBlockPalette(reader, out.palette);
        if (!result) {
          reader.pop_depth();
          return std::unexpected(result.error());
        }
      }
      else if (!is_v3 && name == "PaletteMax" &&
        *tag_result == nbt::TagType::Int) {
        // Size hint, actual size determined by scanning Palette.
        auto v = reader.Read<std::int32_t>();
        if (!v) {
          reader.pop_depth();
          return std::unexpected(v.error());
        }
      }
      else if (!is_v3 && name == "BlockData" &&
        *tag_result == nbt::TagType::ByteArray) {
        auto len = reader.ReadLength(
          reader.limits().max_array_elements);
        if (!len) {
          reader.pop_depth();
          return std::unexpected(len.error());
        }
        auto span_result = reader.PeekRaw(*len);
        if (!span_result) {
          reader.pop_depth();
          return std::unexpected(span_result.error());
        }
        v2_block_data_raw = *span_result;
        reader.advance(*len);
        v2_have_block_data = true;
      }
      else if (!is_v3 && name == "BlockEntities" &&
        *tag_result == nbt::TagType::List) {
        auto result = ParseBlockEntities(
          reader, out.block_entities, /*is_v3=*/false);
        if (!result) {
          reader.pop_depth();
          return std::unexpected(result.error());
        }
      }
      // v2: flat biome palette and data.
      else if (!is_v3 && name == "BiomePalette" &&
        *tag_result == nbt::TagType::Compound) {
        auto result = ParseBiomePalette(
          reader, out.biome_palette);
        if (!result) {
          reader.pop_depth();
          return std::unexpected(result.error());
        }
      }
      else if (!is_v3 && name == "BiomePaletteMax" &&
        *tag_result == nbt::TagType::Int) {
        auto v = reader.Read<std::int32_t>();
        if (!v) {
          reader.pop_depth();
          return std::unexpected(v.error());
        }
      }
      else if (!is_v3 && name == "BiomeData" &&
        *tag_result == nbt::TagType::ByteArray) {
        auto len = reader.ReadLength(
          reader.limits().max_array_elements);
        if (!len) {
          reader.pop_depth();
          return std::unexpected(len.error());
        }
        auto span_result = reader.PeekRaw(*len);
        if (!span_result) {
          reader.pop_depth();
          return std::unexpected(span_result.error());
        }
        v2_biome_data_raw = *span_result;
        reader.advance(*len);
      }
      // Entities (both v2 and v3).
      else if (name == "Entities" &&
        *tag_result == nbt::TagType::List) {
        auto result = ParseEntities(
          reader, out.entities, is_v3);
        if (!result) {
          reader.pop_depth();
          return std::unexpected(result.error());
        }
      }
      // Unknown field: skip.
      else {
        auto skip_result = nbt::SkipPayload(reader, *tag_result);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
      }
    }

    reader.pop_depth();

    // Required fields validation.
    if (!have_version) [[unlikely]] {
      return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "Version",
        reader.pos()));
    }
    if (!have_data_version) [[unlikely]] {
      return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "DataVersion",
        reader.pos()));
    }
    if (!have_width) [[unlikely]] {
      return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "Width",
        reader.pos()));
    }
    if (!have_height) [[unlikely]] {
      return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "Height",
        reader.pos()));
    }
    if (!have_length) [[unlikely]] {
      return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "Length",
        reader.pos()));
    }

    // Volume overflow check.
    const std::uint64_t volume = VolumeOf(out);
    if (volume > reader.limits().max_volume_per_region)
      [[unlikely]] {
      return std::unexpected(ParseError{
          ParseError::Code::VolumeOverflow,
          "Width*Height*Length", 0 });
    }

    // v2: delayed block data decoding.
    if (!is_v3 && v2_have_block_data) {
      auto decode_result = DecodeVarintArray(
        v2_block_data_raw, volume,
        out.palette.size(), *out.arena);
      if (!decode_result) {
        return std::unexpected(decode_result.error());
      }
      out.block_indices = std::move(*decode_result);
    }

    // v2: delayed biome data decoding.
    if (!is_v3 && !v2_biome_data_raw.empty() &&
      !out.biome_palette.empty()) {
      // v2: 2D biomes, volume = W * L.
      const auto biome_volume =
        static_cast<std::uint64_t>(out.width) *
        static_cast<std::uint64_t>(out.length);
      auto decode_result = DecodeVarintArray(
        v2_biome_data_raw, biome_volume,
        out.biome_palette.size(), *out.arena);
      if (!decode_result) {
        return std::unexpected(decode_result.error());
      }
      out.biome_indices = std::move(*decode_result);
    }

    return {};
  }

  [[nodiscard]] ParseResult<void> ParseRoot(
    nbt::ByteReader& reader, Schematic& out) {
    auto root_tag = reader.Read<std::uint8_t>();
    if (!root_tag) {
      return std::unexpected(root_tag.error());
    }
    if (*root_tag !=
      static_cast<std::uint8_t>(nbt::TagType::Compound)) {
      return std::unexpected(
        reader.Error(ParseError::Code::InvalidTagId));
    }

    auto root_name = reader.ReadStringView();
    if (!root_name) {
      return std::unexpected(root_name.error());
    }

    // v2: root name is "Schematic", fields directly in root.
    // v3: root name is "", contains "Schematic" child compound.
    if (*root_name == "Schematic") {
      return ParseSchematicFields(reader, out, /*is_v3=*/false);
    }

    // v3 path: scan root for "Schematic" child compound.
    reader.push_depth();

    bool found_schematic = false;

    for (;;) {
      std::string_view name;
      auto tag_result = reader.ReadCompoundEntryHeaderView(name);
      if (!tag_result) {
        reader.pop_depth();
        return std::unexpected(tag_result.error());
      }
      if (*tag_result == nbt::TagType::End) {
        break;
      }

      if (name == "Schematic" &&
        *tag_result == nbt::TagType::Compound) {
        auto result = ParseSchematicFields(
          reader, out, /*is_v3=*/true);
        if (!result) {
          reader.pop_depth();
          return std::unexpected(result.error());
        }
        found_schematic = true;
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

    if (!found_schematic) [[unlikely]] {
      return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "Schematic",
        reader.pos()));
    }

    return {};
  }

}  // namespace fschema::parser::schem::detail