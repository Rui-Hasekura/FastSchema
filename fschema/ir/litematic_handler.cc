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

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "fschema/base/nbt_writer.h"
#include "fschema/ir/internal/bit_pack.h"
#include "fschema/ir/internal/codec_utils.h"
#include "fschema/ir/materialize.h"
#include "fschema/litematic/parse.h"

#if defined(__AVX2__)
#  include <immintrin.h>
#endif

namespace fschema::ir::format {

namespace {

inline bool ShouldUseExtensions(const Schema& ir, std::int32_t target_version) {
  if (ir.source_format != SourceFormat::kLitematica) return false;
  const std::int32_t effective_version =
      (target_version == 0) ? ir.source_version : target_version;
  return effective_version == ir.source_version;
}

[[nodiscard]] inline std::uint32_t PaletteBpb(std::size_t palette_size) {
  std::uint32_t bpb =
      palette_size <= 4
          ? 2
          : static_cast<std::uint32_t>(std::bit_width(palette_size - 1));
  return bpb < 2 ? 2 : bpb;
}

}  // namespace

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
  std::size_t nbt_hint = 0;
  for (const auto& reg : ir.regions) {
    nbt_hint += static_cast<std::size_t>(reg.bounds.size[0]) *
                static_cast<std::size_t>(reg.bounds.size[1]) *
                static_cast<std::size_t>(reg.bounds.size[2]) * 2;
    nbt_hint += reg.palette.size() * 128;
    nbt_hint += reg.block_entities.size() * 256;
    nbt_hint += reg.entities.size() * 256;
  }
  nbt_hint += 4096;
  base::NbtWriter writer(nbt_hint);

  // Determine target version
  std::int32_t version = 5;
  if (options.target_version >= 5 && options.target_version <= 7) {
    version = options.target_version;
  } else if (ir.data_version >= kDataVersion1205) {
    version = 7;
  } else if (ir.data_version >= kDataVersion118) {
    version = 6;
  }

  const bool use_ext = ShouldUseExtensions(ir, options.target_version);

  writer.BeginRootCompound("Litematic");
  writer.WriteIntField("Version", version);
  if (version >= 7) {
    writer.WriteIntField("SubVersion", 1);
  }
  writer.WriteIntField("MinecraftDataVersion", ir.data_version);

