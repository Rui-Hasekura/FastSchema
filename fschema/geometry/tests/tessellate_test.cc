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

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "fschema/base/error.h"
#include "fschema/geometry/resource_pack.h"
#include "fschema/geometry/tessellate.h"
#include "fschema/geometry/tests/geometry_test_util.h"
#include "fschema/geometry/types.h"

namespace {

using fschema::ToString;
using fschema::geometry::Mesh;
using fschema::geometry::ResourcePack;
using fschema::geometry::TessellateRegion;
using fschema::geometry::TessellationOptions;
using fschema::tests::ResourcePackFixture;
using fschema::tests::StoneRegion;

TEST(TessellateTest, SingleStoneBlockEmitsSixFaces) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(1, 1, 1);  // single stone block
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // 6 faces * 4 vertices = 24 vertices; 6 faces * 2 triangles = 12 triangles.
  EXPECT_EQ(r->opaque_mesh.VertexCount(), 24u);
  EXPECT_EQ(r->opaque_mesh.TriangleCount(), 12u);
  // No transparent geometry.
  EXPECT_EQ(r->transparent_mesh.VertexCount(), 0u);
  EXPECT_EQ(r->transparent_mesh.TriangleCount(), 0u);
  EXPECT_FALSE(r->HasTransparency());
}

TEST(TessellateTest, TwoAdjacentBlocksCullInteriorFaces) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(2, 1, 1);  // 2x1x1: blocks at (0,0,0) and (1,0,0)
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // Each block has 6 faces. The touching +X face of block (0,0,0) and
  // the -X face of block (1,0,0) are culled (both stone = opaque
  // neighbor). So 6 + 6 - 2 = 10 visible faces.
  EXPECT_EQ(r->opaque_mesh.VertexCount(), 10u * 4u);
  EXPECT_EQ(r->opaque_mesh.TriangleCount(), 10u * 2u);
}

TEST(TessellateTest, FullCubeEmitsExteriorOnly) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(2, 2, 2);  // 2x2x2 = 8 stone blocks
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // 8 blocks * 6 faces = 48 total faces.
  // Interior faces (shared between two stone blocks) are culled:
  //   - 4 internal +X/-X face pairs (between columns 0 and 1 in X)
  //     per Y-Z layer: 2x2 = 4 pairs = 8 faces culled
  //   - 4 internal +Y/-Y face pairs (between rows 0 and 1 in Y)
  //     per X-Z layer: 2x2 = 4 pairs = 8 faces culled
  //   - 4 internal +Z/-Z face pairs = 8 faces culled
  //   Total interior faces culled: 24.
  // Visible faces: 48 - 24 = 24 = 6 sides x 4 blocks per side.
  EXPECT_EQ(r->opaque_mesh.VertexCount(), 24u * 4u);
  EXPECT_EQ(r->opaque_mesh.TriangleCount(), 24u * 2u);
}

TEST(TessellateTest, EmptyRegionProducesNoGeometry) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(4, 4, 4);
  // Override: fill with air (palette slot 0).
  std::uint16_t* data =
      reinterpret_cast<std::uint16_t*>(sr.region.block_indices.data());
  const std::size_t vol = 4 * 4 * 4;
  for (std::size_t i = 0; i < vol; ++i) data[i] = 0;

  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  EXPECT_EQ(r->opaque_mesh.VertexCount(), 0u);
  EXPECT_EQ(r->opaque_mesh.TriangleCount(), 0u);
  EXPECT_EQ(r->transparent_mesh.VertexCount(), 0u);
  EXPECT_EQ(r->TotalVertices(), 0u);
}

TEST(TessellateTest, AtlasHasOneSlotForStone) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(1, 1, 1);
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // The stone texture was inserted once; atlas should have 1 slot.
  EXPECT_EQ(r->atlas.slots.size(), 1u);
  EXPECT_GT(r->atlas.width, 0u);
  EXPECT_GT(r->atlas.height, 0u);
}

TEST(TessellateTest, AtlasUVsAreIn01) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(1, 1, 1);
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value());

  for (std::size_t i = 0; i < r->opaque_mesh.uvs.size(); i += 2) {
    const float u = r->opaque_mesh.uvs[i];
    const float v = r->opaque_mesh.uvs[i + 1];
    EXPECT_GE(u, 0.0f);
    EXPECT_LE(u, 1.0f);
    EXPECT_GE(v, 0.0f);
    EXPECT_LE(v, 1.0f);
  }
}

