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

#ifndef FSCHEMA_GEOMETRY_TINT_H_
#define FSCHEMA_GEOMETRY_TINT_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fschema::geometry {

struct TintTable {
  // Slot indices. Kept in sync with ModelFace::TintSlot().
  //   0 = identity (no tint)
  //   1 = grass
  //   2 = foliage
  //   3 = water
  //   4 = redstone
  //   5 = birch foliage (fixed color, biome-independent)
  //   6 = spruce foliage (fixed color, biome-independent)
  static constexpr std::size_t kSlotCount = 7;

  std::array<std::array<std::uint8_t, 3>, kSlotCount> slots{};

  // Default table: plains-biome colors + vanilla redstone.
  constexpr TintTable() noexcept {
    slots[0] = {0xFF, 0xFF, 0xFF};
    slots[1] = {0x91, 0xB4, 0x4E};
    slots[2] = {0x77, 0xA9, 0x3E};
    slots[3] = {0x3F, 0x57, 0xBE};
    slots[4] = {0xFF, 0x00, 0x00};
    slots[5] = {0x80, 0xA7, 0x55};
    slots[6] = {0x61, 0x99, 0x61};
  }

  // Returns a tint table for the given biome. Biomes without a specific
  // colormap fall back to plains.
  [[nodiscard]] static TintTable ForBiome(std::string_view biome_id);
};

}  // namespace fschema::geometry

#endif  // FSCHEMA_GEOMETRY_TINT_H_
