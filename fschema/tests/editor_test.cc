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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fschema/base/block_utils.h"
#include "fschema/base/compressor.h"
#include "fschema/base/error.h"
#include "fschema/base/nbt/reader.h"
#include "fschema/base/nbt/skip.h"
#include "fschema/base/nbt/tag.h"
#include "fschema/editors/bounding_box_trim.h"
#include "fschema/editors/erase_nbt.h"
#include "fschema/editors/extract.h"
#include "fschema/editors/fill.h"
#include "fschema/editors/hollow.h"
#include "fschema/editors/mirror.h"
#include "fschema/editors/move.h"
#include "fschema/editors/replace.h"
#include "fschema/editors/replace_palette_entry.h"
#include "fschema/editors/rotate.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematica_handler.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/sponge_handler.h"
#include "fschema/ir/types.h"
#include "fschema/tests/testdata_util.h"

namespace fs = std::filesystem;
namespace fir = fschema::ir;
namespace firc = fschema::ir::format;
namespace filt = fschema::filters;
namespace ed = fschema::editors;

namespace {

class EditorTest : public ::testing::TestWithParam<fs::path> {
 protected:
  void SetUp() override {
    path_ = GetParam();
    ext_ = path_.extension().string();

    static bool initialized = false;
    if (!initialized) {
      firc::FormatRegistry::Instance().Register(
          std::make_unique<firc::LitematicaHandler>());
      firc::FormatRegistry::Instance().Register(
          std::make_unique<firc::SpongeHandler>());
      initialized = true;
    }

    auto decompressed = fschema::base::DecompressGzipFile(path_);
    ASSERT_TRUE(decompressed.has_value()) << "Decompress failed for " << path_;
    ASSERT_FALSE(decompressed->empty());

    auto owner =
        std::make_unique<std::vector<std::byte>>(std::move(*decompressed));
    fir::SourceFormat fmt = (ext_ == ".litematic")
                                ? fir::SourceFormat::kLitematica
                                : fir::SourceFormat::kSponge;

    auto schematic_res = firc::DecodeFromFormat(fmt, std::move(owner));
    ASSERT_TRUE(schematic_res.has_value()) << "Decode failed for " << path_;
    schema_ = std::make_unique<fir::Schematic>(std::move(*schematic_res));

    ASSERT_FALSE(schema_->regions.empty());
    region_ = &schema_->regions[0];

    auto mat_res = fir::EnsureMaterialized(*region_, *schema_->arena);
    ASSERT_TRUE(mat_res.has_value()) << "Materialization failed";
  }

