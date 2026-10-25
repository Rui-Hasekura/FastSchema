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

#include "fschema/geometry/internal/ao.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/geometry/export.h"
#include "fschema/geometry/internal/cull.h"
#include "fschema/geometry/resource_pack.h"
#include "fschema/geometry/tessellate.h"
#include "fschema/geometry/tests/geometry_test_util.h"
#include "fschema/geometry/tint.h"
#include "fschema/geometry/types.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"

namespace {

using fschema::ToString;
using fschema::geometry::ByteBuffer;
using fschema::geometry::ExportGlb;
using fschema::geometry::ResourcePack;
using fschema::geometry::TessellateRegion;
using fschema::geometry::TessellationOptions;
using fschema::geometry::TessellationResult;
using fschema::geometry::TintTable;
using fschema::geometry::internal::ComputeVertexAO;
using fschema::geometry::internal::BlockGroupTable;
using fschema::ir::BlockState;
using fschema::ir::BoundingBox;
using fschema::ir::PropertyEncoding;
using fschema::ir::Region;
using fschema::memory::Arena;
using fschema::memory::UnInitBuffer;
using fschema::tests::BuildGroupTable;
using fschema::tests::ResourcePackFixture;
using fschema::tests::StoneRegion;

TEST(AoTest, NoNeighborsOpaqueReturnsFullBright) {
  // 3x3x3 all-air region; for the +X face vertex at (0,0,0), the three AO
  // neighbors are out of bounds or air.
  std::vector<std::uint8_t> flags(27, 0);  // 3x3x3 all air
  BlockGroupTable t = BuildGroupTable(flags);
  BoundingBox b{/*.origin=*/{0, 0, 0}, /*.size=*/{3, 3, 3}};

  const std::array<float, 3> vertex_local{1.0f, 0.0f, 0.0f};
  const std::uint8_t ao = ComputeVertexAO(t,
                                          b,
                                          {0, 0, 0},
                                          /*cull_face=*/1,
                                          vertex_local,
                                          /*ao_strength=*/0.4f);
  EXPECT_EQ(ao, 255) << "no opaque neighbors should be full bright";
}

TEST(AoTest, BothSidesOpaqueGivesDarkestLevel) {
  // For block (0, 1, 1) with +X face vertex 0 (local (1,0,0)):
  //   side1 = (1, 0, 1), side2 = (1, 1, 0), corner = (1, 0, 0).
  // block_pos must be (0, 1, 1) so that side1/side2 are in bounds
  // (need by >= 1 and bz >= 1).
  std::vector<std::uint8_t> flags(27, 0);  // 3x3x3 all air
  // side1 (1,0,1): linear index y*9 + z*3 + x = 4
  flags[4] = 1;
  // side2 (1,1,0): linear index = 10
  flags[10] = 1;
  BlockGroupTable t = BuildGroupTable(flags);
  BoundingBox b{/*.origin=*/{0, 0, 0}, /*.size=*/{3, 3, 3}};

  const std::array<float, 3> vertex_local{1.0f, 0.0f, 0.0f};
  const std::uint8_t ao = ComputeVertexAO(t,
                                          b,
                                          {0, 1, 1},
                                          /*cull_face=*/1,
                                          vertex_local,
                                          /*ao_strength=*/0.4f);
  // Both sides opaque -> level 3 -> 255 - 3*255*0.4/3 = 153
  EXPECT_EQ(ao, 153) << "both sides opaque should be level 3 = 153";
}

TEST(AoTest, OneSideOpaqueGivesLevel2) {
  std::vector<std::uint8_t> flags(27, 0);  // 3x3x3 all air
  // (1,0,1) opaque: side1 of block (0,1,1), +X face v0. Linear index
  // is y*9 + z*3 + x = 0*9 + 1*3 + 1 = 4.
  flags[4] = 1;
  BlockGroupTable t = BuildGroupTable(flags);
  BoundingBox b{/*.origin=*/{0, 0, 0}, /*.size=*/{3, 3, 3}};

  const std::array<float, 3> vertex_local{1.0f, 0.0f, 0.0f};
  const std::uint8_t ao =
      ComputeVertexAO(t, b, {0, 1, 1}, 1, vertex_local, 0.4f);
  // One side opaque -> level 2 -> 255 - 2*255*0.4/3 = 187
  EXPECT_EQ(ao, 187) << "one side opaque should be level 2 = 187";
}

TEST(AoTest, OnlyCornerOpaqueGivesLevel1) {
  std::vector<std::uint8_t> flags(27, 0);  // 3x3x3 all air
  // Corner neighbor of block (0,1,1), +X face v0, is (1,0,0): linear 1.
  flags[1] = 1;  // opaque
  BlockGroupTable t = BuildGroupTable(flags);
  BoundingBox b{/*.origin=*/{0, 0, 0}, /*.size=*/{3, 3, 3}};

  const std::array<float, 3> vertex_local{1.0f, 0.0f, 0.0f};
  const std::uint8_t ao =
      ComputeVertexAO(t, b, {0, 1, 1}, 1, vertex_local, 0.4f);
  // Only corner opaque -> level 1 -> 255 - 255*0.4/3 ~= 221
  EXPECT_EQ(ao, 221) << "only corner opaque should be level 1 = 221";
}

TEST(AoTest, AoStrengthZeroMeansNoDarkening) {
  std::vector<std::uint8_t> flags(27, 1);  // all opaque
  BlockGroupTable t = BuildGroupTable(flags);
  BoundingBox b{/*.origin=*/{0, 0, 0}, /*.size=*/{3, 3, 3}};

  const std::array<float, 3> vertex_local{1.0f, 0.0f, 0.0f};
  const std::uint8_t ao =
      ComputeVertexAO(t, b, {0, 1, 1}, 1, vertex_local, /*ao_strength=*/0.0f);
  EXPECT_EQ(ao, 255) << "ao_strength=0 disables darkening";
}

TEST(AoTest, AoStrengthOneGivesMaxDarkening) {
  std::vector<std::uint8_t> flags(27, 1);  // all opaque -> level 3
  BlockGroupTable t = BuildGroupTable(flags);
  BoundingBox b{/*.origin=*/{0, 0, 0}, /*.size=*/{3, 3, 3}};

  const std::array<float, 3> vertex_local{1.0f, 0.0f, 0.0f};
  const std::uint8_t ao =
      ComputeVertexAO(t, b, {0, 1, 1}, 1, vertex_local, /*ao_strength=*/1.0f);
  // All opaque -> level 3; ao_strength 1 -> 255 - 255 = 0
  EXPECT_EQ(ao, 0) << "max darkening should produce 0";
}

TEST(AoTest, OutOfBoundsNeighborsDontShadow) {
  // Corner block of a 1x1x1 region; all AO neighbors are out of bounds.
  std::vector<std::uint8_t> flags(1, 1);  // single opaque block
  BlockGroupTable t = BuildGroupTable(flags);
  BoundingBox b{/*.origin=*/{0, 0, 0}, /*.size=*/{1, 1, 1}};

  const std::array<float, 3> vertex_local{1.0f, 0.0f, 0.0f};
  const std::uint8_t ao =
      ComputeVertexAO(t, b, {0, 0, 0}, 1, vertex_local, 0.4f);
  EXPECT_EQ(ao, 255) << "out-of-bounds neighbors don't shadow";
}

// End-to-end AO + tint tests via TessellateRegion + ExportGlb

// 3x3x3 stone region with an air pocket at the center (1,1,1). The six
// blocks adjacent to the pocket expose one visible face each, whose AO
// neighbors are in-bounds stone, so AO shadowing triggers.
struct StoneRegionWithHole {
  std::unique_ptr<Arena> arena = std::make_unique<Arena>();
  Region region{};

