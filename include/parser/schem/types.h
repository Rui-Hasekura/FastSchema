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

#ifndef FSCHEMA_PARSER_SCHEM_TYPES_H_
#define FSCHEMA_PARSER_SCHEM_TYPES_H_

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "parser/arena.h"
#include "parser/nbt/noinit_allocator.h"

template <typename T>
using NoInitVector = std::vector<T, fschema::parser::nbt::NoInitAllocator<T>>;

namespace fschema::parser::schem {

  enum class Version : std::int32_t {
    kV2 = 2,
    kV3 = 3,
  };

  struct Metadata {
    std::string_view name;
    std::string_view author;
    std::int64_t date = 0;
    std::span<const std::byte> required_mods;
    std::span<const std::byte> extra;
  };

  // Schem palette entry. The full blockstate string is parsed into
  // name (resource location) and properties (without surrounding brackets).
  //   full:       "minecraft:redstone_wire[east=side,north=none,power=15,...]"
  //   name:       "minecraft:redstone_wire"
  //   properties: "east=side,north=none,power=15,..."
  struct BlockState {
    std::string_view full;
    std::string_view name;
    std::string_view properties;
  };

  struct BlockEntity {
    std::string_view id;
    std::array<std::int32_t, 3> pos{ 0, 0, 0 };
    std::span<const std::byte> data;
  };

  struct Entity {
    std::string_view id;
    std::array<double, 3> pos{ 0, 0, 0 };
    std::span<const std::byte> data;
  };

  struct Schematic {
    Version version = Version::kV2;
    std::int32_t data_version = 0;

    Metadata metadata;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t length = 0;
    std::array<std::int32_t, 3> offset{ 0, 0, 0 };

    std::vector<BlockState> palette;
    NoInitVector<std::uint16_t> block_indices;

    std::vector<BlockEntity> block_entities;
    std::vector<Entity> entities;

    std::vector<std::string_view> biome_palette;
    NoInitVector<std::uint16_t> biome_indices;

    std::unique_ptr<Arena> arena;
    std::unique_ptr<std::vector<std::byte>> owner;
  };

  [[nodiscard]] inline std::uint64_t VolumeOf(
    const Schematic& s) noexcept {
    return static_cast<std::uint64_t>(s.width) *
      static_cast<std::uint64_t>(s.height) *
      static_cast<std::uint64_t>(s.length);
  }

  // Biome array length depends on format version:
  // v2: Width * Length (2D, full vertical column)
  // v3: Width * Height * Length (3D)
  [[nodiscard]] inline std::uint64_t BiomeVolumeOf(
    const Schematic& s) noexcept {
    if (s.version == Version::kV3) {
      return VolumeOf(s);
    }
    return static_cast<std::uint64_t>(s.width) *
      static_cast<std::uint64_t>(s.length);
  }

}  // namespace fschema::parser::schem

#endif  // FSCHEMA_PARSER_SCHEM_TYPES_H_