// The per-direction uv assignment matches vanilla's model bakery
// (FaceBakery.defaultFaceUV) on every face: side faces stand the
// texture upright and unmirrored as seen from outside (image top row
// on the top edge, image left column on the outside view's left),
// up is a north-up map (u = +X, v = +Z) and down reads with south up
// (u = +X, v = -Z). Face uv (0, 0) samples the texture's top-left
// texel (atlas Finalize keeps PNG row order, emit is linear). Corner
// values per face in BuildCubeFace order.
TEST(TessellateTest, CubeFaceUvsMatchVanillaOrientation) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(1, 1, 1);
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->opaque_mesh.VertexCount(), 24u);
  ASSERT_EQ(r->atlas.slots.size(), 1u);  // cube_all: one stone texture

  const Mesh& m = r->opaque_mesh;
  const auto& slot = r->atlas.slots[0];
  constexpr float kEps = 1e-4f;
  int faces = 0;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    const float nx = m.normals[q * 12 + 0];
    const float ny = m.normals[q * 12 + 1];
    const float nz = m.normals[q * 12 + 2];
    float u[4], v[4];
    for (int k = 0; k < 4; ++k) {
      u[k] = (m.uvs[(q * 4 + k) * 2 + 0] - slot.u0) / (slot.u1 - slot.u0);
      v[k] = (m.uvs[(q * 4 + k) * 2 + 1] - slot.v0) / (slot.v1 - slot.v0);
    }
    const float (*want)[2] = nullptr;
    if (ny > 0.5f) {  // up: NW, NE, SE, SW
      static const float w[4][2] = {
          {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
      want = w;
    } else if (ny < -0.5f) {  // down: NW, SW, SE, NE
      static const float w[4][2] = {
          {0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
      want = w;
    } else if (nz < -0.5f) {  // north: bW, bE, tE, tW
      static const float w[4][2] = {
          {1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}};
      want = w;
    } else if (nz > 0.5f) {  // south: bW, tW, tE, bE
      static const float w[4][2] = {
          {0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
      want = w;
    } else if (nx > 0.5f) {  // east: bN, bS, tS, tN
      static const float w[4][2] = {
          {1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}};
      want = w;
    } else {  // west: bN, tN, tS, bS
      static const float w[4][2] = {
          {0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
      want = w;
    }
    ++faces;
    for (int k = 0; k < 4; ++k) {
      EXPECT_NEAR(u[k], want[k][0], kEps);
      EXPECT_NEAR(v[k], want[k][1], kEps);
    }
  }
  EXPECT_EQ(faces, 6);
}

// Merged quads tile the texture along the same per-direction model
// image axes as unmerged faces: a 2x1x1 stone row merges its north
// pair into one quad whose u spans 2..0 (the north basis mirrors u
// along -X) with the image upright (v = 1 along the bottom edge, 0
// along the top), and its up pair into u 0..2 / v 0..1. Before the
// uv-orientation fix the merged north face transposed the axes
// relative to the unmerged output.
TEST(TessellateTest, MergedQuadUvsFollowFaceOrientation) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(2, 1, 1);
  TessellationOptions opts;
  opts.merge_coplanar_faces = true;
  auto r = TessellateRegion(sr.region, *sr.arena, **pack, opts);
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->atlas.slots.size(), 1u);

  const Mesh& m = r->opaque_mesh;
  const auto& slot = r->atlas.slots[0];
  // Tile units (slot rect / repeats): merged quads address the slot
  // in tile units, so normalize by one texture tile, not by the whole
  // tiled slot rect.
  const float tile_u = (slot.u1 - slot.u0) / slot.repeats_x;
  const float tile_v = (slot.v1 - slot.v0) / slot.repeats_y;
  constexpr float kEps = 1e-4f;
  int norths = 0, ups = 0;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    const float ny = m.normals[q * 12 + 1];
    const float nz = m.normals[q * 12 + 2];
    float u[4], v[4];
    for (int k = 0; k < 4; ++k) {
      u[k] = (m.uvs[(q * 4 + k) * 2 + 0] - slot.u0) / tile_u;
      v[k] = (m.uvs[(q * 4 + k) * 2 + 1] - slot.v0) / tile_v;
    }
    if (nz < -0.5f) {
      // Merged north wall, merge corner order bW, bE, tE, tW:
      // u tiles 2,0,0,2 (the model north basis mirrors u along -X);
      // v = 1,1,0,0 (image upright).
      ++norths;
      const float wu[4] = {2.0f, 0.0f, 0.0f, 2.0f};
      const float wv[4] = {1.0f, 1.0f, 0.0f, 0.0f};
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], wu[k], kEps);
        EXPECT_NEAR(v[k], wv[k], kEps);
      }
    } else if (ny > 0.5f) {
      // Merged up floor, merge corner order NW, NE, SE, SW:
      // u tiles 0,2,2,0 along +X; v = 0,0,1,1 along +Z.
      ++ups;
      const float wu[4] = {0.0f, 2.0f, 2.0f, 0.0f};
      const float wv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], wu[k], kEps);
        EXPECT_NEAR(v[k], wv[k], kEps);
      }
    }
  }
  EXPECT_EQ(norths, 1);
  EXPECT_EQ(ups, 1);
}

TEST(TessellateTest, VertexPositionsSpanBlockVolume) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(2, 2, 2);
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value());

  // All vertices should be inside [0, 2]^3 (the 2x2x2 block volume).
  // ModelFace corners are in [0, 1] block-local; world position = block
  // integer coords + corner offset, so range is [0, 2].
  for (std::size_t i = 0; i < r->opaque_mesh.positions.size(); i += 3) {
    const float x = r->opaque_mesh.positions[i + 0];
    const float y = r->opaque_mesh.positions[i + 1];
    const float z = r->opaque_mesh.positions[i + 2];
    EXPECT_GE(x, 0.0f);
    EXPECT_LE(x, 2.0f);
    EXPECT_GE(y, 0.0f);
    EXPECT_LE(y, 2.0f);
    EXPECT_GE(z, 0.0f);
    EXPECT_LE(z, 2.0f);
  }
}

