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

// Example: Convert between Litematica and Sponge Schematic formats via the
// format-agnostic IR. Supports all 20 conversion paths:
//   Litematica v5/v6/v7 ↔ Schem v2/v3
//   Litematica v5 ↔ v6 ↔ v7  (same-format version downgrade/upgrade)
//   Schem v2 ↔ v3
//
// Usage: convert_format <input> <output> [--target-version <ver>]
//                        [--no-compress]
//
// Examples:
//   convert_format input.litematic output.schem
//   convert_format input.litematic output.schem --target-version 2
//   convert_format input.schem output.litematic --target-version 7
//   convert_format input.litematic output.litematic --target-version 5

#include <cstring>
#include <filesystem>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "fschema/base/compressor.h"
#include "fschema/base/error.h"
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematic_handler.h"
#include "fschema/ir/schem_handler.h"
#include "fschema/ir/types.h"

namespace fir = fschema::ir;
namespace firc = fschema::ir::format;

[[nodiscard]] fir::SourceFormat DetectFormat(
    const std::filesystem::path& path) {
  auto ext = path.extension().string();
  if (ext == ".litematic") return fir::SourceFormat::kLitematica;
  if (ext == ".schem") return fir::SourceFormat::kSchem;
  std::println("Unknown file extension: {} (expected .litematic or .schem)",
               ext);
  std::exit(1);
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::println(
        "Usage: {} <input> <output> [--target-version <ver>] [--no-compress]",
        argv[0]);
    std::println("\nExamples:");
    std::println("  {} input.litematic output.schem", argv[0]);
    std::println("  {} input.litematic output.schem --target-version 2",
                 argv[0]);
    std::println("  {} input.schem output.litematic --target-version 7",
                 argv[0]);
    std::println("  {} input.litematic output.litematic --target-version 5",
                 argv[0]);
    return 1;
  }

  const std::filesystem::path input_path = argv[1];
  const std::filesystem::path output_path = argv[2];

  const auto from_fmt = DetectFormat(input_path);
  const auto to_fmt = DetectFormat(output_path);

  // Parse CLI options
  firc::EncodeOptions opts;
  opts.multi_region =
      firc::EncodeOptions::MultiRegionStrategy::kMergeBoundingBox;

  for (int i = 3; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--target-version" && i + 1 < argc) {
      opts.target_version = std::stoi(argv[++i]);
    } else if (arg == "--no-compress") {
      opts.compress = false;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      return 1;
    }
  }

  // Register format handlers (singleton, only first call has effect)
  firc::FormatRegistry::Instance().Register(
      std::make_unique<firc::LitematicaHandler>());
  firc::FormatRegistry::Instance().Register(
      std::make_unique<firc::SchemHandler>());

  // STEP 1: Read and decompress input file
  auto unpacked = fschema::base::DecompressGzipFile(input_path);
  if (!unpacked) {
    std::println("Decompress failed: {}",
                 fschema::base::ToString(unpacked.error()));
    return 1;
  }

  auto owner = std::make_unique<std::vector<std::byte>>(std::move(*unpacked));

  // STEP 2: Convert via IR
  auto result = firc::Convert(from_fmt, to_fmt, std::move(owner), opts);
  if (!result) {
    const fschema::ParseError& err = result.error();
    std::println("Convert failed: {} at \"{}\" (offset: {})",
                 fschema::ToString(err),
                 err.path,
                 err.offset);
    return 1;
  }

  // STEP 3: Write output
  const auto& out_bytes = *result;
  FILE* f = std::fopen(output_path.string().c_str(), "wb");
  if (!f) {
    std::println("Failed to open output file: {}", output_path);
    return 1;
  }
  std::fwrite(out_bytes.data(), 1, out_bytes.size(), f);
  std::fclose(f);

  std::println("Converted: {} → {} ({} bytes)",
               input_path,
               output_path,
               out_bytes.size());
  return 0;
}