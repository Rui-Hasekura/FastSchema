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

#ifndef FSCHEMA_IR_TYPES_H_
#define FSCHEMA_IR_TYPES_H_

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "fschema/base/block_utils.h"
#include "fschema/base/nbt/tag.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"

namespace fschema::ir {

// 2860 is Minecraft Java Edition 1.18's DataVersion.
// Expanded world height (-64 to 320) and chunk Y offsets.
// https://minecraft.wiki/w/Java_Edition_1.18
constexpr std::int32_t kDataVersion118 = 2860;

// 3837 is Minecraft Java Edition 1.20.5's DataVersion.
// Introduced item components (replacing item NBT tags) in this version.
// https://minecraft.wiki/w/Java_Edition_1.20.5
constexpr std::int32_t kDataVersion1205 = 3837;

enum class SourceFormat : std::uint8_t {
  kLitematica = 0,
  kSponge = 1,
};

enum class PropertyEncoding : std::uint8_t {
  kNone = 0,
  kNbt = 1,     // Litematica: Raw NBT Compound
  kString = 2,  // Sponge Schematic: Key-value string ("k=v,k=v")
};

// Represents a unique block state (e.g.,
// "minecraft:oak_stairs[facing=north]").
// Used as an entry in a Region's palette.
struct BlockState {
  std::string_view name;

  // Raw byte representation of the block's properties.
  // The encoding depends on the source format (see `prop_encoding`).
  std::span<const std::byte> raw_properties;

  // Defines how `raw_properties` should be interpreted.
  PropertyEncoding prop_encoding = PropertyEncoding::kNone;
};

struct Entity {
  std::string_view id;
  std::array<double, 3> position{};  // World-space
  std::array<double, 3> motion{};
  std::array<float, 2> rotation{};

  // Includes components and other extra data.
  std::span<const std::byte> raw_nbt;
};

struct BlockEntity {
  std::string_view id;
  std::array<std::int32_t, 3> block_position{};  // World-space
  std::span<const std::byte> raw_nbt;
};

// An axis-aligned 3D bounding box representing a region's spatial extents.
struct BoundingBox {
  // The world-space origin (minimum corner) of the box.
  std::int32_t origin[3]{};

  // The dimensions of the box: size[0]=Width(X),
  //                            size[1]=Height(Y),
  //                            size[2]=Length(Z).
  // Invariant:
  // All elements are strictly non-negative after format normalization.
  // A size of 0 indicates an empty region.
  std::int32_t size[3]{};
};

enum class BlockDataEncoding : std::uint8_t {
  kNone = 0,
  kLitematicaLongArray,  // Litematica
  kSpongeVarint,         // Sponge Varint ByteArray
};

// Holds the raw, packed representation of a region's block indices.
// This allows deferred materialization (decoding)
// until block-level access is required.
struct LazyBlockData {
  // The raw byte span of the packed block data
  // (e.g., Litematica LongArray or Sponge Varint ByteArray).
  // Points into the Schematic's owned byte buffer.
  std::span<const std::byte> raw_bytes;

  // The encoding scheme used by `raw_bytes`.
  BlockDataEncoding encoding = BlockDataEncoding::kNone;

  // Bits per block (only valid for `kLitematicaLongArray` encoding).
  std::uint32_t bits_per_block = 0;

  // The original palette size when parsed,
  // used for validation during unpacking.
  std::size_t palette_size = 0;

  // True if palette[0] is an air variant. Required by Litematica format.
  bool air_at_zero = false;

  // True if the associated palette has not been structurally modified
  // (e.g., entries appended).
  // If true, the encoder may
  // bypass repacking and write `raw_bytes` directly (lazy passthrough).
  bool palette_pristine = true;
};

// Represents a 3D volume of blocks, entities, and block entities.
// This is the primary unit of manipulation for
// filters, editors, and inspectors.
struct Region {
  // The name of the region (e.g., "main", or named sub-regions in Litematica).
  std::string_view name;

