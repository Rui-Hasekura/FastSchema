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
#include <utility>
#include <vector>

#include "fschema/base/block_utils.h"
#include "fschema/base/compressor.h"
#include "fschema/base/error.h"
#include "fschema/filters/view.h"
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematica_handler.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/types.h"
#include "fschema/litematica/internal/block_states.h"
#include "fschema/litematica/parse.h"
#include "fschema/litematica/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"
#include "fschema/tests/testdata_util.h"

namespace fs = std::filesystem;
namespace fb = fschema::base;
namespace fir = fschema::ir;
namespace firc = fschema::ir::format;
namespace filt = fschema::filters;
namespace fl = fschema::litematica;

[[nodiscard]] std::uint64_t ExpectedVolume(
    const std::array<std::int32_t, 3>& size) noexcept {
  const auto abs = [](std::int32_t v) -> std::uint64_t {
    return v < 0 ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(v))
                 : static_cast<std::uint64_t>(v);
  };
  return abs(size[0]) * abs(size[1]) * abs(size[2]);
}

class LitematicaParseTest : public ::testing::TestWithParam<fs::path> {
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

TEST_P(LitematicaParseTest, ParsesSuccessfullyAndValidatesStructure) {
  const auto& file_path = GetParam();

  auto unpacked_bytes = fb::DecompressGzipFile(file_path);
  ASSERT_TRUE(unpacked_bytes.has_value())
      << "Decompress failed for " << file_path << ": "
      << fb::ToString(unpacked_bytes.error());
  ASSERT_FALSE(unpacked_bytes->empty());

  auto owner =
      std::make_unique<std::vector<std::byte>>(std::move(*unpacked_bytes));
  auto result = fl::ParseLitematica(std::move(owner));

  ASSERT_TRUE(result.has_value())
      << "Parse failed for " << file_path
      << " Code: " << static_cast<int>(result.error().code);

  const auto& litematica = result.value();

  EXPECT_TRUE(litematica.version == fl::Version::kV5 ||
              litematica.version == fl::Version::kV6 ||
              litematica.version == fl::Version::kV7)
      << "Unsupported version: " << static_cast<int>(litematica.version);

  EXPECT_EQ(litematica.metadata.region_count, litematica.regions.size())
      << "Metadata region_count mismatch";

  std::uint64_t total_non_air_blocks = 0;

  for (std::size_t i = 0; i < litematica.regions.size(); ++i) {
    const auto& region = litematica.regions[i];

    const std::uint64_t expected_vol = ExpectedVolume(region.size);

    EXPECT_FALSE(region.palette.empty())
        << "Region [" << i << "] has empty palette";

    if (expected_vol == 0) continue;

    const std::uint32_t bpb = fl::internal::BitsPerBlock(region.palette.size());
    auto unpack_res = fl::internal::UnpackIndicesFused(region.raw_block_states,
                                                       bpb,
                                                       expected_vol,
                                                       region.palette.size());
    ASSERT_TRUE(unpack_res.has_value()) << "Unpack failed for region " << i;

    EXPECT_EQ(unpack_res->size(), expected_vol)
        << "Region [" << i << "] block_indices size mismatch with volume";

    const std::uint64_t n = unpack_res->size();
    constexpr std::uint64_t stride = 4096;
    for (std::uint64_t j = 0; j < n; j += stride) {
      EXPECT_LT((*unpack_res)[j], region.palette.size())
          << "Region [" << i << "] PaletteIndexOutOfRange at block " << j;
    }

    if (!region.palette.empty() && !unpack_res->empty()) {
      std::vector<std::uint64_t> counts(region.palette.size(), 0);
      for (std::uint16_t idx : *unpack_res) {
        ++counts[idx];
      }

      std::uint64_t non_air = 0;
      for (std::size_t k = 0; k < region.palette.size(); ++k) {
        if (counts[k] > 0 &&
            !fschema::base::IsAirVariant(region.palette[k].name)) {
          non_air += counts[k];
        }
      }
      total_non_air_blocks += non_air;
    }
  }

  EXPECT_EQ(total_non_air_blocks,
            static_cast<std::uint64_t>(litematica.metadata.total_blocks))
      << "Total non-air blocks do not match metadata.total_blocks";
}

INSTANTIATE_TEST_SUITE_P(
    Samples,
    LitematicaParseTest,
    ::testing::ValuesIn(LitematicaParseTest::GetSampleFiles()));

TEST(LitematicaParseNegativeTest, NullBytesInput) {
  std::unique_ptr<std::vector<std::byte>> null_owner = nullptr;
  auto result = fl::ParseLitematica(std::move(null_owner));

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, fschema::ParseError::Code::kTruncated);
}

