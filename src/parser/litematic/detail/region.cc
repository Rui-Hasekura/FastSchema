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

#include "parser/litematic/detail/region.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "parser/arena.h"
#include "parser/error.h"
#include "parser/litematic/detail/block_states.h"
#include "parser/litematic/detail/entity.h"
#include "parser/litematic/detail/palette.h"
#include "parser/litematic/detail/pending.h"
#include "parser/litematic/detail/tile_entity.h"
#include "parser/litematic/types.h"
#include "parser/nbt/reader.h"
#include "parser/nbt/skip.h"
#include "parser/nbt/tag.h"

namespace fschema::parser::litematic::detail {

  // Regions Compound: every region is a Compound entry
  //   <region_name>: Compound {
  //     Position: Compound { x,y,z: Int }
  //     Size: Compound { x,y,z: Int }
  //     BlockStatePalette: List<Compound>
  //     BlockStates: LongArray
  //     TileEntities: List<Compound>
  //     Entities: List<Compound>
  //     PendingBlockTicks / PendingFluidTicks (v6+)
  //     PendingBlockEntities / PendingEntities (v6+)
  //   }
  // Field order is not fixed (BlockStates can appear before Size/Palette),
  // so BlockStates is zero-copied as a span, and unpacking is delayed until
  // the end of the region.

  [[nodiscard]] ParseResult<std::array<std::int32_t, 3>>
    ParseVec3Int(nbt::ByteReader& reader) {
    std::array<std::int32_t, 3> result = { 0, 0, 0 };
    reader.push_depth();
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

      if (name == "x" && *tag_result == nbt::TagType::Int) {
        auto value = reader.Read<std::int32_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        result[0] = *value;
      }
      else if (name == "y" && *tag_result == nbt::TagType::Int) {
        auto value = reader.Read<std::int32_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        result[1] = *value;
      }
      else if (name == "z" && *tag_result == nbt::TagType::Int) {
        auto value = reader.Read<std::int32_t>();
        if (!value) {
          reader.pop_depth();
          return std::unexpected(value.error());
        }
        result[2] = *value;
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
    return result;
  }

  [[nodiscard]] ParseResult<Region> ParseRegion(
    nbt::ByteReader& reader, std::string_view region_name,
    Arena& arena) {
    Region region;
    region.name = region_name;

    reader.push_depth();
    bool have_size = false;
    bool have_palette = false;
    bool have_states = false;

    std::span<const std::byte> block_states_raw;

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

      if (name == "Position" && *tag_result == nbt::TagType::Compound) {
        auto pos_result = ParseVec3Int(reader);
        if (!pos_result) {
          reader.pop_depth();
          return std::unexpected(pos_result.error());
        }
        region.position = *pos_result;
      }
      else if (name == "Size" && *tag_result == nbt::TagType::Compound) {
        auto size_result = ParseVec3Int(reader);
        if (!size_result) {
          reader.pop_depth();
          return std::unexpected(size_result.error());
        }
        region.size = *size_result;
        have_size = true;
      }
      else if (name == "BlockStatePalette" && *tag_result == nbt::TagType::List) {
        auto pal_result = ParsePalette(reader, region.palette);
        if (!pal_result) {
          reader.pop_depth();
          return std::unexpected(pal_result.error());
        }
        have_palette = true;
      }
      else if (name == "BlockStates" && *tag_result == nbt::TagType::LongArray) {
        // Zero materialization: validate + span pass-through, unpacking delayed
        auto len_raw = reader.Read<std::int32_t>();
        if (!len_raw) {
          reader.pop_depth();
          return std::unexpected(len_raw.error());
        }
        const auto length = static_cast<std::int64_t>(*len_raw);
        if (length < 0) {
          reader.pop_depth();
          return std::unexpected(
            reader.Error(ParseError::Code::NegativeLength));
        }
        if (static_cast<std::uint64_t>(length) >
          reader.limits().max_array_elements) {
          reader.pop_depth();
          return std::unexpected(
            reader.Error(ParseError::Code::OversizedPayload));
        }
        const auto payload_bytes = static_cast<std::uint64_t>(length) * 8;
        if (reader.remaining() < payload_bytes) {
          reader.pop_depth();
          return std::unexpected(
            reader.Error(ParseError::Code::Truncated));
        }
        auto span_result = reader.PeekRaw(
          static_cast<std::size_t>(payload_bytes));
        if (!span_result) {
          reader.pop_depth();
          return std::unexpected(span_result.error());
        }
        block_states_raw = *span_result;
        reader.advance(static_cast<std::size_t>(payload_bytes));
        have_states = true;
      }
      else if (name == "TileEntities" && *tag_result == nbt::TagType::List) {
        auto tiles_result = ParseTileEntities(reader, region.tile_entities);
        if (!tiles_result) {
          reader.pop_depth();
          return std::unexpected(tiles_result.error());
        }
      }
      else if (name == "Entities" && *tag_result == nbt::TagType::List) {
        auto ents_result = ParseEntities(reader, region.entities);
        if (!ents_result) {
          reader.pop_depth();
          return std::unexpected(ents_result.error());
        }
      }
      else if (name == "PendingBlockTicks" &&
        *tag_result == nbt::TagType::List) {
        auto res_result = ParsePendingTicks(reader, region.pending_block_ticks);
        if (!res_result) {
          reader.pop_depth();
          return std::unexpected(res_result.error());
        }
      }
      else if (name == "PendingFluidTicks" &&
        *tag_result == nbt::TagType::List) {
        auto res_result = ParsePendingTicks(reader, region.pending_fluid_ticks);
        if (!res_result) {
          reader.pop_depth();
          return std::unexpected(res_result.error());
        }
      }
      else if (name == "PendingBlockEntities" &&
        *tag_result == nbt::TagType::List) {
        const auto start = reader.pos();
        auto skip_result = nbt::SkipPayload(reader, nbt::TagType::List);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
        region.pending_block_entities = reader.SpanFrom(start);
      }
      else if (name == "PendingEntities" &&
        *tag_result == nbt::TagType::List) {
        const auto start = reader.pos();
        auto skip_result = nbt::SkipPayload(reader, nbt::TagType::List);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
        region.pending_entities = reader.SpanFrom(start);
      }
      else {
        // Unknown -> Skip
        auto skip_result = nbt::SkipPayload(reader, *tag_result);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
      }
    }

    reader.pop_depth();

    if (!have_size) {
      return std::unexpected(ParseError::At(
        ParseError::Code::MissingField,
        std::format("Regions/{}/Size", region.name),
        reader.pos()));
    }
    if (!have_palette) {
      return std::unexpected(ParseError::At(
        ParseError::Code::MissingField,
        std::format("Regions/{}/BlockStatePalette", region.name),
        reader.pos()));
    }
    if (!have_states) {
      return std::unexpected(ParseError::At(
        ParseError::Code::MissingField,
        std::format("Regions/{}/BlockStates", region.name),
        reader.pos()));
    }

    // Overflow-safe volume calculation + limit check
    const auto abs_dim = [](std::int32_t v) -> std::uint64_t {
      return v < 0 ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(v))
        : static_cast<std::uint64_t>(v);
      };
    const std::uint64_t ax = abs_dim(region.size[0]);
    const std::uint64_t ay = abs_dim(region.size[1]);
    const std::uint64_t az = abs_dim(region.size[2]);
    const std::uint64_t limit = reader.limits().max_volume_per_region;

