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
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/geometry/export.h"
#include "fschema/geometry/resource_pack.h"
#include "fschema/geometry/tessellate.h"
#include "fschema/geometry/tests/geometry_test_util.h"
#include "fschema/geometry/types.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"

namespace {

using fschema::ToString;
using fschema::geometry::ExportGlb;
using fschema::geometry::ResourcePack;
using fschema::geometry::TessellateRegion;
using fschema::geometry::TessellateSchematic;
using fschema::geometry::TessellationOptions;
using fschema::geometry::TessellationResult;
using fschema::ir::BlockState;
using fschema::ir::BoundingBox;
using fschema::ir::PropertyEncoding;
using fschema::ir::Region;
using fschema::ir::Schematic;
using fschema::ir::SourceFormat;
using fschema::memory::Arena;
using fschema::memory::UnInitBuffer;
using fschema::tests::ResourcePackFixture;

// Build a synthetic ir::Schematic with 2 regions:
//   Region "main":   bounds origin (0, 0, 0),   size (2, 2, 2), 8 stone
//   Region "offset": bounds origin (10, 0, 0), size (1, 1, 1), 1 stone
// The 2x2x2 cube has 24 exterior faces; the lone block has 6.
// Total: 30 visible faces = 120 vertices, 60 triangles.
struct TwoRegionSchematic {
  std::unique_ptr<Arena> arena = std::make_unique<Arena>();
  Schematic schematic{};

  TwoRegionSchematic() {
    schematic.source_format = SourceFormat::kLitematica;
    schematic.source_version = 6;
    schematic.data_version = 3105;
    schematic.arena = std::move(arena);
    schematic.regions.resize(2);

    // Region 0: "main" at (0,0,0), size (2,2,2), all stone.
    {
      Region& r = schematic.regions[0];
      r.name = "main";
      r.bounds = BoundingBox{/*.origin=*/{0, 0, 0}, /*.size=*/{2, 2, 2}};
      r.palette = {
          BlockState{.name = "minecraft:air",
                     .prop_encoding = PropertyEncoding::kNone},
          BlockState{.name = "minecraft:stone",
                     .prop_encoding = PropertyEncoding::kNone},
      };
      r.is_air_lut.assign(2, 0);
      r.is_air_lut[0] = 1;
      r.block_indices = UnInitBuffer<std::uint16_t>(8, schematic.arena.get());
      std::uint16_t* data =
          reinterpret_cast<std::uint16_t*>(r.block_indices.data());
      for (int i = 0; i < 8; ++i) data[i] = 1;
      r.is_materialized = true;
      r.lazy_source.encoding = fschema::ir::BlockDataEncoding::kNone;
    }

    // Region 1: "offset" at (10,0,0), size (1,1,1), all stone.
    {
      Region& r = schematic.regions[1];
      r.name = "offset";
      r.bounds = BoundingBox{/*.origin=*/{10, 0, 0}, /*.size=*/{1, 1, 1}};
      r.palette = {
          BlockState{.name = "minecraft:air",
                     .prop_encoding = PropertyEncoding::kNone},
          BlockState{.name = "minecraft:stone",
                     .prop_encoding = PropertyEncoding::kNone},
      };
      r.is_air_lut.assign(2, 0);
      r.is_air_lut[0] = 1;
      r.block_indices = UnInitBuffer<std::uint16_t>(1, schematic.arena.get());
      std::uint16_t* data =
          reinterpret_cast<std::uint16_t*>(r.block_indices.data());
      data[0] = 1;
      r.is_materialized = true;
      r.lazy_source.encoding = fschema::ir::BlockDataEncoding::kNone;
    }
  }
};

TEST(SchematicTest, TwoRegionSchematicProducesCorrectTriangleCount) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  TwoRegionSchematic ts;
  auto r = TessellateSchematic(ts.schematic, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // 2x2x2 cube: 24 visible faces = 48 triangles
  // 1x1x1 block: 6 visible faces = 12 triangles
  // Total: 60 triangles
  EXPECT_EQ(r->opaque_mesh.TriangleCount(), 60u);
  EXPECT_EQ(r->opaque_mesh.VertexCount(), 120u);
}

TEST(SchematicTest, WorldSpacePositionsSpanBothRegions) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  TwoRegionSchematic ts;
  auto r = TessellateSchematic(ts.schematic, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value());

