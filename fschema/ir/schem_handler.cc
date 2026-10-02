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

#include "fschema/ir/schem_handler.h"

#include <array>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fschema/base/nbt_writer.h"
#include "fschema/ir/internal/codec_utils.h"
#include "fschema/ir/internal/varint_pack.h"
#include "fschema/ir/manipulate.h"
#include "fschema/ir/materialize.h"
#include "fschema/schem/parse.h"

namespace fschema::ir::format {

namespace {

inline bool ShouldUseExtensions(const Schema& ir, bool is_v3) {
  if (ir.source_format != SourceFormat::kSchem) return false;
  return (is_v3 == (ir.source_version == 3));
}

}  // namespace

ParseResult<Schema> SchemHandler::Decode(
    std::unique_ptr<std::vector<std::byte>> bytes,
    const base::DecodeLimits& limits) const {
  auto schem = schem::ParseSchematicFromBytes(std::move(bytes), limits);
  if (!schem) return std::unexpected(schem.error());
  return DecodeFromParsed(std::move(*schem));
}

ParseResult<std::vector<std::byte>> SchemHandler::Encode(
    const Schema& ir,
    const EncodeOptions& options) const {
  const Region* target_region = nullptr;
  std::unique_ptr<Region> merged_region;
  std::unique_ptr<memory::Arena> merge_arena;

  if (ir.regions.empty()) {
    return std::unexpected(
        ParseError{ParseError::Code::MissingField, "Regions", 0});
  }
  if (ir.regions.size() == 1) {
    target_region = &ir.regions[0];
  } else {
    switch (options.multi_region) {
      case EncodeOptions::MultiRegionStrategy::kStrictSingle:
        return std::unexpected(
            ParseError{ParseError::Code::UnsupportedVersion, "MultiRegion", 0});
      case EncodeOptions::MultiRegionStrategy::kExtractFirst:
        target_region = &ir.regions[0];
        break;
      case EncodeOptions::MultiRegionStrategy::kExtractByName:
        for (const auto& r : ir.regions) {
          if (r.name == options.extract_region_name) {
            target_region = &r;
            break;
          }
        }
        if (!target_region)
          return std::unexpected(
              ParseError{ParseError::Code::MissingField, "ExtractRegion", 0});
        break;
      case EncodeOptions::MultiRegionStrategy::kMergeBoundingBox: {
        merge_arena = std::make_unique<memory::Arena>();
        auto merge_res =
            MergeRegions(ir.regions, options.fill_block, *merge_arena);
        if (!merge_res) return std::unexpected(merge_res.error());
        merged_region = std::make_unique<Region>(std::move(*merge_res));
        target_region = merged_region.get();
        break;
      }
    }
  }

  if (options.target_version != 0 && options.target_version != 2 &&
      options.target_version != 3) {
    return std::unexpected(
        ParseError{ParseError::Code::UnsupportedVersion, "TargetVersion", 0});
  }
  bool is_v3 = (options.target_version == 0 || options.target_version == 3);
  bool use_ext = ShouldUseExtensions(ir, is_v3);

  std::size_t nbt_hint = 0;
  nbt_hint += target_region->palette.size() * 128;
  nbt_hint += internal::VarintUpperBound(
      static_cast<std::size_t>(target_region->bounds.size[0]) *
          static_cast<std::size_t>(target_region->bounds.size[1]) *
          static_cast<std::size_t>(target_region->bounds.size[2]),
      target_region->palette.size());
  nbt_hint += target_region->block_entities.size() * 256;
  nbt_hint += target_region->entities.size() * 256;
  nbt_hint += 4096;

  base::NbtWriter writer(nbt_hint);

  if (is_v3) {
    writer.BeginRootCompound("");
    writer.BeginCompoundField("Schematic");
  } else {
    writer.BeginRootCompound("Schematic");
  }

  writer.WriteIntField("Version", is_v3 ? 3 : 2);
  writer.WriteIntField("DataVersion", ir.data_version);

  // Metadata
  writer.BeginCompoundField("Metadata");
  if (!ir.metadata.name.empty())
    writer.WriteStringField("Name", ir.metadata.name);
  if (!ir.metadata.author.empty())
    writer.WriteStringField("Author", ir.metadata.author);

  if (use_ext) {
    if (!ir.metadata.raw_compound.empty()) {
      auto res = internal::FilterAndWriteFields(
          ir.metadata.raw_compound, internal::kSchemMetadataSkip, writer);
      if (!res) return std::unexpected(res.error());
    }
  }
  writer.EndCompoundField();

  // Dimensions
  auto check_short = [](std::int32_t val) -> ParseResult<void> {
    if (val < std::numeric_limits<std::int16_t>::min() ||
        val > std::numeric_limits<std::int16_t>::max()) {
      return std::unexpected(ParseError{
          ParseError::Code::OversizedPayload, "DimensionOverflow", 0});
    }
    return {};
  };
  if (auto r = check_short(target_region->bounds.size[0]); !r)
    return std::unexpected(r.error());
  if (auto r = check_short(target_region->bounds.size[1]); !r)
    return std::unexpected(r.error());
  if (auto r = check_short(target_region->bounds.size[2]); !r)
    return std::unexpected(r.error());

  writer.WriteShortField(
      "Width", static_cast<std::int16_t>(target_region->bounds.size[0]));
  writer.WriteShortField(
      "Height", static_cast<std::int16_t>(target_region->bounds.size[1]));
  writer.WriteShortField(
      "Length", static_cast<std::int16_t>(target_region->bounds.size[2]));
  writer.WriteIntArrayField(
      "Offset", std::span<const std::int32_t>(target_region->bounds.origin, 3));

  // Blocks
  // (v3 container or v2 flat)
  if (is_v3) {
    writer.BeginCompoundField("Blocks");
  }

  // Palette
  writer.BeginCompoundField("Palette");
  for (std::size_t i = 0; i < target_region->palette.size(); ++i) {
    const auto& bs = target_region->palette[i];
    std::string blockstate_str(bs.name);
    if (bs.prop_encoding == PropertyEncoding::kString &&
        !bs.raw_properties.empty()) {
      blockstate_str += "[";
      blockstate_str.append(
          reinterpret_cast<const char*>(bs.raw_properties.data()),
          bs.raw_properties.size());
      blockstate_str += "]";
    } else if (bs.prop_encoding == PropertyEncoding::kNbt &&
               !bs.raw_properties.empty()) {
      blockstate_str += "[";
      blockstate_str += internal::NbtPropsToString(bs.raw_properties);
      blockstate_str += "]";
    }
    writer.WriteIntField(blockstate_str, static_cast<std::int32_t>(i));
  }
  writer.EndCompoundField();

  bool raw_emit_eligible = target_region->lazy_source.encoding ==
                               ir::BlockDataEncoding::kSpongeVarint &&
                           target_region->lazy_source.palette_pristine &&
                           !target_region->lazy_source.raw_bytes.empty();

  if (raw_emit_eligible) {
    writer.WriteByteArrayField(
        is_v3 ? "Data" : "BlockData",
        std::span<const std::int8_t>(
            reinterpret_cast<const std::int8_t*>(
                target_region->lazy_source.raw_bytes.data()),
            target_region->lazy_source.raw_bytes.size()));
  } else {
    auto mat_res = ir::EnsureMaterialized(*target_region, *ir.arena);
    if (!mat_res) return std::unexpected(mat_res.error());

    const std::size_t n = target_region->block_indices.size();
    const std::size_t pal = target_region->palette.size();
    const std::size_t bound = internal::VarintUpperBound(n, pal);
    auto block_data = std::make_unique_for_overwrite<std::byte[]>(bound);
    const std::size_t written = internal::PackVarintInto(
        block_data.get(),
        std::span<const std::uint16_t>(target_region->block_indices.data(), n),
        pal);

    writer.WriteByteArrayField(
        is_v3 ? "Data" : "BlockData",
        std::span<const std::int8_t>(
            reinterpret_cast<const std::int8_t*>(block_data.get()), written));
  }

  // Block entities
  writer.BeginListField("BlockEntities",
                        base::TagType::Compound,
                        target_region->block_entities.size());
  for (const auto& be : target_region->block_entities) {
    writer.BeginListElementCompound();
    writer.WriteStringField("Id", be.id);
    writer.WriteIntArrayField(
        "Pos", std::span<const std::int32_t>(be.block_position.data(), 3));
    if (is_v3) {
      writer.BeginCompoundField("Data");
      auto res = internal::FilterAndWriteFields(
          be.raw_nbt, internal::kBlockEntitySkip, writer);
      if (!res) return std::unexpected(res.error());
      writer.EndCompoundField();
    } else {
      auto res = internal::FilterAndWriteFields(
          be.raw_nbt, internal::kBlockEntitySkip, writer);
      if (!res) return std::unexpected(res.error());
    }
    writer.EndListElementCompound();
  }
  writer.EndListField();

  if (is_v3) {
    writer.EndCompoundField();  // END Blocks
  }

  // Region extensions / Biomes
  if (use_ext) {
    if (!target_region->raw_compound.empty()) {
      auto skip =
          is_v3 ? std::span<const std::string_view>(internal::kSchemV3Skip)
                : std::span<const std::string_view>(internal::kSchemV2Skip);
      auto res = internal::FilterAndWriteFields(
          target_region->raw_compound, skip, writer);
      if (!res) return std::unexpected(res.error());
    }
  }

  // Entities
  writer.BeginListField(
      "Entities", base::TagType::Compound, target_region->entities.size());
  for (const auto& ent : target_region->entities) {
    writer.BeginListElementCompound();
    writer.WriteStringField("Id", ent.id);
    writer.BeginListField("Pos", base::TagType::Double, 3);
    writer.WriteListElementDouble(ent.position[0]);
    writer.WriteListElementDouble(ent.position[1]);
    writer.WriteListElementDouble(ent.position[2]);
    writer.EndListField();
    if (is_v3) {
      writer.BeginCompoundField("Data");
    }
    writer.BeginListField("Motion", base::TagType::Double, 3);
    writer.WriteListElementDouble(ent.motion[0]);
    writer.WriteListElementDouble(ent.motion[1]);
    writer.WriteListElementDouble(ent.motion[2]);
    writer.EndListField();
    writer.BeginListField("Rotation", base::TagType::Float, 2);
    writer.WriteListElementFloat(ent.rotation[0]);
    writer.WriteListElementFloat(ent.rotation[1]);
    writer.EndListField();
    auto res =
        internal::FilterAndWriteFields(ent.raw_nbt, internal::kEntSkip, writer);
    if (!res) return std::unexpected(res.error());
    if (is_v3) {
      writer.EndCompoundField();
    }
    writer.EndListElementCompound();
  }
  writer.EndListField();

  if (is_v3) {
    writer.EndCompoundField();  // END Schematic
  }
  writer.EndRootCompound();

  return std::move(writer).Finalize();
}

ParseResult<Schema> SchemHandler::DecodeFromParsed(
    schem::Schematic&& src) const {
  Schema ir;
  ir.source_format = SourceFormat::kSchem;
  ir.source_version = static_cast<std::int32_t>(src.version);
  ir.data_version = src.data_version;
  ir.arena = std::move(src.arena);
  ir.owner = std::move(src.owner);

  // Common metadata
  ir.metadata.name = src.metadata.name;
  ir.metadata.author = src.metadata.author;
  ir.metadata.raw_compound = src.metadata.raw_compound;

  // Region
  Region r;
  r.name = "main";
  r.bounds.origin[0] = 0;
  r.bounds.origin[1] = 0;
  r.bounds.origin[2] = 0;
  r.bounds.size[0] = static_cast<std::int32_t>(src.width);
  r.bounds.size[1] = static_cast<std::int32_t>(src.height);
  r.bounds.size[2] = static_cast<std::int32_t>(src.length);

  r.palette.reserve(src.palette.size());
  for (auto& bs : src.palette) {
    BlockState out;
    out.name = bs.name;
    out.raw_properties = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(bs.properties.data()),
        bs.properties.size());
    out.prop_encoding = PropertyEncoding::kString;
    r.palette.push_back(std::move(out));
  }

