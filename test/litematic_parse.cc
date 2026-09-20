#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "parser/error.h"
#include "parser/litematic/parse.h"
#include "parser/litematic/types.h"
#include "parser/unpacker.h"

#ifndef FASTSCHEMA_SAMPLES_DIR
#error "FASTSCHEMA_SAMPLES_DIR is not defined. Please check CMakeLists.txt."
#endif

namespace fs = std::filesystem;
namespace fp = fschema::parser;
namespace fl = fschema::parser::litematic;

// Helpers

[[nodiscard]] bool IsAirVariant(std::string_view name) noexcept {
  return name == "minecraft:air"
      || name == "minecraft:void_air"
      || name == "minecraft:cave_air";
}

[[nodiscard]] uint64_t ExpectedVolume(
  const std::array<std::int32_t, 3>& size) noexcept {
  const auto abs = [](int32_t v) -> uint64_t {
    return v < 0 ? uint64_t(-int64_t(v)) : uint64_t(v);
    };
  return abs(size[0]) * abs(size[1]) * abs(size[2]);
}

// Parameterized Test Fixture for Sample Files

class LitematicParseTest : public ::testing::TestWithParam<fs::path> {
public:
  static std::vector<fs::path> GetSampleFiles() {
    std::vector<fs::path> files;
    const fs::path samples_dir = FASTSCHEMA_SAMPLES_DIR;

    if (!fs::exists(samples_dir)) {
      return files;
    }

    for (const auto& entry : fs::directory_iterator(samples_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".litematic") {
        files.push_back(entry.path());
      }
    }
    return files;
  }
};

// E2E Test:
// Unpack + Parse + Structural Validation

TEST_P(LitematicParseTest, ParsesSuccessfullyAndValidatesStructure) {
  const auto& file_path = GetParam();

  // STEP 1: Decompression
  auto unpacked_bytes = fp::UnpackLitematicFrom(file_path);
  ASSERT_TRUE(unpacked_bytes.has_value())
    << "Decompress failed for " << file_path
    << ": " << fp::ToString(unpacked_bytes.error());
  ASSERT_FALSE(unpacked_bytes->empty());

  // STEP 2: Parse Litematic
  auto owner = std::make_unique<std::vector<std::byte>>(std::move(*unpacked_bytes));
  auto result = fl::ParseLitematic(std::move(owner));

  ASSERT_TRUE(result.has_value())
    << "Parse failed for " << file_path
    << " Code: " << static_cast<int>(result.error().code);

  const auto& litematic = result.value();

  // Version must be supported (5, 6, 7)
  EXPECT_TRUE(litematic.version == fl::Version::kV5 ||
    litematic.version == fl::Version::kV6 ||
    litematic.version == fl::Version::kV7)
    << "Unsupported version: " << static_cast<int>(litematic.version);

  // Metadata consistency
  EXPECT_EQ(litematic.metadata.region_count, litematic.regions.size())
    << "Metadata region_count mismatch";

  uint64_t total_non_air_blocks = 0;

  // Region structural integrity
  for (size_t i = 0; i < litematic.regions.size(); ++i) {
    const auto& region = litematic.regions[i];

    // Volume check
    const uint64_t expected_vol = ExpectedVolume(region.size);
    EXPECT_EQ(region.block_indices.size(), expected_vol)
      << "Region [" << i << "] block_indices size mismatch with volume";

    // Palette check
    EXPECT_FALSE(region.palette.empty())
      << "Region [" << i << "] has empty palette";

    // Index bounds check (sampled for performance)
    const uint64_t n = region.block_indices.size();
    constexpr uint64_t stride = 4096;
    for (uint64_t j = 0; j < n; j += stride) {
      EXPECT_LT(region.block_indices[j], region.palette.size())
        << "Region [" << i << "] PaletteIndexOutOfRange at block " << j;
    }

    // Conservation check (Count non-air blocks)
    if (!region.palette.empty() && !region.block_indices.empty()) {
      std::vector<uint64_t> counts(region.palette.size(), 0);
      for (uint32_t idx : region.block_indices) ++counts[idx];

      uint64_t non_air = 0;
      for (size_t k = 0; k < region.palette.size(); ++k) {
        if (counts[k] > 0 && !IsAirVariant(region.palette[k].name)) {
          non_air += counts[k];
        }
      }
      total_non_air_blocks += non_air;
    }
  }

  // Global block conservation
  EXPECT_EQ(total_non_air_blocks, static_cast<uint64_t>(litematic.metadata.total_blocks))
    << "Total non-air blocks do not match metadata.total_blocks";
}

INSTANTIATE_TEST_SUITE_P(
  Samples,
  LitematicParseTest,
  ::testing::ValuesIn(LitematicParseTest::GetSampleFiles())
);

// Negative Tests

TEST(LitematicParseNegativeTest, NullBytesInput) {
  std::unique_ptr<std::vector<std::byte>> null_owner = nullptr;
  auto result = fl::ParseLitematic(std::move(null_owner));

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, fp::ParseError::Code::Truncated);
}

TEST(LitematicParseNegativeTest, EmptyBytesInput) {
  auto empty_owner = std::make_unique<std::vector<std::byte>>();
  auto result = fl::ParseLitematic(std::move(empty_owner));

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, fp::ParseError::Code::Truncated);
}

TEST(LitematicUnpackNegativeTest, FileNotFound) {
  auto result = fp::UnpackLitematicFrom("nonexistent_file.litematic");

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), fp::DecompressError::FileNotFound);
}