  // Compute common values for cross-format path
  std::int32_t total_blocks = 0;
  std::int64_t total_volume = 0;
  if (!use_ext) {
    for (const auto& reg : ir.regions) {
      total_volume += static_cast<std::int64_t>(std::abs(reg.bounds.size[0])) *
                      static_cast<std::int64_t>(std::abs(reg.bounds.size[1])) *
                      static_cast<std::int64_t>(std::abs(reg.bounds.size[2]));
      auto mat_res = ir::EnsureMaterialized(reg, *ir.arena);
      if (!mat_res) return std::unexpected(mat_res.error());

      if (reg.block_indices.empty()) continue;
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
          total_blocks += is_air[idx] ^ 1;
        }
      }
    }
  }

  // Metadata
  std::int64_t ms = absl::ToUnixMillis(absl::Now());
  std::string_view name =
      ir.metadata.name.empty() ? "Unnamed" : ir.metadata.name;
  std::string_view author =
      ir.metadata.author.empty() ? "Unknown" : ir.metadata.author;

  writer.BeginCompoundField("Metadata");
  writer.WriteStringField("Name", name);
  writer.WriteStringField("Author", author);

  if (use_ext) {
    // Round-trip: write extensions
    // (Description, RegionCount, TotalBlocks, etc.)
    if (!ir.metadata.raw_compound.empty()) {
      auto res = internal::FilterAndWriteFields(
          ir.metadata.raw_compound, internal::kLitMetadataSkip, writer);
      if (!res) return std::unexpected(res.error());
    }
  } else {
    // Cross-format: compute all format-specific fields
    writer.WriteStringField("Description", "");
    writer.WriteIntField("RegionCount",
                         static_cast<std::int32_t>(ir.regions.size()));
    writer.WriteIntField("TotalBlocks", total_blocks);
    writer.WriteIntField("TotalVolume", total_volume);

    writer.BeginCompoundField("EnclosingSize");
    std::array<std::int32_t, 3> enc_size = {0, 0, 0};
    if (!ir.regions.empty()) {
      for (int i = 0; i < 3; ++i) {
        enc_size[i] = std::abs(ir.regions[0].bounds.size[i]);
      }
    }
    writer.WriteIntField("x", enc_size[0]);
    writer.WriteIntField("y", enc_size[1]);
    writer.WriteIntField("z", enc_size[2]);
    writer.EndCompoundField();

    writer.WriteLongField("TimeCreated", ms);
    writer.WriteLongField("TimeModified", ms);
  }
  writer.EndCompoundField();

  // Regions
  writer.BeginCompoundField("Regions");
  for (const auto& reg : ir.regions) {
    writer.BeginCompoundField(reg.name);

    // Position
    writer.BeginCompoundField("Position");
    writer.WriteIntField("x", reg.bounds.origin[0]);
    writer.WriteIntField("y", reg.bounds.origin[1]);
    writer.WriteIntField("z", reg.bounds.origin[2]);
    writer.EndCompoundField();

    // Size
    writer.BeginCompoundField("Size");
    writer.WriteIntField("x", reg.bounds.size[0]);
    writer.WriteIntField("y", reg.bounds.size[1]);
    writer.WriteIntField("z", reg.bounds.size[2]);
    writer.EndCompoundField();

    // Palette & index preparation
    std::vector<ir::BlockState> out_palette;
    std::unique_ptr<std::uint16_t[]> remapped;
    std::span<const std::uint16_t> pack_src;

    std::uint16_t air_idx = kAirIdxSentinel;
    for (std::size_t i = 0; i < reg.palette.size(); ++i) {
      if (reg.palette[i].name == "minecraft:air" ||
          reg.palette[i].name == "minecraft:cave_air" ||
          reg.palette[i].name == "minecraft:void_air") {
        air_idx = static_cast<std::uint16_t>(i);
        break;
      }
    }

    const bool block_states_passthrough =
        reg.lazy_source.encoding ==
            ir::BlockDataEncoding::kLitematicaLongArray &&
        reg.lazy_source.palette_pristine && reg.lazy_source.air_at_zero &&
        PaletteBpb(reg.palette.size()) == reg.lazy_source.bits_per_block &&
        !reg.lazy_source.raw_bytes.empty();

    if (block_states_passthrough) {
      out_palette.assign(reg.palette.begin(), reg.palette.end());
    } else if (air_idx == 0) {
      auto mat_res = ir::EnsureMaterialized(reg, *ir.arena);
      if (!mat_res) return std::unexpected(mat_res.error());
      out_palette.assign(reg.palette.begin(), reg.palette.end());
      pack_src = std::span<const std::uint16_t>(reg.block_indices.data(),
                                                reg.block_indices.size());
    } else if (air_idx != kAirIdxSentinel) {
      auto mat_res = ir::EnsureMaterialized(reg, *ir.arena);
      if (!mat_res) return std::unexpected(mat_res.error());

      out_palette.assign(reg.palette.begin(), reg.palette.end());
      std::swap(out_palette[0], out_palette[air_idx]);
      const std::size_t n = reg.block_indices.size();
      remapped = std::make_unique_for_overwrite<std::uint16_t[]>(n);
      const std::uint16_t* src = reg.block_indices.data();
      std::uint16_t* dst = remapped.get();

#if defined(__AVX2__)
      const __m256i v_zero = _mm256_setzero_si256();
      const __m256i v_air = _mm256_set1_epi16(
          static_cast<short>(static_cast<std::uint16_t>(air_idx)));
      std::size_t i = 0;
      for (; i + 16 <= n; i += 16) {
        __m256i v =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
        __m256i is0 = _mm256_cmpeq_epi16(v, v_zero);
        __m256i isAir = _mm256_cmpeq_epi16(v, v_air);
        __m256i r = _mm256_blendv_epi8(v, v_air, is0);
        r = _mm256_blendv_epi8(r, v_zero, isAir);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), r);
      }
      // Scalar tail
      for (; i < n; ++i) {
        std::uint16_t idx = src[i];
        dst[i] = (idx == 0)         ? air_idx
                 : (idx == air_idx) ? static_cast<std::uint16_t>(0)
                                    : idx;
      }
#else
      for (std::size_t i = 0; i < n; ++i) {
        std::uint16_t idx = src[i];
        dst[i] = (idx == 0)         ? air_idx
                 : (idx == air_idx) ? static_cast<std::uint16_t>(0)
                                    : idx;
      }