  fs::path path_;
  std::string ext_;
  std::unique_ptr<fir::Schematic> schema_;
  fir::Region* region_;
};

TEST_P(EditorTest, FillModifiesBlocks) {
  const auto& b = region_->bounds;
  if (b.size[0] == 0 || b.size[1] == 0 || b.size[2] == 0) GTEST_SKIP();

  filt::BoxFilter box{0, 0, 0, b.size[0] / 2, b.size[1] / 2, b.size[2] / 2};
  auto view_res = filt::MakeView(*region_, box, *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  std::string_view target_block = "minecraft:stone";
  auto fill_res = ed::Fill(*view_res, target_block);
  ASSERT_TRUE(fill_res.has_value()) << "Fill failed";

  view_res->ForEach([&](filt::LocalPos p, std::uint16_t pal_idx) {
    ASSERT_LT(pal_idx, region_->palette.size());
    EXPECT_EQ(region_->palette[pal_idx].name, target_block);
  });

  EXPECT_TRUE(region_->edited);
}

TEST_P(EditorTest, ReplaceWithChangesPalette) {
  if (region_->palette.empty()) GTEST_SKIP();

  // Pick any non-air palette entry as the replacement source.
  std::string_view from_block;
  std::uint16_t from_idx = 0;
  for (std::size_t i = 0; i < region_->palette.size(); ++i) {
    if (!fschema::base::IsAirVariant(region_->palette[i].name)) {
      from_block = region_->palette[i].name;
      from_idx = static_cast<std::uint16_t>(i);
      break;
    }
  }
  if (from_block.empty()) GTEST_SKIP();

  std::string_view to_block = "minecraft:dirt";
  auto view_res = filt::MakeView(*region_,
                                 filt::BoxFilter{0,
                                                 0,
                                                 0,
                                                 region_->bounds.size[0] - 1,
                                                 region_->bounds.size[1] - 1,
                                                 region_->bounds.size[2] - 1},
                                 *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  auto res = ed::ReplaceWith(*view_res, from_block, to_block);
  ASSERT_TRUE(res.has_value());

  for (std::uint16_t idx : region_->block_indices) {
    EXPECT_NE(idx, from_idx);
  }
}

TEST_P(EditorTest, RotateFourTimesReturnsOriginal) {
  const auto& b = region_->bounds;
  if (b.size[0] == 0 || b.size[1] == 0 || b.size[2] == 0) GTEST_SKIP();

  std::int32_t orig_size[3] = {b.size[0], b.size[1], b.size[2]};

  auto view_res = filt::MakeView(
      *region_,
      filt::BoxFilter{0, 0, 0, b.size[0] - 1, b.size[1] - 1, b.size[2] - 1},
      *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  fir::Region current = std::move(*region_);
  schema_->regions[0] = {};

  for (int i = 0; i < 4; ++i) {
    auto v = filt::MakeView(current,
                            filt::BoxFilter{0,
                                            0,
                                            0,
                                            current.bounds.size[0] - 1,
                                            current.bounds.size[1] - 1,
                                            current.bounds.size[2] - 1},
                            *schema_->arena);
    auto rot_res = ed::Rotate(*v, filt::Axis::kY, 1, *schema_->arena);
    ASSERT_TRUE(rot_res.has_value());
    current = std::move(*rot_res);
  }

  EXPECT_EQ(current.bounds.size[0], orig_size[0]);
  EXPECT_EQ(current.bounds.size[1], orig_size[1]);
  EXPECT_EQ(current.bounds.size[2], orig_size[2]);
}

TEST_P(EditorTest, MoveShiftsBlocks) {
  const auto& b = region_->bounds;
  if (b.size[0] <= 1 || b.size[1] == 0 || b.size[2] == 0) GTEST_SKIP();

  auto view_res =
      filt::MakeView(*region_,
                     filt::BoxFilter{0, 0, 0, 0, b.size[1] - 1, b.size[2] - 1},
                     *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  auto move_res = ed::Move(*view_res, 1, 0, 0);
  ASSERT_TRUE(move_res.has_value());

  // After shifting +X by 1, the source layer at x=0 must be cleared.
  for (int y = 0; y < b.size[1]; ++y) {
    for (int z = 0; z < b.size[2]; ++z) {
      std::uint16_t pal =
          region_->block_indices[filt::LinearIndex({0, y, z}, b)];
      EXPECT_TRUE(region_->IsAir(pal));
    }
  }
}

TEST_P(EditorTest, ExtractShrinksBounds) {
  const auto& b = region_->bounds;
  if (b.size[0] < 3 || b.size[1] < 3 || b.size[2] < 3) GTEST_SKIP();

  auto view_res = filt::MakeView(
      *region_, filt::BoxFilter{1, 1, 1, 2, 2, 2}, *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  auto extract_res = ed::Extract(*view_res, *schema_->arena);
  ASSERT_TRUE(extract_res.has_value());

  EXPECT_EQ(extract_res->bounds.size[0], 2);
  EXPECT_EQ(extract_res->bounds.size[1], 2);
  EXPECT_EQ(extract_res->bounds.size[2], 2);
}

TEST_P(EditorTest, HollowRemovesInterior) {
  const auto& b = region_->bounds;
  if (b.size[0] < 3 || b.size[1] < 3 || b.size[2] < 3) GTEST_SKIP();

  auto view_res = filt::MakeView(
      *region_,
      filt::BoxFilter{0, 0, 0, b.size[0] - 1, b.size[1] - 1, b.size[2] - 1},
      *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  auto hollow_res = ed::Hollow(*view_res, *schema_->arena);
  ASSERT_TRUE(hollow_res.has_value());

  // Hollow should clear the interior, including the center.
  std::uint16_t center_pal = region_->block_indices[filt::LinearIndex(
      {b.size[0] / 2, b.size[1] / 2, b.size[2] / 2}, b)];
  EXPECT_TRUE(region_->IsAir(center_pal));
}

TEST_P(EditorTest, BoundingBoxTrimReducesSize) {
  const auto& b = region_->bounds;
  if (b.size[0] < 3 || b.size[1] < 3 || b.size[2] < 3) GTEST_SKIP();

  auto trim_res = ed::BoundingBoxTrim(*region_, *schema_->arena);
  ASSERT_TRUE(trim_res.has_value());

  EXPECT_LE(trim_res->bounds.size[0], b.size[0]);
  EXPECT_LE(trim_res->bounds.size[1], b.size[1]);
  EXPECT_LE(trim_res->bounds.size[2], b.size[2]);
}

TEST_P(EditorTest, EraseNbtRemovesFields) {
  if (region_->block_entities.empty()) GTEST_SKIP();

  auto& be = region_->block_entities[0];
  std::vector<std::byte> original_nbt(be.raw_nbt.begin(), be.raw_nbt.end());

  std::string_view target_ids[] = {be.id};
  std::string_view fields_to_erase[] = {"id", "Id", "x", "y", "z"};

  auto view_res = filt::MakeView(*region_,
                                 filt::BoxFilter{0,
                                                 0,
                                                 0,
                                                 region_->bounds.size[0] - 1,
                                                 region_->bounds.size[1] - 1,
                                                 region_->bounds.size[2] - 1},
                                 *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  auto erase_res =
      ed::EraseNbt(*view_res,
                   std::span<const std::string_view>(target_ids),
                   std::span<const std::string_view>(fields_to_erase),
                   *schema_->arena);
  ASSERT_TRUE(erase_res.has_value());

  // EraseNbt should rewrite the payload, so size or content must change.
  EXPECT_NE(be.raw_nbt.size(), original_nbt.size());
}

INSTANTIATE_TEST_SUITE_P(AllSampleFiles, EditorTest, ::testing::ValuesIn([]() {
                           std::vector<fs::path> paths;
                           const fs::path dir = fschema::test::GetTestDataDir();
                           if (fs::exists(dir)) {
                             for (const auto& entry :
                                  fs::directory_iterator(dir)) {
                               if (entry.is_regular_file()) {
                                 auto ext = entry.path().extension();
                                 if (ext == ".litematic" || ext == ".schem") {
                                   paths.push_back(entry.path());
                                 }
                               }
                             }
                           }
                           return paths;
                         }()));

}  // namespace