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

#include "fschema/ir/litematica_handler.h"

#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "fschema/base/nbt/writer.h"
#include "fschema/ir/internal/bit_pack.h"
#include "fschema/ir/internal/codec_utils.h"
#include "fschema/ir/materialize.h"
#include "fschema/litematica/parse.h"

#if defined(__AVX2__)
#  include <immintrin.h>
#endif

namespace fschema::ir::format {

namespace {

inline bool ShouldUseExtensions(const Schematic& schematic,
                                std::int32_t target_version) {
  if (schematic.source_format != SourceFormat::kLitematica) return false;
  const std::int32_t effective_version =
      (target_version == 0) ? schematic.source_version : target_version;
  return effective_version == schematic.source_version;
}

[[nodiscard]] inline std::uint32_t PaletteBpb(std::size_t palette_size) {
  std::uint32_t bpb =
      palette_size <= 4
          ? 2
          : static_cast<std::uint32_t>(std::bit_width(palette_size - 1));
  return bpb < 2 ? 2 : bpb;
}

}  // namespace

ParseResult<Schematic> LitematicaHandler::Decode(
    std::unique_ptr<std::vector<std::byte>> bytes,
    const base::DecodeLimits& limits) const {
  auto parsed = litematica::ParseLitematica(std::move(bytes), limits);
  if (!parsed) return std::unexpected(parsed.error());
  return DecodeFromParsed(std::move(*parsed));
}

constexpr std::uint16_t kAirIdxSentinel = 0xFFFF;

ParseResult<std::vector<std::byte>> LitematicaHandler::Encode(
    const Schematic& schematic,
    const EncodeOptions& options) const {
  std::size_t nbt_hint = 0;
  for (const auto& reg : schematic.regions) {
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
  } else if (schematic.data_version >= kDataVersion1205) {
    version = 7;
  } else if (schematic.data_version >= kDataVersion118) {
    version = 6;
  }

  const bool use_ext = ShouldUseExtensions(schematic, options.target_version);

  // Anonymous root (name = ""), to match Litematica mod's on-disk format.
  // Disk: 0A 00 00 ...  (TAG_Compound + u16 name length = 0)

  writer.BeginRootCompound("");
  writer.WriteIntField("Version", version);
  if (version >= 7) {
    writer.WriteIntField("SubVersion", 1);
  }
  writer.WriteIntField("MinecraftDataVersion", schematic.data_version);

  // Compute common values for cross-format path
  std::int32_t total_blocks = 0;
  std::int64_t total_volume = 0;
  if (!use_ext) {
    for (const auto& reg : schematic.regions) {
      total_volume += static_cast<std::int64_t>(std::abs(reg.bounds.size[0])) *
                      static_cast<std::int64_t>(std::abs(reg.bounds.size[1])) *
                      static_cast<std::int64_t>(std::abs(reg.bounds.size[2]));
      auto mat_res = ir::EnsureMaterialized(reg, *schematic.arena);
      if (!mat_res) return std::unexpected(mat_res.error());
      reg.EnsureAirLut();

      if (reg.block_indices.empty()) continue;
      for (std::uint16_t idx : reg.block_indices) {
        if (idx < reg.palette.size()) {
          total_blocks += reg.IsAir(idx) ? 0 : 1;
        }
      }
    }
  }

  // Metadata
  std::int64_t ms = absl::ToUnixMillis(absl::Now());
  std::string_view name =
      schematic.metadata.name.empty() ? "Unnamed" : schematic.metadata.name;
  std::string_view author =
      schematic.metadata.author.empty() ? "Unknown" : schematic.metadata.author;

  writer.BeginCompoundField("Metadata");
  writer.WriteStringField("Name", name);
  writer.WriteStringField("Author", author);

  if (use_ext) {
    // Round-trip: write extensions
    // (Description, RegionCount, TotalBlocks, etc.)
    if (!schematic.metadata.raw_compound.empty()) {
      auto res = internal::FilterAndWriteFields(
          schematic.metadata.raw_compound, internal::kLitMetadataSkip, writer);
      if (!res) return std::unexpected(res.error());
    }
  } else {
    // Cross-format: compute all format-specific fields
    writer.WriteStringField("Description", "");
    writer.WriteIntField("RegionCount",
                         static_cast<std::int32_t>(schematic.regions.size()));
    writer.WriteIntField("TotalBlocks", total_blocks);
    writer.WriteIntField("TotalVolume", total_volume);

    writer.BeginCompoundField("EnclosingSize");
    std::array<std::int32_t, 3> enc_size = {0, 0, 0};
    if (!schematic.regions.empty()) {
      for (int i = 0; i < 3; ++i) {
        enc_size[i] = std::abs(schematic.regions[0].bounds.size[i]);
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
  for (const auto& reg : schematic.regions) {
    writer.BeginCompoundField(reg.name);

    // Position
    writer.BeginCompoundField("Position");
    writer.WriteIntField("x", reg.raw_position[0]);
    writer.WriteIntField("y", reg.raw_position[1]);
    writer.WriteIntField("z", reg.raw_position[2]);
    writer.EndCompoundField();

    // Size
    writer.BeginCompoundField("Size");
    writer.WriteIntField("x", reg.raw_size[0]);
    writer.WriteIntField("y", reg.raw_size[1]);
    writer.WriteIntField("z", reg.raw_size[2]);
    writer.EndCompoundField();

    // Palette & index preparation
    std::vector<ir::BlockState> out_palette;
    std::unique_ptr<std::uint16_t[]> remapped;
    std::span<const std::uint16_t> pack_src;

    std::uint16_t air_idx = kAirIdxSentinel;
    for (std::size_t i = 0; i < reg.palette.size(); ++i) {
      if (base::IsAirVariant(reg.palette[i].name)) {
        air_idx = static_cast<std::uint16_t>(i);
        break;
      }
    }

    // Passthrough: reuse the raw Litematica long-array when nothing about
    // the region requires repacking. Requires a Litematica long-array
    // encoding, a pristine palette whose slot 0 is air, matching
    // bits-per-block, no edits, and non-empty raw bytes.
    const bool block_states_passthrough =
        reg.lazy_source.encoding ==
            ir::BlockDataEncoding::kLitematicaLongArray &&
        reg.lazy_source.palette_pristine && reg.lazy_source.air_at_zero &&
        PaletteBpb(reg.palette.size()) == reg.lazy_source.bits_per_block &&
        !reg.edited && !reg.lazy_source.raw_bytes.empty();

    if (block_states_passthrough) {
      out_palette.assign(reg.palette.begin(), reg.palette.end());
    } else if (air_idx == 0) {
      auto mat_res = ir::EnsureMaterialized(reg, *schematic.arena);
      if (!mat_res) return std::unexpected(mat_res.error());
      out_palette.assign(reg.palette.begin(), reg.palette.end());
      pack_src = std::span<const std::uint16_t>(reg.block_indices.data(),
                                                reg.block_indices.size());
    } else if (air_idx != kAirIdxSentinel) {
      auto mat_res = ir::EnsureMaterialized(reg, *schematic.arena);
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
      auto mat_res = ir::EnsureMaterialized(reg, *schematic.arena);
      if (!mat_res) return std::unexpected(mat_res.error());

      if (reg.palette.size() >= 65536) {
        return std::unexpected(ParseError{
            ParseError::Code::kOversizedPayload, "PaletteOverflow", 0});
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
        "BlockStatePalette", base::TagType::kCompound, out_palette.size());
    for (const auto& bs : out_palette) {
      writer.BeginListElementCompound();
      writer.WriteStringField("Name", bs.name);
      if (bs.prop_encoding == PropertyEncoding::kNbt &&
          !bs.raw_properties.empty()) {
        writer.WriteRawField(
            "Properties", base::TagType::kCompound, bs.raw_properties);
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
          internal::LitematicaLongCount(pack_src.size(), bpb) * 8;
      auto packed = std::make_unique_for_overwrite<std::byte[]>(packed_bytes);

      [[maybe_unused]] const std::size_t written =
          internal::PackIndicesLitematicaInto(packed.get(), pack_src, bpb);
      assert(written == packed_bytes);

      writer.WriteLongArrayFieldBE(
          "BlockStates",
          std::span<const std::byte>(packed.get(), packed_bytes));
    }

    // TileEntities (common)
    writer.BeginListField(
        "TileEntities", base::TagType::kCompound, reg.block_entities.size());
    for (const auto& te : reg.block_entities) {
      writer.BeginListElementCompound();
      auto write_te_body = [&]() -> ParseResult<void> {
        writer.WriteStringField("id", te.id);
        writer.WriteIntField("x", te.block_position[0] - reg.bounds.origin[0]);
        writer.WriteIntField("y", te.block_position[1] - reg.bounds.origin[1]);
        writer.WriteIntField("z", te.block_position[2] - reg.bounds.origin[2]);
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
        "Entities", base::TagType::kCompound, reg.entities.size());
    for (const auto& ent : reg.entities) {
      writer.BeginListElementCompound();
      writer.WriteStringField("id", ent.id);
      writer.BeginListField("Pos", base::TagType::kDouble, 3);
      writer.WriteListElementDouble(ent.position[0] -
                                    static_cast<double>(reg.raw_position[0]));
      writer.WriteListElementDouble(ent.position[1] -
                                    static_cast<double>(reg.raw_position[1]));
      writer.WriteListElementDouble(ent.position[2] -
                                    static_cast<double>(reg.raw_position[2]));
      writer.EndListField();
      writer.BeginListField("Motion", base::TagType::kDouble, 3);
      writer.WriteListElementDouble(ent.motion[0]);
      writer.WriteListElementDouble(ent.motion[1]);
      writer.WriteListElementDouble(ent.motion[2]);
      writer.EndListField();
      writer.BeginListField("Rotation", base::TagType::kFloat, 2);
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
      writer.BeginListField("PendingBlockTicks", base::TagType::kCompound, 0);
      writer.EndListField();
      writer.BeginListField("PendingFluidTicks", base::TagType::kCompound, 0);
      writer.EndListField();
    }

    writer.EndCompoundField();
  }
  writer.EndCompoundField();
  writer.EndRootCompound();

  return std::move(writer).Finalize();
}

ParseResult<Schematic> LitematicaHandler::DecodeFromParsed(
    litematica::Litematica&& src) const {
  Schematic schematic;
  schematic.source_format = SourceFormat::kLitematica;
  schematic.source_version = static_cast<std::int32_t>(src.version);
  schematic.data_version = src.data_version;
  schematic.arena = std::move(src.arena);
  schematic.owner = std::move(src.owner);

  // Common metadata
  schematic.metadata.name = src.metadata.name;
  schematic.metadata.author = src.metadata.author;
  schematic.metadata.raw_compound = src.metadata.raw_compound;

  // Regions
  schematic.regions.reserve(src.regions.size());
  for (auto& reg : src.regions) {
    Region r;
    r.name = reg.name;
    r.raw_position = reg.position;
    r.raw_size = reg.size;
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
    r.lazy_source.palette_pristine = true;
    r.lazy_source.air_at_zero =
        !reg.palette.empty() && base::IsAirVariant(reg.palette[0].name);
    r.block_entities.reserve(reg.tile_entities.size());
    for (auto& te : reg.tile_entities) {
      BlockEntity be;
      be.id = te.id;
      be.block_position = {
          te.block_position[0] + r.bounds.origin[0],
          te.block_position[1] + r.bounds.origin[1],
          te.block_position[2] + r.bounds.origin[2],
      };
      be.raw_nbt = te.raw_nbt;
      r.block_entities.push_back(std::move(be));
    }

    r.entities.reserve(reg.entities.size());
    for (auto& src_ent : reg.entities) {
      Entity ent;
      ent.id = src_ent.id;
      ent.position = {
          src_ent.position[0] + static_cast<double>(reg.position[0]),
          src_ent.position[1] + static_cast<double>(reg.position[1]),
          src_ent.position[2] + static_cast<double>(reg.position[2]),
      };
      ent.motion = src_ent.motion;
      ent.rotation = src_ent.rotation;
      ent.raw_nbt = src_ent.raw_nbt;
      r.entities.push_back(std::move(ent));
    }

    r.raw_compound = reg.raw_compound;

    schematic.regions.push_back(std::move(r));
  }
  return schematic;
}

}  // namespace fschema::ir::format