TEST(LitematicaParseNegativeTest, EmptyBytesInput) {
  auto empty_owner = std::make_unique<std::vector<std::byte>>();
  auto result = fl::ParseLitematica(std::move(empty_owner));

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, fschema::ParseError::Code::kTruncated);
}

TEST(LitematicaUnpackNegativeTest, FileNotFound) {
  auto result = fb::DecompressGzipFile("nonexistent_file.litematic");

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), fb::CompressorError::kFileNotFound);
}

// ---------------------------------------------------------------------------
// IR decode: negative-size regions and entity anchoring.
//
// Litematica anchors two coordinate frames per region: the block array
// starts at the region's minimum corner, which on a negative-size axis is
// Position+Size+1 (selection corners are inclusive -- the last block sits
// ON Position itself), while each entity's Pos is relative to Position.
// default_4653.litematic has all three sizes negative (Position (3,4,3),
// Size (-4,-5,-4)) and carries one sheep standing in a glass cell, so it
// pins both conventions at once: decoded right, the sheep's feet rest
// exactly on the glass base block under it, the way the game places it.
// Decoded with a Position+Size origin, every block shifts one cell past
// the entities on each negative axis and the sheep appears sunk one
// block into the cell.
// ---------------------------------------------------------------------------

TEST(LitematicaIrDecodeTest, NegativeSizeOriginAndSheepAnchor) {
  static bool handlers_registered = false;
  if (!handlers_registered) {
    firc::FormatRegistry::Instance().Register(
        std::make_unique<firc::LitematicaHandler>());
    handlers_registered = true;
  }

  const fs::path path =
      fschema::test::GetTestDataDir() / "default_4653.litematic";
  auto unpacked = fb::DecompressGzipFile(path);
  ASSERT_TRUE(unpacked.has_value())
      << "Decompress failed: " << fb::ToString(unpacked.error());

  auto owner =
      std::make_unique<std::vector<std::byte>>(std::move(*unpacked));
  auto decoded =
      firc::DecodeFromFormat(fir::SourceFormat::kLitematica, std::move(owner));
  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ(decoded->regions.size(), 1u);
  const fir::Region& r = decoded->regions[0];

  // Raw on-disk values survive verbatim.
  EXPECT_EQ(r.raw_position, (std::array<std::int32_t, 3>{3, 4, 3}));
  EXPECT_EQ(r.raw_size, (std::array<std::int32_t, 3>{-4, -5, -4}));

  // Inclusive corners: the block array starts at Position+Size+1 = (0,0,0)
  // on all three negative axes, not at Position+Size = (-1,-1,-1).
  EXPECT_EQ(r.bounds.origin[0], 0);
  EXPECT_EQ(r.bounds.origin[1], 0);
  EXPECT_EQ(r.bounds.origin[2], 0);
  EXPECT_EQ(r.bounds.size[0], 4);
  EXPECT_EQ(r.bounds.size[1], 5);
  EXPECT_EQ(r.bounds.size[2], 4);

  // Entities are Position-relative: Pos (-0.992, -3.0, -1.55) plus
  // Position (3,4,3) is where the game pastes the sheep.
  ASSERT_EQ(r.entities.size(), 1u);
  EXPECT_EQ(r.entities[0].id, "minecraft:sheep");
  EXPECT_NEAR(r.entities[0].position[0], 2.007747497834316, 1e-9);
  EXPECT_NEAR(r.entities[0].position[1], 1.0, 1e-9);
  EXPECT_NEAR(r.entities[0].position[2], 1.449999988079071, 1e-9);

  // The sheep stands in a glass cell: the block under its feet is the
  // white stained glass base at local (2,0,1), with air in the cells
  // above it.
  ASSERT_TRUE(fir::EnsureMaterialized(r, *decoded->arena).has_value());
  const std::uint16_t floor_pal =
      r.block_indices[filt::LinearIndex({2, 0, 1}, r.bounds)];
  EXPECT_EQ(r.palette[floor_pal].name, "minecraft:white_stained_glass");
  EXPECT_TRUE(
      r.IsAir(r.block_indices[filt::LinearIndex({2, 1, 1}, r.bounds)]));
  EXPECT_TRUE(
      r.IsAir(r.block_indices[filt::LinearIndex({2, 2, 1}, r.bounds)]));

  // Geometric invariant the renderer depends on: the feet rest exactly
  // on the top face of the floor block below them. With the wrong
  // (Position+Size) origin the floor drops to y = -1 and the sheep
  // floats one block above it, i.e. the structure swallows it.
  EXPECT_DOUBLE_EQ(r.entities[0].position[1],
                   static_cast<double>(r.bounds.origin[1] + 0 + 1));
}

