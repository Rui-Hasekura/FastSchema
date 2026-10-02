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

#include "fschema/base/nbt_tag.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/noinit_allocator.h"

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
  kSchem = 1,
};

enum class PropertyEncoding : std::uint8_t {
  kNone = 0,
  kNbt = 1,     // Litematica: Raw NBT Compound
  kString = 2,  // Sponge Schematic: Key-value string ("k=v,k=v")
};

struct Extension {
  std::string_view key;
  base::TagType tag_type = base::TagType::End;
  std::span<const std::byte> raw_payload;
};

struct BlockState {
  std::string_view name;

  // View to raw property bytes.
  std::span<const std::byte> raw_properties;

  PropertyEncoding prop_encoding = PropertyEncoding::kNone;
};

struct Entity {
  std::string_view id;
  std::array<double, 3> position{};
  std::array<double, 3> motion{};
  std::array<float, 2> rotation{};

  // ALL EXTRA DATA include Components.
  std::span<const std::byte> raw_nbt;
};

struct BlockEntity {
  std::string_view id;
  std::array<std::int32_t, 3> block_position{};
  std::span<const std::byte> raw_nbt;
};

// TODO: implement v2 -> v3 biome conversion (v3 is a 3D superset of v2)
enum class BiomeLayout : std::uint8_t {
  kNone = 0,
  k2D = 1,  // Sponge Schematic v2
  k3D = 2,  // Sponge Schematic v3
};

struct BiomeData {
  std::vector<std::string_view> palette;
  memory::NoInitVector<std::uint16_t> indices;
  BiomeLayout layout = BiomeLayout::kNone;
};

struct BoundingBox {
  std::int32_t origin[3]{};

  // Always positive (> 0) after normalization.
  std::int32_t size[3]{};
};

enum class BlockDataEncoding : std::uint8_t {
  kNone = 0,
  kLitematicaLongArray,  // Litematica
  kSpongeVarint,         // Sponge Varint ByteArray
};

struct LazyBlockData {
  std::span<const std::byte> raw_bytes;
  BlockDataEncoding encoding = BlockDataEncoding::kNone;
  std::uint32_t bits_per_block = 0;  // For Litematica
  std::size_t palette_size = 0;      // For format validation
  bool air_at_zero = false;          // Litematica: palette[0] is air
  bool palette_pristine = true;      // Whether the palette is unmodified
};

struct Region {
  std::string_view name;
  BoundingBox bounds;

  std::vector<BlockState> palette;

  // Materialized block indices on demand
  mutable memory::NoInitVector<std::uint16_t> block_indices;
  mutable LazyBlockData lazy_source;
  mutable bool is_materialized = false;

  std::vector<Entity> entities;
  std::vector<BlockEntity> block_entities;

  // For specific fields.
  std::span<const std::byte> raw_compound;
};

struct Metadata {
  // Common fields.
  std::string_view name;
  std::string_view author;

  // For specific fields.
  std::span<const std::byte> raw_compound;
};

struct Schema {
  SourceFormat source_format;

  // Format version for the source file.
  std::int32_t source_version = 0;

  // Minecraft DataVersion.
  std::int32_t data_version = 0;

  Metadata metadata;
  std::vector<Region> regions;

  // Memory ownership management.
  std::unique_ptr<std::vector<std::byte>> owner;
  std::unique_ptr<memory::Arena> arena;
};

}  // namespace fschema::ir

#endif  // FSCHEMA_IR_TYPES_H_