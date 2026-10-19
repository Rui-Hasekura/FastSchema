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
#include <string>
#include <vector>

#include "fschema/base/block_utils.h"
#include "fschema/base/compressor.h"
#include "fschema/base/error.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/internal/neighbors.h"
#include "fschema/filters/view.h"
#include "fschema/inspectors/count.h"
#include "fschema/inspectors/diff.h"
#include "fschema/inspectors/floating_check.h"
#include "fschema/inspectors/list_entity.h"
#include "fschema/inspectors/list_material.h"
#include "fschema/inspectors/list_tile_entity.h"
#include "fschema/inspectors/support_check.h"
#include "fschema/inspectors/version_check.h"
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
namespace insp = fschema::inspectors;

namespace {

struct LoadedSchematic {
  std::string filename;
  std::unique_ptr<fir::Schematic> schematic;
};

[[nodiscard]] std::vector<LoadedSchematic> LoadAllSchemas() {
  std::vector<LoadedSchematic> out;
  const fs::path dir = fschema::test::GetTestDataDir();
  if (!fs::exists(dir)) return out;

  static bool initialized = false;
  if (!initialized) {
    firc::FormatRegistry::Instance().Register(
        std::make_unique<firc::LitematicaHandler>());
    firc::FormatRegistry::Instance().Register(
        std::make_unique<firc::SpongeHandler>());
    initialized = true;
  }

  for (const auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    auto path = entry.path();
    auto ext = path.extension();
    if (ext != ".litematic" && ext != ".schem") continue;

    auto decompressed = fschema::base::DecompressGzipFile(path);
    if (!decompressed) continue;

    auto owner =
        std::make_unique<std::vector<std::byte>>(std::move(*decompressed));
    fir::SourceFormat fmt = (ext == ".litematic")
                                ? fir::SourceFormat::kLitematica
                                : fir::SourceFormat::kSponge;

    auto schematic_res = firc::DecodeFromFormat(fmt, std::move(owner));
    if (!schematic_res) continue;

    out.push_back({path.filename().string(),
                   std::make_unique<fir::Schematic>(std::move(*schematic_res))});
  }
  return out;
}

const std::vector<LoadedSchematic>& GetTestSchemas() {
  static const std::vector<LoadedSchematic> schematics = LoadAllSchemas();
  return schematics;
}

class InspectorTest : public ::testing::TestWithParam<size_t> {
 protected:
  void SetUp() override {
    const auto& schematics = GetTestSchemas();
    index_ = GetParam();
    ASSERT_LT(index_, schematics.size());
    schema_ = schematics[index_].schematic.get();
    ASSERT_FALSE(schema_->regions.empty());
    region_ = &schema_->regions[0];

    auto mat_res = fir::EnsureMaterialized(*region_, *schema_->arena);
    ASSERT_TRUE(mat_res.has_value()) << "Materialization failed";

    // Full-region view for view-based inspectors such as Count and
    // VersionCheck.
    const auto& b = region_->bounds;
    filt::BoxFilter all_box{
        0, 0, 0, b.size[0] - 1, b.size[1] - 1, b.size[2] - 1};
    auto view_res = filt::MakeView(*region_, all_box, *schema_->arena);
    ASSERT_TRUE(view_res.has_value());
    view_ = std::make_unique<filt::View>(*view_res);
  }

  size_t index_ = 0;
  fir::Schematic* schema_ = nullptr;
  fir::Region* region_ = nullptr;
  std::unique_ptr<filt::View> view_ = nullptr;
};

TEST_P(InspectorTest, CountCalculatesCorrectly) {
  if (region_->palette.empty()) GTEST_SKIP();

  std::size_t air_count = insp::Count(*view_, "minecraft:air");

  // Independent reference count over the raw palette indices.
  std::size_t manual_air_count = 0;
  const std::uint16_t* data = region_->block_indices.data();
  std::size_t vol = region_->block_indices.size();
  for (std::size_t i = 0; i < vol; ++i) {
    std::uint16_t pal = data[i];
    if (pal < region_->palette.size() &&
        region_->palette[pal].name == "minecraft:air") {
      manual_air_count++;
    }
  }

  EXPECT_EQ(air_count, manual_air_count);
}

TEST_P(InspectorTest, ListMaterialSumsToVolume) {
  auto materials = insp::ListMaterial(*region_, false);

  std::uint64_t total_count = 0;
  for (const auto& entry : materials) {
    total_count += entry.count;
  }

  std::uint64_t volume = static_cast<std::uint64_t>(region_->bounds.size[0]) *
                         static_cast<std::uint64_t>(region_->bounds.size[1]) *
                         static_cast<std::uint64_t>(region_->bounds.size[2]);

  EXPECT_EQ(total_count, volume);
  EXPECT_EQ(materials.size(), region_->palette.size());
}

TEST_P(InspectorTest, ListEntityMatchesRegion) {
  auto entities = insp::ListEntity(*region_, false);
  EXPECT_EQ(entities.size(), region_->entities.size());
}

TEST_P(InspectorTest, ListTileEntityMatchesRegion) {
  auto tile_entities = insp::ListTileEntity(*region_, false);
  EXPECT_EQ(tile_entities.size(), region_->block_entities.size());
}

TEST_P(InspectorTest, DiffSameRegionIsEmpty) {
  auto diff_res = insp::Diff(*region_, *region_);
  ASSERT_TRUE(diff_res.has_value()) << "Diff failed unexpectedly";
  EXPECT_TRUE(diff_res->empty());
}

TEST_P(InspectorTest, FloatingCheckFindsValidBlocks) {
  auto result = insp::FloatingCheck(*region_);

  const auto& b = region_->bounds;
  for (const auto& fb : result.blocks) {
    ASSERT_LT(fb.pos.x, b.size[0]);
    ASSERT_LT(fb.pos.y, b.size[1]);
    ASSERT_LT(fb.pos.z, b.size[2]);

    std::uint16_t pal = region_->block_indices[filt::LinearIndex(fb.pos, b)];
    ASSERT_FALSE(region_->IsAir(pal));

    // Cross-check the reported position against the floating predicate.
    EXPECT_TRUE(
        filt::internal::IsFloatingAt(*region_, fb.pos.x, fb.pos.y, fb.pos.z));
  }
}

TEST_P(InspectorTest, SupportCheckFindsValidBlocks) {
  auto result = insp::SupportCheck(*region_);

  const auto& rule_lookup = insp::GetSupportRuleLookup();
  const auto& b = region_->bounds;

  for (const auto& ub : result) {
    // Only blocks with a support rule may be reported as unsupported.
    EXPECT_TRUE(rule_lookup.contains(ub.block_name));

    ASSERT_GE(ub.pos.x, 0);
    ASSERT_LT(ub.pos.x, b.size[0]);
    ASSERT_GE(ub.pos.y, 0);
    ASSERT_LT(ub.pos.y, b.size[1]);
    ASSERT_GE(ub.pos.z, 0);
    ASSERT_LT(ub.pos.z, b.size[2]);
  }
}

TEST_P(InspectorTest, VersionCheckReturnsValidData) {
  std::int32_t min_req = insp::GetMinimumRequiredDataVersion(*view_);
  EXPECT_GE(min_req, 0);

  // May be INT_MAX when no block is scheduled for removal.
  std::int32_t highest_sup = insp::GetHighestSupportedDataVersion(*view_);
  EXPECT_GE(highest_sup, 0);
}

INSTANTIATE_TEST_SUITE_P(AllSampleFiles,
                         InspectorTest,
                         ::testing::Range(size_t{0}, GetTestSchemas().size()));

}  // namespace