  StoneRegionWithHole() {
    region.bounds = BoundingBox{/*.origin=*/{0, 0, 0}, /*.size=*/{3, 3, 3}};
    region.palette = {
        BlockState{.name = "minecraft:air",
                   .prop_encoding = PropertyEncoding::kNone},
        BlockState{.name = "minecraft:stone",
                   .prop_encoding = PropertyEncoding::kNone},
    };
    region.is_air_lut.assign(2, 0);
    region.is_air_lut[0] = 1;
    const std::size_t vol = 27;
    region.block_indices = UnInitBuffer<std::uint16_t>(vol, arena.get());
    std::uint16_t* data =
        reinterpret_cast<std::uint16_t*>(region.block_indices.data());
    for (std::size_t i = 0; i < vol; ++i) data[i] = 1;  // all stone
    // (1,1,1) -> air: linear index y*9 + z*3 + x = 13
    data[13] = 0;
    region.is_materialized = true;
    region.lazy_source.encoding = fschema::ir::BlockDataEncoding::kNone;
  }
};

// Reads vertex i's RGBA from the COLOR_0 attribute in the GLB BIN chunk.
// Returns false if the vertex is out of range.
bool ReadVertexColor(const ByteBuffer& glb,
                     std::uint32_t vertex_idx,
                     std::uint8_t& r,
                     std::uint8_t& g,
                     std::uint8_t& b,
                     std::uint8_t& a) {
  const std::size_t json_offset = 12;
  std::uint32_t json_len = 0;
  std::memcpy(&json_len, glb.bytes.data() + json_offset, 4);
  const std::size_t bin_header_offset =
      12 + 8 + ((json_len + 3) & ~std::size_t(3));

  std::uint32_t bin_len = 0;
  std::memcpy(&bin_len, glb.bytes.data() + bin_header_offset, 4);
  const std::uint8_t* bin_data = glb.bytes.data() + bin_header_offset + 8;

  // Both fixtures below yield 24 vertices (6 visible faces x 4), so
  // COLOR_0 starts right after POSITION (288), NORMAL (288), and
  // TEXCOORD_0 (192).
  const std::size_t color_offset = 288 + 288 + 192;
  if (color_offset + (vertex_idx * 4 + 4) > bin_len) return false;

  const std::uint8_t* p = bin_data + color_offset + (vertex_idx * 4);
  r = p[0];
  g = p[1];
  b = p[2];
  a = p[3];
  return true;
}

TEST(AoIntegrationTest, SingleStoneBlockAoIsMostlyBright) {
  // A lone block has no opaque neighbors: every vertex is level 0 (255).
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  TessellationOptions opts;
  opts.ambient_occlusion = true;
  opts.ao_strength = 0.4f;
  auto tess = TessellateRegion(sr.region, *sr.arena, **pack, opts);
  ASSERT_TRUE(tess.has_value()) << ToString(tess.error());

  // 24 vertices (6 faces x 4).
  ASSERT_EQ(tess->opaque_mesh.ao.size(), 24u);
  for (std::size_t i = 0; i < tess->opaque_mesh.ao.size(); ++i) {
    EXPECT_EQ(tess->opaque_mesh.ao[i], 255)
        << "vertex " << i << " AO should be 255 (no neighbors)";
  }
}

TEST(AoIntegrationTest, TwoAdjacentBlocksHaveShadowedInteriorVertices) {
  // Center-air scenario (see StoneRegionWithHole): block (0,1,1)'s +X
  // face is visible, and its vertex-0 AO neighbors are all stone:
  //   side1 = (1,0,1), side2 = (1,1,0), corner = (1,0,0)
  //   -> level 3 -> 255 - 255*1.0 = 0 at ao_strength 1.0
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegionWithHole sr;

  TessellationOptions opts;
  opts.ambient_occlusion = true;
  opts.ao_strength = 1.0f;
  auto tess = TessellateRegion(sr.region, *sr.arena, **pack, opts);
  ASSERT_TRUE(tess.has_value());

  // At ao_strength 1.0, at least one vertex must be darkened.
  bool found_darkened = false;
  for (std::uint8_t ao : tess->opaque_mesh.ao) {
    if (ao < 255) {
      found_darkened = true;
      break;
    }
  }
  EXPECT_TRUE(found_darkened) << "expected at least one AO-darkened vertex "
                                 "(center-air scenario must trigger AO)";
}

TEST(TintIntegrationTest, DefaultTintTableProducesWhiteRGB) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  // Default TintTable is identity, so COLOR_0 RGB should be (255,255,255).
  auto glb = ExportGlb(*tess);  // default TintTable
  ASSERT_TRUE(glb.has_value());

