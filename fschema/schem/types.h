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

#ifndef FSCHEMA_SCHEM_TYPES_H_
#define FSCHEMA_SCHEM_TYPES_H_

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "fschema/base/nbt/tag.h"
#include "fschema/memory/arena.h"

namespace fschema::schem {

enum class Version : std::int32_t {
  kV2 = 2,
  kV3 = 3,
};

struct RawField {
  std::string_view name;
  base::TagType type = base::TagType::End;
  std::span<const std::byte> payload;
};

struct Metadata {
  std::string_view name;
  std::string_view author;
  std::int64_t date = 0;
  std::span<const std::byte> required_mods;
  std::span<const std::byte> extra;
  std::span<const std::byte> raw_compound;
};

struct BlockState {
  std::string_view name;
  std::string_view properties;
};

struct BlockEntity {
  std::string_view id;
  std::array<std::int32_t, 3> pos{0, 0, 0};
  std::span<const std::byte> data;
};

struct Entity {
  std::string_view id;
  std::array<double, 3> pos{0, 0, 0};
  std::array<double, 3> motion{0, 0, 0};
  std::array<float, 2> rotation{0, 0};
  std::span<const std::byte> data;
};

struct Schematic {
  Version version = Version::kV2;
  std::int32_t data_version = 0;

  Metadata metadata;

  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t length = 0;
  std::array<std::int32_t, 3> offset{0, 0, 0};

  std::vector<BlockState> palette;
  std::span<const std::byte> raw_block_data;

  std::vector<BlockEntity> block_entities;
  std::vector<Entity> entities;

  std::vector<std::string_view> biome_palette;
  std::span<const std::byte> raw_biome_data;

  std::unique_ptr<std::vector<std::byte>> owner;
  std::unique_ptr<memory::Arena> arena;
  std::span<const std::byte> raw_compound;
};

[[nodiscard]] inline std::uint64_t VolumeOf(const Schematic& s) noexcept {
  return static_cast<std::uint64_t>(s.width) *
         static_cast<std::uint64_t>(s.height) *
         static_cast<std::uint64_t>(s.length);
}

[[nodiscard]] inline std::uint64_t BiomeVolumeOf(const Schematic& s) noexcept {
  if (s.version == Version::kV3) {
    return VolumeOf(s);
  }
  return static_cast<std::uint64_t>(s.width) *
         static_cast<std::uint64_t>(s.length);
}

}  // namespace fschema::schem

#endif  // FSCHEMA_SCHEM_TYPES_H_