    std::uint64_t volume;
    if (ax == 0 || ay == 0 || az == 0) {
      volume = 0;
    }
    else if (ax > limit || ay > limit || az > limit) {
      return std::unexpected(
        reader.Error(ParseError::Code::OversizedPayload));
    }
    else {
      const std::uint64_t v2 = ax * ay;  // <= limit^2, no overflow
      if (v2 > limit / az) {
        return std::unexpected(
          reader.Error(ParseError::Code::OversizedPayload));
      }
      volume = v2 * az;  // <= limit
    }

    // Delayed fused unpacking (dispatch + allocation + kernel)
    const std::uint32_t bits_per_block = BitsPerBlock(region.palette.size());
    {
      auto indices_result = UnpackIndicesFused(
        block_states_raw, bits_per_block, volume,
        region.palette.size(), arena);
      if (!indices_result) {
        return std::unexpected(indices_result.error());
      }
      region.block_indices = std::move(*indices_result);
    }
    return region;
  }

  [[nodiscard]] ParseResult<void> ParseRegions(
    nbt::ByteReader& reader, Litematic& out) {
    reader.push_depth();

    // Regions count limit check
    std::size_t region_count = 0;

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

      if (++region_count > reader.limits().max_regions) {
        reader.pop_depth();
        return std::unexpected(
          reader.Error(ParseError::Code::OversizedPayload));
      }

      if (*tag_result == nbt::TagType::Compound) {
        auto region_result = ParseRegion(reader, name, *out.arena);
        if (!region_result) {
          reader.pop_depth();
          return std::unexpected(region_result.error());
        }
        out.regions.push_back(std::move(*region_result));
      }
      else {
        // Skip the entry if the Region is not a Compound.
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

}  // namespace fschema::parser::litematic::detail