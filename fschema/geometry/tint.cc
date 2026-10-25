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

#include "fschema/geometry/tint.h"

namespace fschema::geometry {

TintTable TintTable::ForBiome(std::string_view biome_id) {
  TintTable t;  // defaults to plains
  if (biome_id == "minecraft:desert") {
    t.slots[1] = {0xBF, 0xB7, 0x55};
    t.slots[2] = {0xAE, 0xA4, 0x2A};
  } else if (biome_id == "minecraft:swamp") {
    t.slots[1] = {0x6A, 0x70, 0x39};
    t.slots[2] = {0x6A, 0x70, 0x39};
  } else if (biome_id == "minecraft:badlands") {
    t.slots[1] = {0x90, 0x81, 0x4D};
    t.slots[2] = {0x9E, 0x81, 0x4D};
  }
  return t;
}

}  // namespace fschema::geometry