TEST(LitematicaIrDecodeTest, NegativeSizeRoundTripKeepsEntityOnItsBlock) {
  static bool handlers_registered = false;
  if (!handlers_registered) {
    firc::FormatRegistry::Instance().Register(
        std::make_unique<firc::LitematicaHandler>());
    handlers_registered = true;
  }

  // A 2x2x2 region stored the way Litematica saves a flipped selection:
  // Position (5,6,7), Size (-2,-2,-2), so the block array starts at
  // (4,5,6) and Position itself is the last block column. One stone sits
  // at the minimum corner with a sheep standing on top of it.
  fir::Schematic s;
  s.source_format = fir::SourceFormat::kLitematica;
  s.source_version = 6;
  s.data_version = 3105;
  s.arena = std::make_unique<fschema::memory::Arena>();

  fir::Region reg;
  static constexpr char kName[] = "neg";
  reg.name = kName;
  reg.raw_position = {5, 6, 7};
  reg.raw_size = {-2, -2, -2};
  reg.bounds = {{4, 5, 6}, {2, 2, 2}};
  reg.palette = {
      fir::BlockState{.name = "minecraft:air",
                      .prop_encoding = fir::PropertyEncoding::kNone},
      fir::BlockState{.name = "minecraft:stone",
                      .prop_encoding = fir::PropertyEncoding::kNone},
  };
  reg.is_air_lut.assign(2, 0);
  reg.is_air_lut[0] = 1;
  reg.block_indices = fschema::memory::UnInitBuffer<std::uint16_t>(
      8, s.arena.get());
  std::uint16_t* data =
      reinterpret_cast<std::uint16_t*>(reg.block_indices.data());
  for (int i = 0; i < 8; ++i) data[i] = 0;  // air
  data[filt::LinearIndex({0, 0, 0}, reg.bounds)] = 1;  // stone at min corner
  reg.is_materialized = true;
  reg.lazy_source.encoding = fir::BlockDataEncoding::kNone;

  static constexpr char kSheepId[] = "minecraft:sheep";
  reg.entities.push_back(fir::Entity{
      .id = kSheepId,
      .position = {4.5, 6.0, 6.5},  // feet on top of the stone at (4,5,6)
      .rotation = {0.0f, 0.0f},
  });
  s.regions.push_back(std::move(reg));

  auto bytes = firc::EncodeToFormat(fir::SourceFormat::kLitematica, s);
  ASSERT_TRUE(bytes.has_value());

  auto owner =
      std::make_unique<std::vector<std::byte>>(std::move(*bytes));
  auto decoded =
      firc::DecodeFromFormat(fir::SourceFormat::kLitematica, std::move(owner));
  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ(decoded->regions.size(), 1u);
  const fir::Region& out = decoded->regions[0];

  // On-disk shape and the inclusive-corner origin both survive.
  EXPECT_EQ(out.raw_position, (std::array<std::int32_t, 3>{5, 6, 7}));
  EXPECT_EQ(out.raw_size, (std::array<std::int32_t, 3>{-2, -2, -2}));
  EXPECT_EQ(out.bounds.origin[0], 4);
  EXPECT_EQ(out.bounds.origin[1], 5);
  EXPECT_EQ(out.bounds.origin[2], 6);
  EXPECT_EQ(out.bounds.size[0], 2);
  EXPECT_EQ(out.bounds.size[1], 2);
  EXPECT_EQ(out.bounds.size[2], 2);

  // The entity survives at the same world position: the encoder wrote
  // Pos relative to Position, the decoder adds Position back.
  ASSERT_EQ(out.entities.size(), 1u);
  EXPECT_EQ(out.entities[0].id, "minecraft:sheep");
  EXPECT_NEAR(out.entities[0].position[0], 4.5, 1e-12);
  EXPECT_NEAR(out.entities[0].position[1], 6.0, 1e-12);
  EXPECT_NEAR(out.entities[0].position[2], 6.5, 1e-12);

  // The stone lands back at the minimum corner, so the entity's feet
  // rest exactly on its top face, one block above the origin plane.
  ASSERT_TRUE(fir::EnsureMaterialized(out, *decoded->arena).has_value());
  const std::uint16_t stone_pal =
      out.block_indices[filt::LinearIndex({0, 0, 0}, out.bounds)];
  EXPECT_EQ(out.palette[stone_pal].name, "minecraft:stone");
  EXPECT_DOUBLE_EQ(out.entities[0].position[1],
                   static_cast<double>(out.bounds.origin[1] + 1));
}