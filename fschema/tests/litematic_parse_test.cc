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

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "fschema/base/decompression.h"
#include "fschema/base/error.h"
#include "fschema/litematic/parse.h"
#include "fschema/litematic/types.h"
#include "fschema/tests/testdata_util.h"

namespace fs = std::filesystem;
namespace fb = fschema::base;
namespace fl = fschema::litematic;

[[nodiscard]] bool IsAirVariant(std::string_view name) noexcept {
  return name == "minecraft:air" || name == "minecraft:void_air" ||
         name == "minecraft:cave_air";
}

[[nodiscard]] std::uint64_t ExpectedVolume(
    const std::array<std::int32_t, 3>& size) noexcept {
  const auto abs = [](std::int32_t v) -> std::uint64_t {
    return v < 0 ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(v))
                 : static_cast<std::uint64_t>(v);
  };
  return abs(size[0]) * abs(size[1]) * abs(size[2]);
}

class LitematicParseTest : public ::testing::TestWithParam<fs::path> {
 public:
  static std::vector<fs::path> GetSampleFiles() {
    std::vector<fs::path> files;
    const std::filesystem::path dir = fschema::test::GetTestDataDir();

    if (!fs::exists(dir)) {
      return files;
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".litematic") {
        files.push_back(entry.path());
      }
    }
    return files;
  }
};

TEST_P(LitematicParseTest, ParsesSuccessfullyAndValidatesStructure) {
  const auto& file_path = GetParam();

  auto unpacked_bytes = fb::DecompressGzipFile(file_path);
  ASSERT_TRUE(unpacked_bytes.has_value())
      << "Decompress failed for " << file_path << ": "
      << fb::ToString(unpacked_bytes.error());
  ASSERT_FALSE(unpacked_bytes->empty());

  auto owner =
      std::make_unique<std::vector<std::byte>>(std::move(*unpacked_bytes));
  auto result = fl::ParseLitematic(std::move(owner));

  ASSERT_TRUE(result.has_value())
      << "Parse failed for " << file_path
      << " Code: " << static_cast<int>(result.error().code);

  const auto& litematic = result.value();

  EXPECT_TRUE(litematic.version == fl::Version::kV5 ||
              litematic.version == fl::Version::kV6 ||
              litematic.version == fl::Version::kV7)
      << "Unsupported version: " << static_cast<int>(litematic.version);

  EXPECT_EQ(litematic.metadata.region_count, litematic.regions.size())
      << "Metadata region_count mismatch";

  std::uint64_t total_non_air_blocks = 0;

  for (std::size_t i = 0; i < litematic.regions.size(); ++i) {
    const auto& region = litematic.regions[i];

    const std::uint64_t expected_vol = ExpectedVolume(region.size);
    EXPECT_EQ(region.block_indices.size(), expected_vol)
        << "Region [" << i << "] block_indices size mismatch with volume";

    EXPECT_FALSE(region.palette.empty())
        << "Region [" << i << "] has empty palette";

    const std::uint64_t n = region.block_indices.size();
    constexpr std::uint64_t stride = 4096;
    for (std::uint64_t j = 0; j < n; j += stride) {
      EXPECT_LT(region.block_indices[j], region.palette.size())
          << "Region [" << i << "] PaletteIndexOutOfRange at block " << j;
    }

    if (!region.palette.empty() && !region.block_indices.empty()) {
      std::vector<std::uint64_t> counts(region.palette.size(), 0);
      for (std::uint32_t idx : region.block_indices) {
        ++counts[idx];
      }

      std::uint64_t non_air = 0;
      for (std::size_t k = 0; k < region.palette.size(); ++k) {
        if (counts[k] > 0 && !IsAirVariant(region.palette[k].name)) {
          non_air += counts[k];
        }
      }
      total_non_air_blocks += non_air;
    }
  }

  EXPECT_EQ(total_non_air_blocks,
            static_cast<std::uint64_t>(litematic.metadata.total_blocks))
      << "Total non-air blocks do not match metadata.total_blocks";
}

INSTANTIATE_TEST_SUITE_P(
    Samples,
    LitematicParseTest,
    ::testing::ValuesIn(LitematicParseTest::GetSampleFiles()));

TEST(LitematicParseNegativeTest, NullBytesInput) {
  std::unique_ptr<std::vector<std::byte>> null_owner = nullptr;
  auto result = fl::ParseLitematic(std::move(null_owner));

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, fschema::ParseError::Code::Truncated);
}

TEST(LitematicParseNegativeTest, EmptyBytesInput) {
  auto empty_owner = std::make_unique<std::vector<std::byte>>();
  auto result = fl::ParseLitematic(std::move(empty_owner));

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, fschema::ParseError::Code::Truncated);
}

TEST(LitematicUnpackNegativeTest, FileNotFound) {
  auto result = fb::DecompressGzipFile("nonexistent_file.litematic");

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), fb::DecompressError::kFileNotFound);
}