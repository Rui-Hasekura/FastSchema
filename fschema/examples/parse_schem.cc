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

#include <memory>
#include <print>
#include <vector>

#include "fschema/base/compressor.h"
#include "fschema/base/error.h"
#include "fschema/schem/parse.h"
#include "fschema/schem/types.h"

namespace fb = fschema::base;
namespace fsc = fschema::schem;

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::println("Usage: {} <file.schem>", argv[0]);
    return 1;
  }

  // STEP 1: Decompression
  auto unpacked_bytes = fb::DecompressGzipFile(argv[1]);
  if (!unpacked_bytes) {
    std::println("Decompress failed: {}", fb::ToString(unpacked_bytes.error()));
    return 1;
  }

  // STEP 2: Transfer ownership to ParseSchematicFromBytes
  auto owner =
      std::make_unique<std::vector<std::byte>>(std::move(*unpacked_bytes));
  auto result = fsc::ParseSchematicFromBytes(std::move(owner));

  if (!result) {
    const fschema::ParseError& err = result.error();
    std::println("Parse failed: {} at \"{}\" (offset: {})",
                 fschema::ToString(err),
                 err.path,
                 err.offset);
    return 1;
  }

  // Success, access parsed schematic data
  const auto& schematic = *result;
  std::println("Version: {}", static_cast<int>(schematic.version));
  std::println("DataVersion: {}", schematic.data_version);
  std::println("Dimensions: {} x {} x {}",
               schematic.width,
               schematic.height,
               schematic.length);
  std::println("Volume: {}", fsc::VolumeOf(schematic));
  std::println("Palette: {}", schematic.palette.size());
  std::println("Block Entities: {}", schematic.block_entities.size());
  std::println("Entities: {}", schematic.entities.size());

  if (!schematic.raw_biome_data.empty()) {
    std::println("Biome Palette: {}", schematic.biome_palette.size());
    std::println("Biome Data Size: {} bytes", schematic.raw_biome_data.size());
  }

  // Print first few palette entries
  std::println("\nPalette (first 10):\n");
  for (std::size_t i = 0; i < schematic.palette.size() && i < 10; ++i) {
    std::println("  [{}] {}", i, schematic.palette[i].name);
    if (!schematic.palette[i].properties.empty()) {
      std::println("    [{}]", schematic.palette[i].properties);
    }
    std::println("");
  }
}