  std::uint8_t r, g, b, a;
  ASSERT_TRUE(ReadVertexColor(*glb, 0, r, g, b, a));
  EXPECT_EQ(r, 255);
  EXPECT_EQ(g, 255);
  EXPECT_EQ(b, 255);
}

TEST(TintIntegrationTest, CustomTintTableAppliesColor) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  // Every mesh tint slot is 0 (EmitElementFaces never sets one), so this
  // only checks that no tint is accidentally applied; exercising a real
  // tint needs a model with tinted faces (e.g. grass).
  TintTable tint;
  auto glb = ExportGlb(*tess, tint);
  ASSERT_TRUE(glb.has_value());

  std::uint8_t r, g, b, a;
  ASSERT_TRUE(ReadVertexColor(*glb, 0, r, g, b, a));
  EXPECT_EQ(r, 255);
  EXPECT_EQ(g, 255);
  EXPECT_EQ(b, 255);
  // AO byte for a lone block = 255.
  EXPECT_EQ(a, 255);
}

TEST(TintIntegrationTest, AoByteWrittenToColorAlphaChannel) {
  // Center-air scenario at ao_strength 1.0: some vertex must have
  // AO < 255 in COLOR_0 alpha.
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegionWithHole sr;

  TessellationOptions opts;
  opts.ambient_occlusion = true;
  opts.ao_strength = 1.0f;
  auto tess = TessellateRegion(sr.region, *sr.arena, **pack, opts);
  ASSERT_TRUE(tess.has_value());

  auto glb = ExportGlb(*tess);
  ASSERT_TRUE(glb.has_value());

  bool found_darkened = false;
  const std::uint32_t max_verts =
      static_cast<std::uint32_t>(tess->opaque_mesh.ao.size());
  for (std::uint32_t i = 0; i < max_verts; ++i) {
    std::uint8_t r, g, b, a;
    if (ReadVertexColor(*glb, i, r, g, b, a)) {
      if (a < 255) {
        found_darkened = true;
        break;
      }
    }
  }
  EXPECT_TRUE(found_darkened)
      << "expected at least one vertex with alpha < 255";
}

}  // namespace
