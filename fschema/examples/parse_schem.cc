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

// Example: Parse a .schem file (Sponge Schematic v2/v3) and print structure
// summary.
//
// Usage: parse_schem <file.schem>

#include <iostream>
#include <memory>
#include <vector>

#include "fschema/base/compressor.h"
#include "fschema/base/error.h"
#include "fschema/schem/parse.h"
#include "fschema/schem/types.h"

namespace fb = fschema::base;
namespace fsc = fschema::schem;

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <file.schem>\n";
    return 1;
  }

  // STEP 1: Decompression
  auto unpacked_bytes = fb::DecompressGzipFile(argv[1]);
  if (!unpacked_bytes) {
    std::cerr << "Decompress failed: " << fb::ToString(unpacked_bytes.error())
              << "\n";
    return 1;
  }

  // STEP 2: Transfer ownership to ParseSchematicFromBytes
  auto owner =
      std::make_unique<std::vector<std::byte>>(std::move(*unpacked_bytes));
  auto result = fsc::ParseSchematicFromBytes(std::move(owner));

  if (!result) {
    const fschema::ParseError& err = result.error();
    std::cerr << "Parse failed: " << fschema::ToString(err) << " at \""
              << err.path << "\" (offset: " << err.offset << ")\n";
    return 1;
  }

  // Success — access parsed schematic data
  const auto& schematic = *result;
  std::cout << "Version: " << static_cast<int>(schematic.version) << "\n";
  std::cout << "DataVersion: " << schematic.data_version << "\n";
  std::cout << "Dimensions: " << schematic.width << " x " << schematic.height
            << " x " << schematic.length << "\n";
  std::cout << "Volume: " << fsc::VolumeOf(schematic) << "\n";
  std::cout << "Palette: " << schematic.palette.size() << "\n";
  std::cout << "Block Entities: " << schematic.block_entities.size() << "\n";
  std::cout << "Entities: " << schematic.entities.size() << "\n";

  if (!schematic.biome_indices.empty()) {
    std::cout << "Biome Palette: " << schematic.biome_palette.size() << "\n";
    std::cout << "Biome Volume: " << fsc::BiomeVolumeOf(schematic) << "\n";
  }

  // Print first few palette entries
  std::cout << "\nPalette (first 10):\n";
  for (std::size_t i = 0; i < schematic.palette.size() && i < 10; ++i) {
    std::cout << "  [" << i << "] " << schematic.palette[i].name;
    if (!schematic.palette[i].properties.empty()) {
      std::cout << " [" << schematic.palette[i].properties << "]";
    }
    std::cout << "\n";
  }
}