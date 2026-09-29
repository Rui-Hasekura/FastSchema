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

#include "fschema/litematic/internal/region.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/base/nbt_scope.h"
#include "fschema/base/nbt_skip.h"
#include "fschema/base/nbt_tag.h"
#include "fschema/litematic/internal/block_states.h"
#include "fschema/litematic/internal/entity.h"
#include "fschema/litematic/internal/palette.h"
#include "fschema/litematic/internal/pending.h"
#include "fschema/litematic/internal/tile_entity.h"
#include "fschema/litematic/types.h"
#include "fschema/memory/arena.h"

namespace fschema::litematic::internal {

[[nodiscard]] ParseResult<std::array<std::int32_t, 3>> ParseVec3Int(
    base::ByteReader& reader) {
  std::array<std::int32_t, 3> result = {0, 0, 0};

  auto r = ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        if (name == "x" && t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
          result[0] = *v;
        } else if (name == "y" && t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
          result[1] = *v;
        } else if (name == "z" && t == base::TagType::Int) {
          auto v = reader.Read<std::int32_t>();
          if (!v) return std::unexpected(v.error());
          result[2] = *v;
        } else {
          return base::SkipPayload(reader, t);
        }
        return {};
      });
  if (!r) return std::unexpected(r.error());
  return result;
}

[[nodiscard]] ParseResult<Region> ParseRegion(base::ByteReader& reader,
                                              std::string_view region_name,
                                              memory::Arena& arena) {
  Region region;
  region.name = region_name;

  bool have_size = false;
  bool have_palette = false;
  bool have_states = false;
  bool have_position = false;
  std::span<const std::byte> block_states_raw;

  auto result = ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        const std::size_t payload_start = reader.pos();

        if (name == "Position" && t == base::TagType::Compound) {
          auto r = ParseVec3Int(reader);
          if (!r) return std::unexpected(r.error());
          region.position = *r;
          have_position = true;
        } else if (name == "Size" && t == base::TagType::Compound) {
          auto r = ParseVec3Int(reader);
          if (!r) return std::unexpected(r.error());
          region.size = *r;
          have_size = true;
        } else if (name == "BlockStatePalette" && t == base::TagType::List) {
          auto r = ParsePalette(reader, region.palette);
          if (!r) return std::unexpected(r.error());
          have_palette = true;
        } else if (name == "BlockStates" && t == base::TagType::LongArray) {
          auto len_raw = reader.Read<std::int32_t>();
          if (!len_raw) return std::unexpected(len_raw.error());
          const auto length = static_cast<std::int64_t>(*len_raw);
          if (length < 0) {
            return std::unexpected(
                reader.Error(ParseError::Code::NegativeLength));
          }
          if (static_cast<std::uint64_t>(length) >
              reader.limits().max_array_elements) {
            return std::unexpected(
                reader.Error(ParseError::Code::OversizedPayload));
          }
          const auto payload_bytes = static_cast<std::uint64_t>(length) * 8;
          if (reader.remaining() < payload_bytes) {
            return std::unexpected(reader.Error(ParseError::Code::Truncated));
          }
          auto span_result =
              reader.PeekRaw(static_cast<std::size_t>(payload_bytes));
          if (!span_result) return std::unexpected(span_result.error());
          block_states_raw = *span_result;
          reader.advance(static_cast<std::size_t>(payload_bytes));
          have_states = true;
        } else if (name == "TileEntities" && t == base::TagType::List) {
          auto r = ParseTileEntities(reader, region.tile_entities);
          if (!r) return std::unexpected(r.error());
        } else if (name == "Entities" && t == base::TagType::List) {
          auto r = ParseEntities(reader, region.entities);
          if (!r) return std::unexpected(r.error());
        } else if (name == "PendingBlockTicks" && t == base::TagType::List) {
          auto r = ParsePendingTicks(reader, region.pending_block_ticks);
          if (!r) return std::unexpected(r.error());
          region.extensions.push_back(
              {name, t, reader.SpanFrom(payload_start)});
        } else if (name == "PendingFluidTicks" && t == base::TagType::List) {
          auto r = ParsePendingTicks(reader, region.pending_fluid_ticks);
          if (!r) return std::unexpected(r.error());
          region.extensions.push_back(
              {name, t, reader.SpanFrom(payload_start)});
        } else if (name == "PendingBlockEntities" && t == base::TagType::List) {
          auto r = base::SkipPayload(reader, base::TagType::List);
          if (!r) return std::unexpected(r.error());
          region.pending_block_entities = reader.SpanFrom(payload_start);
          region.extensions.push_back(
              {name, t, reader.SpanFrom(payload_start)});
        } else if (name == "PendingEntities" && t == base::TagType::List) {
          auto r = base::SkipPayload(reader, base::TagType::List);
          if (!r) return std::unexpected(r.error());
          region.pending_entities = reader.SpanFrom(payload_start);
          region.extensions.push_back(
              {name, t, reader.SpanFrom(payload_start)});
        } else {
          auto s = base::SkipPayload(reader, t);
          if (!s) return std::unexpected(s.error());
          region.extensions.push_back(
              {name, t, reader.SpanFrom(payload_start)});
        }
        return {};
      });
  if (!result) return std::unexpected(result.error());

  // Required-field checks
  if (!have_size) [[unlikely]] {
    return std::unexpected(
        ParseError::At(ParseError::Code::MissingField,
                       std::format("Regions/{}/Size", region.name),
                       reader.pos()));
  }
  if (!have_palette) [[unlikely]] {
    return std::unexpected(
        ParseError::At(ParseError::Code::MissingField,
                       std::format("Regions/{}/BlockStatePalette", region.name),
                       reader.pos()));
  }
  if (!have_states) [[unlikely]] {
    return std::unexpected(
        ParseError::At(ParseError::Code::MissingField,
                       std::format("Regions/{}/BlockStates", region.name),
                       reader.pos()));
  }
  if (!have_position) [[unlikely]] {
    return std::unexpected(
        ParseError::At(ParseError::Code::MissingField,
                       std::format("Regions/{}/Position", region.name),
                       reader.pos()));
  }

  // Overflow-safe volume calculation
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
  } else if (ax > limit || ay > limit || az > limit) {
    return std::unexpected(reader.Error(ParseError::Code::OversizedPayload));
  } else {
    const std::uint64_t v2 = ax * ay;
    if (v2 > limit / az) {
      return std::unexpected(reader.Error(ParseError::Code::OversizedPayload));
    }
    volume = v2 * az;
  }

  const std::uint32_t bits_per_block = BitsPerBlock(region.palette.size());
  auto indices = UnpackIndicesFused(
      block_states_raw, bits_per_block, volume, region.palette.size(), arena);
  if (!indices) return std::unexpected(indices.error());
  region.block_indices = std::move(*indices);
  return region;
}

[[nodiscard]] ParseResult<void> ParseRegions(base::ByteReader& reader,
                                             Litematic& out) {
  std::size_t region_count = 0;

  auto result = ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        if (++region_count > reader.limits().max_regions) [[unlikely]] {
          return std::unexpected(
              reader.Error(ParseError::Code::OversizedPayload));
        }
        if (t == base::TagType::Compound) {
          auto r = ParseRegion(reader, name, *out.arena);
          if (!r) return std::unexpected(r.error());
          out.regions.push_back(std::move(*r));
        } else {
          return base::SkipPayload(reader, t);
        }
        return {};
      });
  if (!result) return std::unexpected(result.error());
  return {};
}

}  // namespace fschema::litematic::internal