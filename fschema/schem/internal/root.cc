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

#include "fschema/schem/internal/root.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <utility>

#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/base/nbt_scope.h"
#include "fschema/base/nbt_skip.h"
#include "fschema/base/nbt_tag.h"
#include "fschema/schem/internal/block_data.h"
#include "fschema/schem/internal/block_entities.h"
#include "fschema/schem/internal/entities.h"
#include "fschema/schem/internal/palette.h"
#include "fschema/schem/types.h"

namespace fschema::schem::internal {

// ParseMetadata

[[nodiscard]] static ParseResult<void> ParseMetadata(base::ByteReader& reader,
                                                     Schematic& out) {
  auto& meta = out.metadata;

  return ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        if (name == "Name" && t == base::TagType::String) {
          auto v = reader.ReadStringView();
          if (!v) return std::unexpected(v.error());
          meta.name = *v;
        } else if (name == "Author" && t == base::TagType::String) {
          auto v = reader.ReadStringView();
          if (!v) return std::unexpected(v.error());
          meta.author = *v;
        } else if (name == "Date" && t == base::TagType::Long) {
          auto v = reader.Read<std::int64_t>();
          if (!v) return std::unexpected(v.error());
          meta.date = *v;
        } else if (name == "RequiredMods" && t == base::TagType::List) {
          const auto start = reader.pos();
          auto s = base::SkipPayload(reader, base::TagType::List);
          if (!s) return std::unexpected(s.error());
          meta.required_mods = reader.SpanFrom(start);
        } else {
          return base::SkipPayload(reader, t);
        }
        return {};
      });
}

// ParseBlocksContainer  (v3)

[[nodiscard]] static ParseResult<void> ParseBlocksContainer(
    base::ByteReader& reader,
    Schematic& out) {
  std::span<const std::byte> block_data_raw;
  bool have_palette = false;
  bool have_data = false;

  auto result = ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        if (name == "Palette" && t == base::TagType::Compound) {
          auto r = ParseBlockPalette(reader, out.palette);
          if (!r) return std::unexpected(r.error());
          have_palette = true;
        } else if (name == "Data" && t == base::TagType::ByteArray) {
          auto len = reader.ReadLength(reader.limits().max_array_elements);
          if (!len) return std::unexpected(len.error());
          auto span = reader.PeekRaw(*len);
          if (!span) return std::unexpected(span.error());
          block_data_raw = *span;
          reader.advance(*len);
          have_data = true;
        } else if (name == "BlockEntities" && t == base::TagType::List) {
          auto r = ParseBlockEntities(
              reader, out.block_entities, /*is_v3=*/true, *out.arena);
          if (!r) return std::unexpected(r.error());
        } else {
          return base::SkipPayload(reader, t);
        }
        return {};
      });
  if (!result) return std::unexpected(result.error());

  if (!have_palette) [[unlikely]] {
    return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "Blocks/Palette", reader.pos()));
  }
  if (!have_data) [[unlikely]] {
    return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "Blocks/Data", reader.pos()));
  }

  // Delayed varint decoding.
  const auto volume = VolumeOf(out);
  auto decode_result =
      DecodeVarintArray(block_data_raw, volume, out.palette.size(), *out.arena);
  if (!decode_result) {
    return std::unexpected(decode_result.error());
  }
  out.block_indices = std::move(*decode_result);

  return {};
}

// ParseBiomesContainer  (v3)

[[nodiscard]] static ParseResult<void> ParseBiomesContainer(
    base::ByteReader& reader,
    Schematic& out) {
  std::span<const std::byte> biome_data_raw;
  bool have_palette = false;
  bool have_data = false;

  auto result = ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        if (name == "Palette" && t == base::TagType::Compound) {
          auto r = ParseBiomePalette(reader, out.biome_palette);
          if (!r) return std::unexpected(r.error());
          have_palette = true;
        } else if (name == "Data" && t == base::TagType::ByteArray) {
          auto len = reader.ReadLength(reader.limits().max_array_elements);
          if (!len) return std::unexpected(len.error());
          auto span = reader.PeekRaw(*len);
          if (!span) return std::unexpected(span.error());
          biome_data_raw = *span;
          reader.advance(*len);
          have_data = true;
        } else {
          return base::SkipPayload(reader, t);
        }
        return {};
      });
  if (!result) return std::unexpected(result.error());

  if (!have_palette || !have_data) {
    // Biomes is optional; incomplete container treated as absent.
    return {};
  }

  // v3: 3D biomes, volume = W * H * L.
  const auto volume = VolumeOf(out);
  auto decode_result = DecodeVarintArray(
      biome_data_raw, volume, out.biome_palette.size(), *out.arena);
  if (!decode_result) {
    return std::unexpected(decode_result.error());
  }
  out.biome_indices = std::move(*decode_result);

  return {};
}