#endif
      pack_src = std::span<const std::uint16_t>(dst, n);
    } else {
      auto mat_res = ir::EnsureMaterialized(reg, *ir.arena);
      if (!mat_res) return std::unexpected(mat_res.error());

      if (reg.palette.size() >= 65536) {
        return std::unexpected(ParseError{
            ParseError::Code::OversizedPayload, "PaletteOverflow", 0});
      }
      out_palette.push_back({"minecraft:air", {}, PropertyEncoding::kNone});
      for (const auto& bs : reg.palette) out_palette.push_back(bs);
      const std::size_t n = reg.block_indices.size();
      remapped = std::make_unique_for_overwrite<std::uint16_t[]>(n);
      const std::uint16_t* src = reg.block_indices.data();
      // +1 shift; auto-vectorizes (u16 add).
      for (std::size_t i = 0; i < n; ++i) {
        remapped[i] = static_cast<std::uint16_t>(src[i] + 1);
      }
      pack_src = std::span<const std::uint16_t>(remapped.get(), n);
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

    const std::uint32_t bpb = PaletteBpb(out_palette.size());

    // BlockStates
    if (block_states_passthrough) {
      writer.WriteLongArrayFieldBE("BlockStates", reg.lazy_source.raw_bytes);
    } else {
      const std::size_t packed_bytes =
          internal::LitematicLongCount(pack_src.size(), bpb) * 8;
      auto packed = std::make_unique_for_overwrite<std::byte[]>(packed_bytes);
      internal::PackIndicesLitematicInto(packed.get(), pack_src, bpb);
      writer.WriteLongArrayFieldBE(
          "BlockStates",
          std::span<const std::byte>(packed.get(), packed_bytes));
    }

    // TileEntities (common)
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
            te.raw_nbt, internal::kBlockEntitySkip, writer);
      };
      if (version == 7) {
        writer.BeginCompoundField("components");
        auto res = write_te_body();
        if (!res) return std::unexpected(res.error());
        writer.EndCompoundField();
      } else {
        auto res = write_te_body();
        if (!res) return std::unexpected(res.error());
      }
      writer.EndListElementCompound();
    }
    writer.EndListField();

    // Entities (common)
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
      auto res = internal::FilterAndWriteFields(
          ent.raw_nbt, internal::kEntSkip, writer);
      if (!res) return std::unexpected(res.error());
      writer.EndListElementCompound();
    }
    writer.EndListField();

    // Format-specific region fields
    if (use_ext) {
      // Round-trip: write extensions
      // (PendingBlockTicks, PendingFluidTicks, etc.)
      if (!reg.raw_compound.empty()) {
        auto res = internal::FilterAndWriteFields(
            reg.raw_compound, internal::kLitRegionSkip, writer);
        if (!res) return std::unexpected(res.error());
      }
    } else {
      // Cross-format: write empty pending tick lists
      writer.BeginListField("PendingBlockTicks", base::TagType::Compound, 0);
      writer.EndListField();
      writer.BeginListField("PendingFluidTicks", base::TagType::Compound, 0);
      writer.EndListField();
    }

    writer.EndCompoundField();
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

  // Common metadata
  ir.metadata.name = src.metadata.name;
  ir.metadata.author = src.metadata.author;
  ir.metadata.raw_compound = src.metadata.raw_compound;

  // Regions
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
    r.lazy_source.raw_bytes = reg.raw_block_states;
    r.lazy_source.encoding = ir::BlockDataEncoding::kLitematicaLongArray;
    r.lazy_source.bits_per_block = reg.raw_block_states_bpb;
    r.lazy_source.palette_size = reg.palette.size();
    r.lazy_source.air_at_zero =
        !reg.palette.empty() && (reg.palette[0].name == "minecraft:air" ||
                                 reg.palette[0].name == "minecraft:cave_air" ||
                                 reg.palette[0].name == "minecraft:void_air");
    r.lazy_source.palette_pristine = true;
    r.entities.reserve(reg.entities.size());
    for (auto& te : reg.tile_entities) {
      BlockEntity be;
      be.id = te.id;
      be.block_position = te.block_position;
      be.raw_nbt = te.raw_nbt;
      r.block_entities.push_back(std::move(be));
    }
    r.raw_compound = reg.raw_compound;

    ir.regions.push_back(std::move(r));
  }
  return ir;
}

}  // namespace fschema::ir::format