TEST(TessellateTest, NormalsAreUnitAxisAligned) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(1, 1, 1);
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value());

  // Each cube face's normal is one of +/-X, +/-Y, +/-Z (unit axis).
  for (std::size_t i = 0; i < r->opaque_mesh.normals.size(); i += 3) {
    const float nx = r->opaque_mesh.normals[i + 0];
    const float ny = r->opaque_mesh.normals[i + 1];
    const float nz = r->opaque_mesh.normals[i + 2];
    const bool is_axis_aligned =
        (std::abs(nx) == 1.0f && ny == 0.0f && nz == 0.0f) ||
        (nx == 0.0f && std::abs(ny) == 1.0f && nz == 0.0f) ||
        (nx == 0.0f && ny == 0.0f && std::abs(nz) == 1.0f);
    EXPECT_TRUE(is_axis_aligned)
        << "normal (" << nx << "," << ny << "," << nz << ") not unit-axis";
  }
}

TEST(TessellateTest, IndicesFormTwoTrianglesPerFace) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(1, 1, 1);
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value());

  // 6 faces * 2 triangles * 3 indices = 36 indices.
  ASSERT_EQ(r->opaque_mesh.indices.size(), 36u);

  // Each triangle's 3 indices must be in [0, VertexCount).
  for (std::size_t i = 0; i < r->opaque_mesh.indices.size(); i += 3) {
    const auto a = r->opaque_mesh.indices[i + 0];
    const auto b = r->opaque_mesh.indices[i + 1];
    const auto c = r->opaque_mesh.indices[i + 2];
    EXPECT_LT(a, r->opaque_mesh.VertexCount());
    EXPECT_LT(b, r->opaque_mesh.VertexCount());
    EXPECT_LT(c, r->opaque_mesh.VertexCount());
  }
}

// glTF front-face convention: every emitted triangle must wind
// counter-clockwise when viewed from the direction its exported
// per-vertex normal points, i.e. cross(v1-v0, v2-v0) . normal > 0.
// Backface-culling consumers (glTF viewers, RaXhine's CCW/Back
// pipeline) render the mesh inside-out otherwise. Covers both the
// direct and the greedy-merge emission paths.
TEST(TessellateTest, TriangleWindingMatchesVertexNormals) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  for (bool merge : {false, true}) {
    StoneRegion sr(3, 2, 3);
    TessellationOptions opts;
    opts.merge_coplanar_faces = merge;
    auto r = TessellateRegion(sr.region, *sr.arena, **pack, opts);
    ASSERT_TRUE(r.has_value()) << ToString(r.error());

    for (const Mesh* mesh : {&r->opaque_mesh, &r->transparent_mesh}) {
      ASSERT_EQ(mesh->indices.size() % 3, 0u);
      for (std::size_t t = 0; t < mesh->indices.size() / 3; ++t) {
        const std::uint32_t i0 = mesh->indices[t * 3 + 0];
        const std::uint32_t i1 = mesh->indices[t * 3 + 1];
        const std::uint32_t i2 = mesh->indices[t * 3 + 2];
        const float ax = mesh->positions[i1 * 3 + 0] -
                         mesh->positions[i0 * 3 + 0];
        const float ay = mesh->positions[i1 * 3 + 1] -
                         mesh->positions[i0 * 3 + 1];
        const float az = mesh->positions[i1 * 3 + 2] -
                         mesh->positions[i0 * 3 + 2];
        const float bx = mesh->positions[i2 * 3 + 0] -
                         mesh->positions[i0 * 3 + 0];
        const float by = mesh->positions[i2 * 3 + 1] -
                         mesh->positions[i0 * 3 + 1];
        const float bz = mesh->positions[i2 * 3 + 2] -
                         mesh->positions[i0 * 3 + 2];
        const float gx = ay * bz - az * by;
        const float gy = az * bx - ax * bz;
        const float gz = ax * by - ay * bx;
        const float dot = gx * mesh->normals[i0 * 3 + 0] +
                          gy * mesh->normals[i0 * 3 + 1] +
                          gz * mesh->normals[i0 * 3 + 2];
        EXPECT_GT(dot, 0.0f)
            << (merge ? "[merge] " : "[direct] ") << "triangle " << t
            << " winds against its normal (dot=" << dot << ")";
      }
    }
  }
}

TEST(TessellateTest, SameRegionSamePackProducesIdenticalResult) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(2, 2, 2);
  auto r1 =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  auto r2 =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());

  // Vertex count and triangle count must match.
  EXPECT_EQ(r1->TotalVertices(), r2->TotalVertices());
  EXPECT_EQ(r1->TotalTriangles(), r2->TotalTriangles());
  // Atlas slot count must match.
  EXPECT_EQ(r1->atlas.slots.size(), r2->atlas.slots.size());
}

}  // namespace