// ParseSchematicFields  (v2 flat layout + v3 container layout)

[[nodiscard]] static ParseResult<void>
ParseSchematicFields(base::ByteReader& reader, Schematic& out, bool is_v3) {
  bool have_version = false;
  bool have_data_version = false;
  bool have_width = false;
  bool have_height = false;
  bool have_length = false;

  std::span<const std::byte> v2_block_data_raw;
  std::span<const std::byte> v2_biome_data_raw;
  bool v2_have_block_data = false;

  auto result = ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        // Common fields (v2 and v3)
        if (name == "Version" && t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
          if (*v < 2 || *v > 3) {
            return std::unexpected(
                reader.Error(ParseError::Code::UnsupportedVersion));
          }
          out.version = static_cast<Version>(*v);
          have_version = true;
        } else if (name == "DataVersion" && t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
          out.data_version = *v;
          have_data_version = true;
        } else if (name == "Metadata" && t == base::TagType::Compound) {
          auto r = ParseMetadata(reader, out);
          if (!r) return std::unexpected(r.error());
        } else if (name == "Width" && t == base::TagType::Short) {
          auto v = reader.Read<std::int16_t>();
          if (!v) return std::unexpected(v.error());
          out.width =
              static_cast<std::uint32_t>(static_cast<std::uint16_t>(*v));
          have_width = true;
        } else if (name == "Height" && t == base::TagType::Short) {
          auto v = reader.Read<std::int16_t>();
          if (!v) return std::unexpected(v.error());
          out.height =
              static_cast<std::uint32_t>(static_cast<std::uint16_t>(*v));
          have_height = true;
        } else if (name == "Length" && t == base::TagType::Short) {
          auto v = reader.Read<std::int16_t>();
          if (!v) return std::unexpected(v.error());
          out.length =
              static_cast<std::uint32_t>(static_cast<std::uint16_t>(*v));
          have_length = true;
        } else if (name == "Offset" && t == base::TagType::IntArray) {
          auto len = reader.ReadLength(3);
          if (!len) return std::unexpected(len.error());
          if (*len != 3) [[unlikely]] {
            return std::unexpected(
                reader.Error(ParseError::Code::InvalidTagId));
          }
          for (int i = 0; i < 3; ++i) {
            auto v = reader.Read<std::int32_t>();
            if (!v) return std::unexpected(v.error());
            out.offset[i] = *v;
          }
        }
        // v3: Blocks/Biomes containers
        else if (is_v3 && name == "Blocks" && t == base::TagType::Compound) {
          auto r = ParseBlocksContainer(reader, out);
          if (!r) return std::unexpected(r.error());
        } else if (is_v3 && name == "Biomes" && t == base::TagType::Compound) {
          auto r = ParseBiomesContainer(reader, out);
          if (!r) return std::unexpected(r.error());
        }
        // v2: flat palette and block data
        else if (!is_v3 && name == "Palette" && t == base::TagType::Compound) {
          auto r = ParseBlockPalette(reader, out.palette);
          if (!r) return std::unexpected(r.error());
        } else if (!is_v3 && name == "PaletteMax" && t == base::TagType::Int) {
          // Size hint, actual size determined by scanning Palette.
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
        } else if (!is_v3 && name == "BlockData" &&
                   t == base::TagType::ByteArray) {
          auto len = reader.ReadLength(reader.limits().max_array_elements);
          if (!len) return std::unexpected(len.error());
          auto span = reader.PeekRaw(*len);
          if (!span) return std::unexpected(span.error());
          v2_block_data_raw = *span;
          reader.advance(*len);
          v2_have_block_data = true;
        } else if (!is_v3 && name == "BlockEntities" &&
                   t == base::TagType::List) {
          auto r = ParseBlockEntities(
              reader, out.block_entities, /*is_v3=*/false, *out.arena);
          if (!r) return std::unexpected(r.error());
        }
        // v2: flat biome palette and data
        else if (!is_v3 && name == "BiomePalette" &&
                 t == base::TagType::Compound) {
          auto r = ParseBiomePalette(reader, out.biome_palette);
          if (!r) return std::unexpected(r.error());
        } else if (!is_v3 && name == "BiomePaletteMax" &&
                   t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
        } else if (!is_v3 && name == "BiomeData" &&
                   t == base::TagType::ByteArray) {
          auto len = reader.ReadLength(reader.limits().max_array_elements);
          if (!len) return std::unexpected(len.error());
          auto span = reader.PeekRaw(*len);
          if (!span) return std::unexpected(span.error());
          v2_biome_data_raw = *span;
          reader.advance(*len);
        }
        // Entities (both v2 and v3)
        else if (name == "Entities" && t == base::TagType::List) {
          auto r = ParseEntities(reader, out.entities, is_v3, *out.arena);
          if (!r) return std::unexpected(r.error());
        }
        // Skip unknown field
        else {
          return base::SkipPayload(reader, t);
        }
        return {};
      });
  if (!result) return std::unexpected(result.error());

  // Required fields validation
  if (!have_version) [[unlikely]] {
    return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "Version", reader.pos()));
  }
  if (!have_data_version) [[unlikely]] {
    return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "DataVersion", reader.pos()));
  }
  if (!have_width) [[unlikely]] {
    return std::unexpected(
        ParseError::At(ParseError::Code::MissingField, "Width", reader.pos()));
  }
  if (!have_height) [[unlikely]] {
    return std::unexpected(
        ParseError::At(ParseError::Code::MissingField, "Height", reader.pos()));
  }
  if (!have_length) [[unlikely]] {
    return std::unexpected(
        ParseError::At(ParseError::Code::MissingField, "Length", reader.pos()));
  }

  // Volume overflow check
  const std::uint64_t volume = VolumeOf(out);
  if (volume > reader.limits().max_volume_per_region) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::VolumeOverflow, "Width*Height*Length", 0});
  }

  // v2: delayed block data decoding
  if (!is_v3 && v2_have_block_data) {
    auto decode_result = DecodeVarintArray(
        v2_block_data_raw, volume, out.palette.size(), *out.arena);
    if (!decode_result) {
      return std::unexpected(decode_result.error());
    }
    out.block_indices = std::move(*decode_result);
  }

  // v2: delayed biome data decoding
  if (!is_v3 && !v2_biome_data_raw.empty() && !out.biome_palette.empty()) {
    // v2: 2D biomes, volume = W * L.
    const auto biome_volume = static_cast<std::uint64_t>(out.width) *
                              static_cast<std::uint64_t>(out.length);
    auto decode_result = DecodeVarintArray(
        v2_biome_data_raw, biome_volume, out.biome_palette.size(), *out.arena);
    if (!decode_result) {
      return std::unexpected(decode_result.error());
    }
    out.biome_indices = std::move(*decode_result);
  }

  return {};
}

// ParseRoot

[[nodiscard]] ParseResult<void> ParseRoot(base::ByteReader& reader,
                                          Schematic& out) {
  auto root_tag = reader.Read<std::uint8_t>();
  if (!root_tag) {
    return std::unexpected(root_tag.error());
  }
  if (*root_tag != static_cast<std::uint8_t>(base::TagType::Compound)) {
    return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
  }

  auto root_name = reader.ReadStringView();
  if (!root_name) {
    return std::unexpected(root_name.error());
  }

  if (*root_name == "Schematic") {
    return ParseSchematicFields(reader, out, /*is_v3=*/false);
  }

  bool found_schematic = false;

  auto result = ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        if (name == "Schematic" && t == base::TagType::Compound) {
          auto r = ParseSchematicFields(reader, out, /*is_v3=*/true);
          if (!r) return std::unexpected(r.error());
          found_schematic = true;
        } else {
          return base::SkipPayload(reader, t);
        }
        return {};
      });
  if (!result) return std::unexpected(result.error());

  if (!found_schematic) [[unlikely]] {
    return std::unexpected(ParseError::At(
        ParseError::Code::MissingField, "Schematic", reader.pos()));
  }

  return {};
}

}  // namespace fschema::schem::internal