  // The bounds of the region in world space.
  // origin = minimum corner, size >= 0.
  BoundingBox bounds;

  std::array<std::int32_t, 3> raw_position{};

  // raw_size maybe negative because Litematica uses negative values
  // to represent flipping.
  std::array<std::int32_t, 3> raw_size{};

  // The palette of distinct block states.
  // Indices in `block_indices` refer to this vector.
  std::vector<BlockState> palette;

  // Cached LUT for fast `IsAirVariant` lookups by palette index.
  mutable std::vector<std::uint8_t> is_air_lut;

  // Rebuilds `is_air_lut` to match `palette`. Must run on a single
  // thread before any worker reads IsAir(). Allocates on rebuild.
  void EnsureAirLut() const {
    if (is_air_lut.size() == palette.size()) return;
    is_air_lut.assign(palette.size(), 0);
    for (std::size_t i = 0; i < palette.size(); ++i) {
      if (base::IsAirVariant(palette[i].name)) {
        is_air_lut[i] = 1;
      }
    }
  }

  // O(1) air lookup via the cached LUT. Lazy-rebuilds if stale
  // (single-threaded callers only).
  [[nodiscard]] inline bool IsAir(std::uint16_t pal_idx) const noexcept {
    if (is_air_lut.size() != palette.size()) {
      EnsureAirLut();
    }
    return pal_idx < is_air_lut.size() && is_air_lut[pal_idx] != 0;
  }

  // The materialized (decoded) block indices.
  // Memory layout is YZX (Y major, Z middle, X minor) to match disk formats.
  // Mutable: materialized on-demand via `EnsureMaterialized()`.
  mutable memory::UnInitBuffer<std::uint16_t> block_indices;

  // The raw packed data, retained for lazy passthrough during encoding.
  mutable LazyBlockData lazy_source;

  // True if `block_indices` has been decoded and is ready for access.
  mutable bool is_materialized = false;

  // True if the region has been modified by an Editor.
  // Forces the encoder to repack `block_indices`
  // instead of using lazy passthrough.
  mutable bool edited = false;

  // Entities contained within the region (e.g., armor stands, mobs).
  std::vector<Entity> entities;

  // Block entities contained within the region (e.g., minecraft:chest).
  std::vector<BlockEntity> block_entities;

  // Raw NBT compound span for unparsed/unknown fields
  // (used for round-trip fidelity).
  std::span<const std::byte> raw_compound;
};

struct Metadata {
  // Common fields.
  std::string_view name;
  std::string_view author;

  // For specific fields.
  std::span<const std::byte> raw_compound;
};

// The root IR object.
// Represents a fully parsed schematic file,
// independent of the original source format.
//
// Non-copyable: region spans point into `owner`; move only.
struct Schematic {
  // The format this schematic was originally decoded from.
  SourceFormat source_format;

  // The version of the source format
  // (e.g., Litematica v5/v6/v7, Sponge v2/v3).
  std::int32_t source_version = 0;

  // The Minecraft DataVersion (e.g., 2860 for 1.18, 3837 for 1.20.5).
  std::int32_t data_version = 0;

  // File-level metadata.
  Metadata metadata;

  // The collection of regions contained in the schematic.
  std::vector<Region> regions;

  // Ownership of the raw decompressed NBT byte buffer.
  // All `std::string_view` and `std::span` fields in the IR
  // point into this buffer.
  std::unique_ptr<std::vector<std::byte>> owner;

  // Arena allocator for materialized block indices
  // and other temporary allocations.
  std::unique_ptr<memory::Arena> arena;

  Schematic() = default;
  Schematic(const Schematic&) = delete;
  Schematic& operator=(const Schematic&) = delete;
  Schematic(Schematic&&) noexcept = default;
  Schematic& operator=(Schematic&&) noexcept = default;
};

}  // namespace fschema::ir

#endif  // FSCHEMA_IR_TYPES_H_