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

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "fschema/base/decompression.h"
#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/memory/arena.h"
#include "fschema/schem/internal/root.h"
#include "fschema/schem/types.h"
#include "fschema/tests/testdata_util.h"

namespace fs = std::filesystem;
namespace fb = fschema::base;
namespace fm = fschema::memory;
namespace fsc = fschema::schem;

[[nodiscard]] bool IsAirVariant(std::string_view name) noexcept {
  return name == "minecraft:air" || name == "minecraft:void_air" ||
         name == "minecraft:cave_air";
}

class SchemParseTest : public ::testing::TestWithParam<fs::path> {
 public:
  static std::vector<fs::path> GetSampleFiles() {
    std::vector<fs::path> files;
    const std::filesystem::path dir = fschema::test::GetTestDataDir();

    if (!fs::exists(dir)) {
      return files;
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".schem") {
        files.push_back(entry.path());
      }
    }
    return files;
  }
};

TEST_P(SchemParseTest, ParsesSuccessfullyAndValidatesStructure) {
  const auto& file_path = GetParam();

  auto unpacked_bytes = fb::DecompressGzipFile(file_path);
  ASSERT_TRUE(unpacked_bytes.has_value())
      << "Decompress failed for " << file_path << ": "
      << fb::ToString(unpacked_bytes.error());
  ASSERT_FALSE(unpacked_bytes->empty());

  fsc::Schematic schematic;
  schematic.arena = std::make_unique<fm::Arena>();
  schematic.owner =
      std::make_unique<std::vector<std::byte>>(std::move(*unpacked_bytes));

  fb::DecodeLimits limits;
  fb::ByteReader reader(std::span<const std::byte>(schematic.owner->data(),
                                                   schematic.owner->size()),
                        limits);

  auto result = fsc::internal::ParseRoot(reader, schematic);
  ASSERT_TRUE(result.has_value())
      << "Parse failed for " << file_path
      << " Code: " << static_cast<int>(result.error().code);

  // Volume / block_indices consistency
  const std::uint64_t expected_vol = fsc::VolumeOf(schematic);
  EXPECT_EQ(schematic.block_indices.size(), expected_vol)
      << "block_indices size mismatch with volume";

  // Palette sanity
  EXPECT_FALSE(schematic.palette.empty()) << "Palette is empty";

  // Sampled palette-index bounds check
  const std::uint64_t n = schematic.block_indices.size();
  constexpr std::uint64_t stride = 4096;
  for (std::uint64_t j = 0; j < n; j += stride) {
    EXPECT_LT(schematic.block_indices[j], schematic.palette.size())
        << "PaletteIndexOutOfRange at block " << j;
  }

  // Biome consistency (if present)
  if (!schematic.biome_indices.empty()) {
    const std::uint64_t biome_expected = fsc::BiomeVolumeOf(schematic);
    EXPECT_EQ(schematic.biome_indices.size(), biome_expected)
        << "biome_indices size mismatch with biome volume";
    EXPECT_FALSE(schematic.biome_palette.empty())
        << "biome_palette is empty but biome_indices is not";
  }

  // Non-air count
  if (!schematic.palette.empty() && !schematic.block_indices.empty()) {
    std::vector<std::uint64_t> counts(schematic.palette.size(), 0);
    for (std::uint16_t idx : schematic.block_indices) {
      ++counts[idx];
    }
    std::uint64_t non_air = 0;
    for (std::size_t k = 0; k < schematic.palette.size(); ++k) {
      if (counts[k] > 0 && !IsAirVariant(schematic.palette[k].name)) {
        non_air += counts[k];
      }
    }
    // Just log; schem format doesn't carry a metadata total_blocks field
    // to compare against (unlike litematic).
    EXPECT_GT(non_air, 0u) << "Schematic has zero non-air blocks";
  }
}

INSTANTIATE_TEST_SUITE_P(Samples,
                         SchemParseTest,
                         ::testing::ValuesIn(SchemParseTest::GetSampleFiles()));

// Negative tests

TEST(SchemParseNegativeTest, EmptyBytesInput) {
  fsc::Schematic schematic;
  schematic.arena = std::make_unique<fm::Arena>();
  schematic.owner = std::make_unique<std::vector<std::byte>>();

  fb::DecodeLimits limits;
  fb::ByteReader reader(std::span<const std::byte>(), limits);

  auto result = fsc::internal::ParseRoot(reader, schematic);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, fschema::ParseError::Code::Truncated);
}

TEST(SchemParseNegativeTest, InvalidRootTagId) {
  std::vector<std::byte> bytes = {static_cast<std::byte>(13)};

  fsc::Schematic schematic;
  schematic.arena = std::make_unique<fm::Arena>();
  schematic.owner = std::make_unique<std::vector<std::byte>>(std::move(bytes));

  fb::DecodeLimits limits;
  fb::ByteReader reader(std::span<const std::byte>(schematic.owner->data(),
                                                   schematic.owner->size()),
                        limits);

  auto result = fsc::internal::ParseRoot(reader, schematic);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, fschema::ParseError::Code::InvalidTagId);
}

TEST(SchemParseNegativeTest, TruncatedDuringCompoundHeader) {
  // Valid root tag ID (Compound = 10) but truncated immediately after.
  std::vector<std::byte> bytes = {static_cast<std::byte>(10)};

  fsc::Schematic schematic;
  schematic.arena = std::make_unique<fm::Arena>();
  schematic.owner = std::make_unique<std::vector<std::byte>>(std::move(bytes));

  fb::DecodeLimits limits;
  fb::ByteReader reader(std::span<const std::byte>(schematic.owner->data(),
                                                   schematic.owner->size()),
                        limits);

  auto result = fsc::internal::ParseRoot(reader, schematic);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, fschema::ParseError::Code::Truncated);
}