  r.lazy_source.raw_bytes = src.raw_block_data;
  r.lazy_source.encoding = ir::BlockDataEncoding::kSpongeVarint;
  r.lazy_source.palette_size = src.palette.size();
  r.lazy_source.air_at_zero = true;
  r.lazy_source.palette_pristine = true;

  r.entities.reserve(src.entities.size());
  for (auto& ent : src.entities) {
    Entity e;
    e.id = ent.id;
    e.position = ent.pos;
    if (src.version == schem::Version::kV2) {
      e.position[0] -= static_cast<double>(src.offset[0]);
      e.position[1] -= static_cast<double>(src.offset[1]);
      e.position[2] -= static_cast<double>(src.offset[2]);
    }
    e.motion = ent.motion;
    e.rotation = ent.rotation;
    e.raw_nbt = ent.data;
    r.entities.push_back(std::move(e));
  }

  r.block_entities.reserve(src.block_entities.size());
  for (auto& be : src.block_entities) {
    BlockEntity out;
    out.id = be.id;
    out.block_position = be.pos;
    out.raw_nbt = be.data;
    r.block_entities.push_back(std::move(out));
  }

  r.raw_compound = src.raw_compound;

  ir.regions.push_back(std::move(r));
  return ir;
}

}  // namespace fschema::ir::format