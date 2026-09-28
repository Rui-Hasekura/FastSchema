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

#include "fschema/ir/litematic_handler.h"

#include <bit>
#include <chrono>
#include <vector>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "fschema/base/nbt_skip.h"
#include "fschema/base/nbt_writer.h"
#include "fschema/ir/internal/bit_pack.h"
#include "fschema/ir/internal/codec_utils.h"
#include "fschema/litematic/parse.h"

namespace fschema::ir::format {

ParseResult<Schema> LitematicaHandler::Decode(
    std::unique_ptr<std::vector<std::byte>> bytes,
    const base::DecodeLimits& limits) const {
  auto lit = litematic::ParseLitematic(std::move(bytes), limits);
  if (!lit) return std::unexpected(lit.error());
  return DecodeFromParsed(std::move(*lit));
}

constexpr std::uint16_t kAirIdxSentinel = 0xFFFF;

ParseResult<std::vector<std::byte>> LitematicaHandler::Encode(
    const Schema& ir,
    const EncodeOptions& options) const {
  base::NbtWriter writer;

  std::int32_t version = 5;
  if (options.target_version >= 5 && options.target_version <= 7) {
    version = options.target_version;
  } else if (ir.data_version >= kDataVersion1205) {
    version = 7;
  } else if (ir.data_version >= kDataVersion118) {
    version = 6;
  }

  writer.BeginRootCompound("Litematic");
  writer.WriteIntField("Version", version);

  if (version >= 7) {
    writer.WriteIntField("SubVersion", 1);
  }

  writer.WriteIntField("MinecraftDataVersion", ir.data_version);

  int64_t ms = absl::ToUnixMillis(absl::Now());
  std::int64_t time_created =
      ir.metadata.time_created != 0 ? ir.metadata.time_created : ms;
  std::int64_t time_modified =
      ir.metadata.time_modified != 0 ? ir.metadata.time_modified : ms;
  std::string_view name =
      ir.metadata.name.empty() ? "Unnamed" : ir.metadata.name;
  std::string_view author =
      ir.metadata.author.empty() ? "Unknown" : ir.metadata.author;

  writer.BeginCompoundField("Metadata");
  writer.WriteStringField("Name", name);
  writer.WriteStringField("Author", author);
  writer.WriteStringField("Description", ir.metadata.description);
  writer.WriteIntField("RegionCount",
                       static_cast<std::int32_t>(ir.regions.size()));

  std::int32_t total_blocks = 0;
  std::int64_t total_volume = 0;
  for (const auto& reg : ir.regions) {
    total_volume += static_cast<std::int64_t>(std::abs(reg.bounds.size[0])) *
                    static_cast<std::int64_t>(std::abs(reg.bounds.size[1])) *
                    static_cast<std::int64_t>(std::abs(reg.bounds.size[2]));
    if (reg.palette.empty() || reg.block_indices.empty()) continue;

    // Build air-ness LUT once per region: avoids 3 string compares per block.
    // palette.size() <= 65536 (DecodeLimits::max_palette_size) -> LUT <= 64KB.
    std::vector<std::uint8_t> is_air(reg.palette.size(), 0);
    for (std::size_t i = 0; i < reg.palette.size(); ++i) {
      const auto& n = reg.palette[i].name;
      if (n == "minecraft:air" || n == "minecraft:cave_air" ||
          n == "minecraft:void_air") {
        is_air[i] = 1;
      }
    }
    for (std::uint16_t idx : reg.block_indices) {
      if (idx < is_air.size()) {
        total_blocks += is_air[idx] ^ 1;  // 1 if non-air, 0 if air
      }
    }
  }

  writer.WriteIntField("TotalBlocks", total_blocks);
  writer.WriteIntField("TotalVolume", total_volume);
  writer.BeginCompoundField("EnclosingSize");
  std::array<std::int32_t, 3> enc_size = ir.metadata.enclosing_size;
  if (enc_size[0] == 0 && enc_size[1] == 0 && enc_size[2] == 0) {
    if (!ir.regions.empty()) {
      for (int i = 0; i < 3; ++i) {
        enc_size[i] = std::abs(ir.regions[0].bounds.size[i]);
      }
    }
  }
  writer.WriteIntField("x", enc_size[0]);
  writer.WriteIntField("y", enc_size[1]);
  writer.WriteIntField("z", enc_size[2]);
  writer.EndCompoundField();
  writer.WriteLongField("TimeCreated", time_created);
  writer.WriteLongField("TimeModified", time_modified);
  writer.EndCompoundField();

  writer.BeginCompoundField("Regions");
  for (const auto& reg : ir.regions) {
    writer.BeginCompoundField(reg.name);

    writer.BeginCompoundField("Position");
    writer.WriteIntField("x", reg.bounds.origin[0]);
    writer.WriteIntField("y", reg.bounds.origin[1]);
    writer.WriteIntField("z", reg.bounds.origin[2]);
    writer.EndCompoundField();

    writer.BeginCompoundField("Size");
    writer.WriteIntField("x", reg.bounds.size[0]);
    writer.WriteIntField("y", reg.bounds.size[1]);
    writer.WriteIntField("z", reg.bounds.size[2]);
    writer.EndCompoundField();

    std::vector<ir::BlockState> out_palette;
    std::vector<std::uint16_t> out_indices(reg.block_indices.size());

    std::uint16_t air_idx = kAirIdxSentinel;
    for (std::size_t i = 0; i < reg.palette.size(); ++i) {
      if (reg.palette[i].name == "minecraft:air" ||
          reg.palette[i].name == "minecraft:cave_air" ||
          reg.palette[i].name == "minecraft:void_air") {
        air_idx = static_cast<std::uint16_t>(i);
        break;
      }
    }

    if (air_idx == 0) {  // already air at 0
      out_palette.assign(reg.palette.begin(), reg.palette.end());
      std::memcpy(out_indices.data(),
                  reg.block_indices.data(),
                  reg.block_indices.size() * sizeof(std::uint16_t));
    } else if (air_idx != kAirIdxSentinel) {
      out_palette.assign(reg.palette.begin(), reg.palette.end());
      std::swap(out_palette[0], out_palette[air_idx]);
      for (std::size_t i = 0; i < reg.block_indices.size(); ++i) {
        std::uint16_t idx = reg.block_indices[i];
        if (idx == 0) {
          out_indices[i] = air_idx;
        } else if (idx == air_idx) {
          out_indices[i] = 0;
        } else {
          out_indices[i] = idx;
        }
      }
    } else {
      if (reg.palette.size() >= 65536) {
        return std::unexpected(ParseError{
            ParseError::Code::OversizedPayload, "PaletteOverflow", 0});
      }
      out_palette.push_back({"minecraft:air", {}, PropertyEncoding::kNone});
      for (const auto& bs : reg.palette) out_palette.push_back(bs);
      for (std::size_t i = 0; i < reg.block_indices.size(); ++i) {
        out_indices[i] = static_cast<std::uint16_t>(reg.block_indices[i] + 1);
      }
    }

    writer.BeginListField(
        "BlockStatePalette", base::TagType::Compound, out_palette.size());
    for (const auto& bs : out_palette) {
      writer.BeginListElementCompound();
      writer.WriteStringField("Name", bs.name);
      if (bs.prop_encoding == PropertyEncoding::kNbt &&
          !bs.raw_properties.empty()) {
        writer.WriteRawField(
            "Properties", base::TagType::Compound, bs.raw_properties);
      } else if (bs.prop_encoding == PropertyEncoding::kString &&
                 !bs.raw_properties.empty()) {
        writer.BeginCompoundField("Properties");
        std::string_view props_str(
            reinterpret_cast<const char*>(bs.raw_properties.data()),
            bs.raw_properties.size());
        std::size_t start = 0;
        while (start < props_str.size()) {
          auto comma = props_str.find(',', start);
          auto end =
              (comma == std::string_view::npos) ? props_str.size() : comma;
          auto pair = props_str.substr(start, end - start);
          auto eq = pair.find('=');
          if (eq != std::string_view::npos) {
            writer.WriteStringField(pair.substr(0, eq), pair.substr(eq + 1));
          }
          start = end + 1;
        }
        writer.EndCompoundField();
      }
      writer.EndListElementCompound();
    }
    writer.EndListField();

    std::uint32_t bpb = out_palette.size() <= 4
                            ? 2
                            : static_cast<std::uint32_t>(
                                  std::bit_width(out_palette.size() - 1));
    if (bpb < 2) bpb = 2;
    auto longs = internal::PackIndicesLitematic(
        std::span<const std::uint16_t>(out_indices.data(), out_indices.size()),
        bpb);
    writer.WriteLongArrayField(
        "BlockStates",
        std::span<const std::int64_t>(
            reinterpret_cast<const std::int64_t*>(longs.data()), longs.size()));

    writer.BeginListField(
        "TileEntities", base::TagType::Compound, reg.block_entities.size());

    for (const auto& te : reg.block_entities) {
      writer.BeginListElementCompound();

      auto write_te_body = [&]() -> ParseResult<void> {
        writer.WriteStringField("id", te.id);
        writer.WriteIntField("x", te.block_position[0]);
        writer.WriteIntField("y", te.block_position[1]);
        writer.WriteIntField("z", te.block_position[2]);
        return internal::FilterAndWriteTileEntityFields(
            te.raw_nbt, internal::kTeSkip, writer);
      };

      if (version == 7) {
        // v7: wrap all fields in "components" compound.
        writer.BeginCompoundField("components");
        auto res = write_te_body();
        if (!res) return std::unexpected(res.error());
        writer.EndCompoundField();
      } else {
        // v5/v6: flat layout, no "components" wrapper.
        auto res = write_te_body();
        if (!res) return std::unexpected(res.error());
      }

      writer.EndListElementCompound();
    }

    writer.EndListField();

    writer.BeginListField(
        "Entities", base::TagType::Compound, reg.entities.size());

    for (const auto& ent : reg.entities) {
      writer.BeginListElementCompound();
      writer.WriteStringField("id", ent.id);

      writer.BeginListField("Pos", base::TagType::Double, 3);
      writer.WriteListElementDouble(ent.position[0]);
      writer.WriteListElementDouble(ent.position[1]);
      writer.WriteListElementDouble(ent.position[2]);
      writer.EndListField();

      writer.BeginListField("Motion", base::TagType::Double, 3);
      writer.WriteListElementDouble(ent.motion[0]);
      writer.WriteListElementDouble(ent.motion[1]);
      writer.WriteListElementDouble(ent.motion[2]);
      writer.EndListField();

      writer.BeginListField("Rotation", base::TagType::Float, 2);
      writer.WriteListElementFloat(ent.rotation[0]);
      writer.WriteListElementFloat(ent.rotation[1]);
      writer.EndListField();

      {
        auto res =
            internal::FilterAndWriteFields(ent.raw_nbt, internal::kEntSkip, writer);
        if (!res) return std::unexpected(res.error());
      }

      writer.EndListElementCompound();
    }
    writer.EndListField();

    writer.BeginListField("PendingBlockTicks",
                          base::TagType::Compound,
                          reg.pending_block_ticks.size());
    for (const auto& tick : reg.pending_block_ticks) {
      writer.BeginListElementCompound();
      writer.WriteStringField("Block", tick.block);
      writer.WriteIntField("x", tick.pos[0]);
      writer.WriteIntField("y", tick.pos[1]);
      writer.WriteIntField("z", tick.pos[2]);
      writer.WriteLongField("SubTick", tick.sub_tick);
      writer.WriteIntField("Priority", tick.priority);
      writer.WriteIntField("Time", tick.time);
      writer.EndListElementCompound();
    }
    writer.EndListField();

    writer.BeginListField("PendingFluidTicks",
                          base::TagType::Compound,
                          reg.pending_fluid_ticks.size());
    for (const auto& tick : reg.pending_fluid_ticks) {
      writer.BeginListElementCompound();
      writer.WriteStringField("Fluid", tick.block);
      writer.WriteIntField("x", tick.pos[0]);
      writer.WriteIntField("y", tick.pos[1]);
      writer.WriteIntField("z", tick.pos[2]);
      writer.WriteLongField("SubTick", tick.sub_tick);
      writer.WriteIntField("Priority", tick.priority);
      writer.WriteIntField("Time", tick.time);
      writer.EndListElementCompound();
    }
    writer.EndListField();

    writer.EndCompoundField();  // End Region
  }
  writer.EndCompoundField();
  writer.EndRootCompound();

  return std::move(writer).Finalize();
}

ParseResult<Schema> LitematicaHandler::DecodeFromParsed(
    litematic::Litematic&& src) const {
  Schema ir;
  ir.source_format = SourceFormat::kLitematica;
  ir.source_version = static_cast<std::int32_t>(src.version);
  ir.data_version = src.data_version;
  ir.arena = std::move(src.arena);
  ir.owner = std::move(src.owner);

  ir.metadata.name = src.metadata.name;
  ir.metadata.author = src.metadata.author;
  ir.metadata.description = src.metadata.description;
  ir.metadata.region_count = src.metadata.region_count;
  ir.metadata.total_blocks = src.metadata.total_blocks;
  ir.metadata.total_volume = src.metadata.total_volume;
  ir.metadata.enclosing_size = src.metadata.enclosing_size;
  ir.metadata.time_created = src.metadata.time_created;
  ir.metadata.time_modified = src.metadata.time_modified;
  ir.metadata.preview_data = src.metadata.preview_data;

  ir.regions.reserve(src.regions.size());
  for (auto& reg : src.regions) {
    Region r;
    r.name = reg.name;
    for (int i = 0; i < 3; ++i) {
      if (reg.size[i] < 0) {
        r.bounds.origin[i] = reg.position[i] + reg.size[i];
        r.bounds.size[i] = -reg.size[i];
      } else {
        r.bounds.origin[i] = reg.position[i];
        r.bounds.size[i] = reg.size[i];
      }
    }
    r.palette.reserve(reg.palette.size());
    for (auto& bs : reg.palette) {
      BlockState out;
      out.name = bs.name;
      out.raw_properties = bs.properties;
      out.prop_encoding = PropertyEncoding::kNbt;
      r.palette.push_back(std::move(out));
    }
    r.block_indices = std::move(reg.block_indices);
    r.index_order = IndexOrder::kYzx;

    r.entities.reserve(reg.entities.size());
    for (auto& ent : reg.entities) {
      Entity e;
      e.id = ent.id;
      e.position = ent.position;
      e.motion = ent.motion;
      e.rotation = ent.rotation;
      e.raw_nbt = ent.raw_nbt;
      r.entities.push_back(std::move(e));
    }

    r.block_entities.reserve(reg.tile_entities.size());
    for (auto& te : reg.tile_entities) {
      BlockEntity be;
      be.id = te.id;
      be.block_position = te.block_position;
      be.raw_nbt = te.raw_nbt;
      r.block_entities.push_back(std::move(be));
    }

    r.pending_block_ticks.reserve(reg.pending_block_ticks.size());
    for (auto& tick : reg.pending_block_ticks) {
      PendingTick t;
      t.block = tick.block;
      t.pos = tick.pos;
      t.sub_tick = tick.sub_tick;
      t.priority = tick.priority;
      t.time = tick.time;
      r.pending_block_ticks.push_back(std::move(t));
    }

    r.pending_fluid_ticks.reserve(reg.pending_fluid_ticks.size());
    for (auto& tick : reg.pending_fluid_ticks) {
      PendingTick t;
      t.block = tick.block;
      t.pos = tick.pos;
      t.sub_tick = tick.sub_tick;
      t.priority = tick.priority;
      t.time = tick.time;
      r.pending_fluid_ticks.push_back(std::move(t));
    }

    r.pending_block_entities_raw = reg.pending_block_entities;
    r.pending_entities_raw = reg.pending_entities;

    ir.regions.push_back(std::move(r));
  }
  return ir;
}

}  // namespace fschema::ir::format