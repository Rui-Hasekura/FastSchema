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

// Example: Parse a .litematic file and print structure summary.
//
// Usage: parse_litematic <file.litematic>

#include <iostream>
#include <memory>
#include <vector>

#include "fschema/base/compressor.h"
#include "fschema/base/error.h"
#include "fschema/litematic/parse.h"
#include "fschema/litematic/types.h"

namespace fb = fschema::base;
namespace fl = fschema::litematic;

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <file.litematic>\n";
    return 1;
  }

  // STEP 1: Decompression
  auto unpacked_bytes = fb::DecompressGzipFile(argv[1]);
  if (!unpacked_bytes) {
    std::cerr << "Decompress failed: " << fb::ToString(unpacked_bytes.error())
              << "\n";
    return 1;
  }

  // STEP 2: Transfer ownership to ParseLitematic
  auto owner =
      std::make_unique<std::vector<std::byte>>(std::move(*unpacked_bytes));
  auto result = fl::ParseLitematic(std::move(owner));

  if (!result) {
    const fschema::ParseError& err = result.error();
    std::cerr << "Parse failed: " << fschema::ToString(err) << " at \""
              << err.path << "\" (offset: " << err.offset << ")\n";
    return 1;
  }

  // Success — fully parsed litematic with zero-copy block indices
  const auto& litematic = *result;
  std::cout << "Name: " << litematic.metadata.name << "\n";
  std::cout << "Author: " << litematic.metadata.author << "\n";
  std::cout << "Version: " << static_cast<int>(litematic.version) << "\n";
  std::cout << "DataVersion: " << litematic.data_version << "\n";
  std::cout << "Regions: " << litematic.regions.size() << "\n";
  std::cout << "Total blocks: " << litematic.metadata.total_blocks << "\n";
  std::cout << "Total volume: " << litematic.metadata.total_volume << "\n";

  for (std::size_t i = 0; i < litematic.regions.size(); ++i) {
    const auto& reg = litematic.regions[i];
    std::cout << "  Region [" << i << "] \"" << reg.name
              << "\": " << reg.block_indices.size() << " blocks, "
              << reg.palette.size() << " palette entries, "
              << reg.entities.size() << " entities, "
              << reg.tile_entities.size() << " tile entities\n";
  }
}