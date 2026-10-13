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

#include "fschema/base/compressor.h"
#include "fschema/base/error.h"
#include "fschema/filters/axis_filter.h"
#include "fschema/filters/block_name_filter.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/composite_filter.h"
#include "fschema/filters/neighbor_filter.h"
#include "fschema/filters/random_filter.h"
#include "fschema/filters/sphere_filter.h"
#include "fschema/filters/surface_filter.h"
#include "fschema/filters/view.h"
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematica_handler.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/schem_handler.h"
#include "fschema/ir/types.h"
#include "fschema/tests/testdata_util.h"

namespace fs = std::filesystem;
namespace fir = fschema::ir;
namespace firc = fschema::ir::format;
namespace filt = fschema::filters;

namespace {

struct LoadedSchema {
  std::string filename;
  std::unique_ptr<fir::Schema> schema;
};

[[nodiscard]] std::vector<LoadedSchema> LoadAllSchemas() {
  std::vector<LoadedSchema> out;
  const fs::path dir = fschema::test::GetTestDataDir();
  if (!fs::exists(dir)) return out;

  static bool initialized = false;
  if (!initialized) {
    firc::FormatRegistry::Instance().Register(
        std::make_unique<firc::LitematicaHandler>());
    firc::FormatRegistry::Instance().Register(
        std::make_unique<firc::SchemHandler>());
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
                                : fir::SourceFormat::kSchem;

    auto schema_res = firc::DecodeFromFormat(fmt, std::move(owner));
    if (!schema_res) continue;

    out.push_back({path.filename().string(),
                   std::make_unique<fir::Schema>(std::move(*schema_res))});
  }
  return out;
}

const std::vector<LoadedSchema>& GetTestSchemas() {
  static const std::vector<LoadedSchema> schemas = LoadAllSchemas();
  return schemas;
}

class FilterTest : public ::testing::TestWithParam<size_t> {
 protected:
  void SetUp() override {
    const auto& schemas = GetTestSchemas();
    index_ = GetParam();
    ASSERT_LT(index_, schemas.size());
    schema_ = schemas[index_].schema.get();
    ASSERT_FALSE(schema_->regions.empty());
    region_ = &schema_->regions[0];

    // Filters read block_indices directly, so materialization is a
    // precondition for every test in this suite.
    auto mat_res = fir::EnsureMaterialized(*region_, *schema_->arena);
    ASSERT_TRUE(mat_res.has_value()) << "Materialization failed";
  }

