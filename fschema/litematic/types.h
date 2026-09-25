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

#ifndef FSCHEMA_LITEMATIC_TYPES_H_
#define FSCHEMA_LITEMATIC_TYPES_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "fschema/memory/arena.h"
#include "fschema/memory/noinit_allocator.h"

namespace fschema::litematic {

  // Minecraft Java Edition Version
  enum class Version : std::int32_t {
    kV5 = 5,   // 1.13 - 1.17
    kV6 = 6,   // 1.18 - 1.20.4
    kV7 = 7,   // 1.20.5+
  };

  // Litematic Metadata
  struct Metadata {
    std::string_view name;
    std::string_view author;
    std::string_view description;
    std::int32_t region_count = 0;
    std::int32_t total_blocks = 0;
    std::int32_t total_volume = 0;
    std::array<std::int32_t, 3> enclosing_size = { 0, 0, 0 };
    std::int64_t time_created = 0;  // Java epoch milliseconds
    std::int64_t time_modified = 0;
    // PreviewData is an IntArray, stored as a raw span, decoded by downstream if needed.
    std::span<const std::byte> preview_data;  // May be empty
  };

  // Palette
  struct BlockState {
    std::string_view name;                         // e.g. "minecraft:oak_stairs[facing=north]"
    // Properties are stored as raw NBT Compound bytes (open schema, arbitrary keys).
    std::span<const std::byte> properties;    // May be empty
  };

  // Entity / TileEntity
  // Eager common fields + Lazy original bytes.
  // High-frequency fields are eagerly parsed for convenience,
  // while the full NBT subtree is kept as a raw span for deep parsing if needed.
  struct Entity {
    std::string_view id;                          // e.g. "minecraft:item_frame"
    std::array<double, 3> position;          // World position (x, y, z)
    std::array<double, 3> motion;            // Motion vector (vx, vy, vz)
    std::array<float, 2> rotation;           // yaw, pitch
    std::span<const std::byte> raw_nbt;      // Full NBT subtree, for deep parsing if needed
  };

  struct TileEntity {
    std::string_view id;                          // e.g. "minecraft:chest"
    std::array<std::int32_t, 3> block_position;   // Block position
    std::span<const std::byte> raw_nbt;      // Full NBT subtree, for deep parsing if needed
  };

  // (v6+) Pending lists (Optional)
  struct PendingTick {
    std::string_view block;                // e.g. "minecraft:comparator"
    std::array<std::int32_t, 3> pos{};     // Region local position
    std::int64_t sub_tick = 0;
    std::int32_t priority = 0;             // May be -1
    std::int32_t time = 0;
  };

  struct Region {
    std::string_view name;
    std::array<std::int32_t, 3> position;         // Relative to origin (can be negative)
    std::array<std::int32_t, 3> size;             // Dimensions (can be negative, negative = flipped)
    std::vector<BlockState> palette;
    // Unpacked block indices for each block in the region.
    // Length = |x| * |y| * |z|; values in [0, palette.size())
    memory::NoInitVector<std::uint16_t> block_indices;
    std::vector<Entity> entities;
    std::vector<TileEntity> tile_entities;

    // (v6+) Pending
    std::vector<PendingTick> pending_block_ticks;
    std::vector<PendingTick> pending_fluid_ticks;
    std::span<const std::byte> pending_block_entities;
    std::span<const std::byte> pending_entities;
  };

  // Up-level
  struct Litematic {
    Version version = Version::kV5;
    std::int32_t data_version = 0;                // Minecraft DataVersion
    Metadata metadata;
    std::vector<Region> regions;

    // Owner of the bytes (after decompression).
    // All spans (properties / raw_nbt / preview_data / pending_*) point to this memory;
    // when Litematic is destructed, all spans become invalid.
    // Downstream should not use this pointer directly.
    std::unique_ptr<memory::Arena> arena;
    std::unique_ptr<std::vector<std::byte>> owner;
  };

  // Convenience functions
  [[nodiscard]] inline std::uint64_t VolumeOf(
    const std::array<std::int32_t, 3>& size) noexcept {
    const auto abs_dim = [](std::int32_t v) -> std::uint64_t {
      return v < 0 ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(v))
        : static_cast<std::uint64_t>(v);
      };
    return abs_dim(size[0]) * abs_dim(size[1]) * abs_dim(size[2]);
  }

  // Get blockname from block_indices + palette
  [[nodiscard]] inline std::string_view BlockNameAt(
    const Region& region, std::uint64_t index) noexcept {
    if (index >= region.block_indices.size()) return {};
    const std::uint16_t palette_idx = region.block_indices[index];
    if (palette_idx >= region.palette.size()) return {};
    return region.palette[palette_idx].name;
  }

}  // namespace fschema::litematic

#endif  // FSCHEMA_LITEMATIC_TYPES_H_