  // Find min/max X across all vertices.
  float min_x = 1e30f, max_x = -1e30f;
  for (std::size_t i = 0; i < r->opaque_mesh.positions.size(); i += 3) {
    const float x = r->opaque_mesh.positions[i];
    min_x = std::min(min_x, x);
    max_x = std::max(max_x, x);
  }
  // Region 0 spans X [0, 2]. Region 1 spans X [10, 11].
  EXPECT_NEAR(min_x, 0.0f, 0.01f);
  EXPECT_NEAR(max_x, 11.0f, 0.01f);
}

TEST(SchematicTest, SharedAtlasHasOneSlot) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  TwoRegionSchematic ts;
  auto r = TessellateSchematic(ts.schematic, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value());

  // Both regions use the same texture (minecraft:block/stone).
  // Atlas should have exactly 1 slot.
  EXPECT_EQ(r->atlas.slots.size(), 1u);
}

TEST(SchematicTest, ExportGlbSucceedsForMultiRegion) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  TwoRegionSchematic ts;
  auto tess = TessellateSchematic(ts.schematic, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto glb = ExportGlb(*tess);
  ASSERT_TRUE(glb.has_value()) << ToString(glb.error());
  EXPECT_GT(glb->bytes.size(), 100u);  // at least a header + minimal JSON
}

TEST(SchematicTest, EmptySchematicProducesNoGeometry) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  Schematic empty{};
  empty.source_format = SourceFormat::kLitematica;
  empty.source_version = 6;
  empty.data_version = 3105;
  auto arena = std::make_unique<Arena>();
  empty.arena = std::move(arena);

  auto r = TessellateSchematic(empty, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  EXPECT_EQ(r->opaque_mesh.VertexCount(), 0u);
  EXPECT_EQ(r->opaque_mesh.TriangleCount(), 0u);
}

TEST(SchematicTest, SingleRegionSchematicMatchesTessellateRegion) {
  // A schematic with 1 region should produce the same triangle count
  // as calling TessellateRegion directly.
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // Build a 1-region schematic with 1x1x1 stone.
  auto arena = std::make_unique<Arena>();
  Schematic schematic{};
  schematic.source_format = SourceFormat::kLitematica;
  schematic.source_version = 6;
  schematic.data_version = 3105;
  schematic.arena = std::move(arena);
  schematic.regions.resize(1);
  {
    Region& r = schematic.regions[0];
    r.name = "main";
    r.bounds = BoundingBox{/*.origin=*/{0, 0, 0}, /*.size=*/{1, 1, 1}};
    r.palette = {
        BlockState{.name = "minecraft:air",
                   .prop_encoding = PropertyEncoding::kNone},
        BlockState{.name = "minecraft:stone",
                   .prop_encoding = PropertyEncoding::kNone},
    };
    r.is_air_lut.assign(2, 0);
    r.is_air_lut[0] = 1;
    r.block_indices = UnInitBuffer<std::uint16_t>(1, schematic.arena.get());
    std::uint16_t* data =
        reinterpret_cast<std::uint16_t*>(r.block_indices.data());
    data[0] = 1;
    r.is_materialized = true;
    r.lazy_source.encoding = fschema::ir::BlockDataEncoding::kNone;
  }

  auto sch_result =
      TessellateSchematic(schematic, **pack, TessellationOptions{});
  ASSERT_TRUE(sch_result.has_value());

  auto reg_result = TessellateRegion(
      schematic.regions[0], *schematic.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(reg_result.has_value());

  EXPECT_EQ(sch_result->opaque_mesh.TriangleCount(),
            reg_result->opaque_mesh.TriangleCount());
}

}  // namespace