  size_t index_ = 0;
  fir::Schema* schema_ = nullptr;
  fir::Region* region_ = nullptr;
};

TEST_P(FilterTest, BoxFilterSelectsCorrectRegion) {
  const auto& b = region_->bounds;
  if (b.size[0] == 0 || b.size[1] == 0 || b.size[2] == 0) GTEST_SKIP();

  filt::BoxFilter box{0, 0, 0, b.size[0] / 2, b.size[1] / 2, b.size[2] / 2};
  auto view_res = filt::MakeView(*region_, box, *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  std::size_t count = 0;
  view_res->ForEach([&](filt::LocalPos p, std::uint16_t) {
    ASSERT_LE(p.x, b.size[0] / 2);
    ASSERT_LE(p.y, b.size[1] / 2);
    ASSERT_LE(p.z, b.size[2] / 2);
    ++count;
  });
  EXPECT_GT(count, 0u);
}

TEST_P(FilterTest, SphereFilterSelectsCorrectRegion) {
  const auto& b = region_->bounds;
  if (b.size[0] == 0 || b.size[1] == 0 || b.size[2] == 0) GTEST_SKIP();

  std::int32_t cx = b.size[0] / 2;
  std::int32_t cy = b.size[1] / 2;
  std::int32_t cz = b.size[2] / 2;
  std::int64_t r2 = static_cast<std::int64_t>(cx) * cx +
                    static_cast<std::int64_t>(cy) * cy +
                    static_cast<std::int64_t>(cz) * cz;

  filt::SphereFilter sphere{cx, cy, cz, r2};
  auto view_res = filt::MakeView(*region_, sphere, *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  view_res->ForEach([&](filt::LocalPos p, std::uint16_t) {
    std::int64_t dx = p.x - cx;
    std::int64_t dy = p.y - cy;
    std::int64_t dz = p.z - cz;
    EXPECT_LE(dx * dx + dy * dy + dz * dz, r2);
  });
}

TEST_P(FilterTest, AxisFilterSelectsCorrectRegion) {
  const auto& b = region_->bounds;
  if (b.size[1] <= 1) GTEST_SKIP();

  filt::AxisFilter axis{filt::Axis::kY, filt::Cmp::kGe, b.size[1] / 2};
  auto view_res = filt::MakeView(*region_, axis, *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  view_res->ForEach(
      [&](filt::LocalPos p, std::uint16_t) { EXPECT_GE(p.y, b.size[1] / 2); });
}

TEST_P(FilterTest, BlockNameFilterMatchesCorrectName) {
  if (region_->palette.empty()) GTEST_SKIP();

  // Use the last palette entry to exercise non-trivial name matching.
  std::string_view target_name = region_->palette.back().name;
  filt::BlockNameFilter filter(target_name, *region_);
  auto view_res = filt::MakeView(*region_, filter, *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  view_res->ForEach([&](filt::LocalPos, std::uint16_t pal_idx) {
    ASSERT_LT(pal_idx, region_->palette.size());
    EXPECT_EQ(region_->palette[pal_idx].name, target_name);
  });
}

TEST_P(FilterTest, IsAirFilterMatchesOnlyAir) {
  filt::IsAirFilter filter(*region_);
  auto view_res = filt::MakeView(*region_, filter, *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  view_res->ForEach([&](filt::LocalPos, std::uint16_t pal_idx) {
    ASSERT_LT(pal_idx, region_->palette.size());
    EXPECT_TRUE(fschema::base::IsAirVariant(region_->palette[pal_idx].name));
  });
}

TEST_P(FilterTest, SurfaceFilterMatchesExposedBlocks) {
  filt::SurfaceFilter filter(*region_);
  auto view_res = filt::MakeView(*region_, filter, *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  const auto& b = region_->bounds;
  view_res->ForEach([&](filt::LocalPos p, std::uint16_t pal_idx) {
    ASSERT_LT(pal_idx, region_->palette.size());
    EXPECT_FALSE(region_->IsAir(pal_idx));

    // Surface blocks are either on the boundary or adjacent to air.
    bool is_exposed = (p.x == 0 || p.x == b.size[0] - 1) ||
                      (p.y == 0 || p.y == b.size[1] - 1) ||
                      (p.z == 0 || p.z == b.size[2] - 1);

    if (!is_exposed) {
      is_exposed = region_->IsAir(region_->block_indices[filt::LinearIndex(
                       {p.x - 1, p.y, p.z}, b)]) ||
                   region_->IsAir(region_->block_indices[filt::LinearIndex(
                       {p.x + 1, p.y, p.z}, b)]) ||
                   region_->IsAir(region_->block_indices[filt::LinearIndex(
                       {p.x, p.y - 1, p.z}, b)]) ||
                   region_->IsAir(region_->block_indices[filt::LinearIndex(
                       {p.x, p.y + 1, p.z}, b)]) ||
                   region_->IsAir(region_->block_indices[filt::LinearIndex(
                       {p.x, p.y, p.z - 1}, b)]) ||
                   region_->IsAir(region_->block_indices[filt::LinearIndex(
                       {p.x, p.y, p.z + 1}, b)]);
    }
    EXPECT_TRUE(is_exposed);
  });
}

TEST_P(FilterTest, NeighborFilterDetectsAdjacentAir) {
  filt::IsAirFilter air_filter(*region_);
  filt::NeighborFilter neighbor_filter(*region_, air_filter);
  auto view_res = filt::MakeView(*region_, neighbor_filter, *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  const auto& b = region_->bounds;
  view_res->ForEach([&](filt::LocalPos p, std::uint16_t) {
    // NeighborFilter should only match blocks adjacent to air.
    bool has_air_neighbor = false;
    if (p.x > 0)
      has_air_neighbor |= region_->IsAir(
          region_->block_indices[filt::LinearIndex({p.x - 1, p.y, p.z}, b)]);
    if (p.x < b.size[0] - 1)
      has_air_neighbor |= region_->IsAir(
          region_->block_indices[filt::LinearIndex({p.x + 1, p.y, p.z}, b)]);
    if (p.y > 0)
      has_air_neighbor |= region_->IsAir(
          region_->block_indices[filt::LinearIndex({p.x, p.y - 1, p.z}, b)]);
    if (p.y < b.size[1] - 1)
      has_air_neighbor |= region_->IsAir(
          region_->block_indices[filt::LinearIndex({p.x, p.y + 1, p.z}, b)]);
    if (p.z > 0)
      has_air_neighbor |= region_->IsAir(
          region_->block_indices[filt::LinearIndex({p.x, p.y, p.z - 1}, b)]);
    if (p.z < b.size[2] - 1)
      has_air_neighbor |= region_->IsAir(
          region_->block_indices[filt::LinearIndex({p.x, p.y, p.z + 1}, b)]);
    EXPECT_TRUE(has_air_neighbor);
  });
}

TEST_P(FilterTest, RandomFilterProb1SelectsAll) {
  filt::RandomFilter filter(1.0);
  auto view_res = filt::MakeView(*region_, filter, *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  std::size_t total_blocks = region_->bounds.size[0] * region_->bounds.size[1] *
                             region_->bounds.size[2];
  EXPECT_EQ(view_res->Count(), total_blocks);
}

TEST_P(FilterTest, RandomFilterProb0SelectsNone) {
  filt::RandomFilter filter(0.0);
  auto view_res = filt::MakeView(*region_, filter, *schema_->arena);
  ASSERT_TRUE(view_res.has_value());
  EXPECT_EQ(view_res->Count(), 0u);
}

TEST_P(FilterTest, CompositeFilterAndLogic) {
  const auto& b = region_->bounds;
  if (b.size[0] == 0) GTEST_SKIP();

  filt::BoxFilter box{0, 0, 0, 0, b.size[1] - 1, b.size[2] - 1};
  filt::IsAirFilter air_filter(*region_);
  auto composite = filt::And(box, air_filter);

  auto view_res = filt::MakeView(*region_, composite, *schema_->arena);
  ASSERT_TRUE(view_res.has_value());

  view_res->ForEach([&](filt::LocalPos p, std::uint16_t pal_idx) {
    ASSERT_EQ(p.x, 0);
    EXPECT_TRUE(region_->IsAir(pal_idx));
  });
}

INSTANTIATE_TEST_SUITE_P(AllSampleFiles,
                         FilterTest,
                         ::testing::Range(size_t{0}, GetTestSchemas().size()));

}  // namespace