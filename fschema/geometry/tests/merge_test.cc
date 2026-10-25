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
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/geometry/resource_pack.h"
#include "fschema/geometry/tessellate.h"
#include "fschema/geometry/tests/geometry_test_util.h"
#include "fschema/geometry/types.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"

namespace {

using fschema::ToString;
using fschema::geometry::ResourcePack;
using fschema::geometry::TessellateRegion;
using fschema::geometry::TessellationOptions;
using fschema::geometry::TessellationResult;
using fschema::tests::ResourcePackFixture;
using fschema::tests::StoneRegion;

TessellationOptions DirectOpts() {
  TessellationOptions o;
  o.merge_coplanar_faces = false;
  o.chunk_edge = 0;
  o.ambient_occlusion = false;
  return o;
}

TessellationOptions MergeOpts() {
  TessellationOptions o;
  o.merge_coplanar_faces = true;
  o.chunk_edge = 0;
  o.ambient_occlusion = false;
  return o;
}

TEST(MergeTest, MergeReducesTriangleCountFor4x4x1Wall) {
  // 4x4x1 wall of stone. Direct path: each of the 6 visible sides
  // has 4 faces (one per block). Merge path: each side merges to
  // 1 face. So:
  //   Direct: 6 sides * 4 faces * 2 tris = 48 triangles
  //   Merge: 6 sides * 1 face * 2 tris = 12 triangles
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(4, 4, 1);

  auto direct = TessellateRegion(sr.region, *sr.arena, **pack, DirectOpts());
  ASSERT_TRUE(direct.has_value()) << ToString(direct.error());

  auto merged = TessellateRegion(sr.region, *sr.arena, **pack, MergeOpts());
  ASSERT_TRUE(merged.has_value()) << ToString(merged.error());

  EXPECT_GT(direct->opaque_mesh.TriangleCount(), 0u);
  EXPECT_GT(merged->opaque_mesh.TriangleCount(), 0u);
  EXPECT_LT(merged->opaque_mesh.TriangleCount(),
            direct->opaque_mesh.TriangleCount())
      << "merge should reduce triangle count";
}

TEST(MergeTest, MergeProducesSameVertexBounds) {
  // Positions should span the same [0, 4] x [0, 4] x [0, 1] volume
  // regardless of merge.
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(4, 4, 1);

  auto merged = TessellateRegion(sr.region, *sr.arena, **pack, MergeOpts());
  ASSERT_TRUE(merged.has_value());

  float min_x = 1e30f, max_x = -1e30f;
  float min_y = 1e30f, max_y = -1e30f;
  float min_z = 1e30f, max_z = -1e30f;
  for (std::size_t i = 0; i < merged->opaque_mesh.positions.size(); i += 3) {
    const float x = merged->opaque_mesh.positions[i + 0];
    const float y = merged->opaque_mesh.positions[i + 1];
    const float z = merged->opaque_mesh.positions[i + 2];
    min_x = std::min(min_x, x);
    max_x = std::max(max_x, x);
    min_y = std::min(min_y, y);
    max_y = std::max(max_y, y);
    min_z = std::min(min_z, z);
    max_z = std::max(max_z, z);
  }
  EXPECT_NEAR(min_x, 0.0f, 0.01f);
  EXPECT_NEAR(max_x, 4.0f, 0.01f);
  EXPECT_NEAR(min_y, 0.0f, 0.01f);
  EXPECT_NEAR(max_y, 4.0f, 0.01f);
  EXPECT_NEAR(min_z, 0.0f, 0.01f);
  EXPECT_NEAR(max_z, 1.0f, 0.01f);
}

TEST(MergeTest, MergeUVsRepeatPerBlock) {
  // With GL_REPEAT wrap mode, merged UVs can be > 1 (texture repeats
  // per block). Verify UVs are non-negative and some are > 1.
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(4, 4, 1);

  auto merged = TessellateRegion(sr.region, *sr.arena, **pack, MergeOpts());
  ASSERT_TRUE(merged.has_value());

  bool found_gt_1 = false;
  for (std::size_t i = 0; i < merged->opaque_mesh.uvs.size(); i += 2) {
    EXPECT_GE(merged->opaque_mesh.uvs[i], 0.0f);
    EXPECT_GE(merged->opaque_mesh.uvs[i + 1], 0.0f);
    if (merged->opaque_mesh.uvs[i] > 1.0f ||
        merged->opaque_mesh.uvs[i + 1] > 1.0f) {
      found_gt_1 = true;
    }
  }
  EXPECT_TRUE(found_gt_1) << "merged UVs should repeat (some UV > 1)";
}

TEST(MergeTest, MergeIndicesAreValid) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(4, 4, 1);

  auto merged = TessellateRegion(sr.region, *sr.arena, **pack, MergeOpts());
  ASSERT_TRUE(merged.has_value());

  const auto vc = merged->opaque_mesh.VertexCount();
  for (std::uint32_t idx : merged->opaque_mesh.indices) {
    ASSERT_LT(idx, vc) << "index " << idx << " >= vertex count " << vc;
  }
}

TEST(MergeTest, MergeForSingleBlockSameAsDirect) {
  // 1x1x1: no merge possible (only 1 block per face). Triangle count
  // should be identical.
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto direct = TessellateRegion(sr.region, *sr.arena, **pack, DirectOpts());
  auto merged = TessellateRegion(sr.region, *sr.arena, **pack, MergeOpts());
  ASSERT_TRUE(direct.has_value());
  ASSERT_TRUE(merged.has_value());

  EXPECT_EQ(direct->opaque_mesh.TriangleCount(),
            merged->opaque_mesh.TriangleCount());
  EXPECT_EQ(direct->opaque_mesh.VertexCount(),
            merged->opaque_mesh.VertexCount());
}

TEST(MergeTest, MergeFor2x2x2CubeReducesTriangles) {
  // 2x2x2 cube: 24 exterior faces direct -> merged into 6 (one per side).
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(2, 2, 2);

  auto direct = TessellateRegion(sr.region, *sr.arena, **pack, DirectOpts());
  auto merged = TessellateRegion(sr.region, *sr.arena, **pack, MergeOpts());
  ASSERT_TRUE(direct.has_value());
  ASSERT_TRUE(merged.has_value());

  // Direct: 24 faces * 2 tris = 48 triangles
  EXPECT_EQ(direct->opaque_mesh.TriangleCount(), 48u);
  // Merged: 6 faces * 2 tris = 12 triangles
  EXPECT_EQ(merged->opaque_mesh.TriangleCount(), 12u);
}

TEST(MergeTest, MergeQuadCornersFormRectangle) {
  // For a 2x2x1 wall, merge emits 6 quads (one per face). Each quad must
  // be an axis-aligned rectangle: constant on the face normal axis, and
  // 2 distinct values on each in-plane axis (each appearing twice).
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(2, 2, 1);

  auto merged = TessellateRegion(sr.region, *sr.arena, **pack, MergeOpts());
  ASSERT_TRUE(merged.has_value());

  ASSERT_EQ(merged->opaque_mesh.VertexCount(), 24u);
  ASSERT_EQ(merged->opaque_mesh.TriangleCount(), 12u);

  for (std::size_t q = 0; q < 6; ++q) {
    std::array<std::array<float, 3>, 4> quad{};
    for (int i = 0; i < 4; ++i) {
      for (int a = 0; a < 3; ++a) {
        quad[i][a] = merged->opaque_mesh.positions[(q * 4 + i) * 3 + a];
      }
    }
    int na = -1;
    for (int a = 0; a < 3; ++a) {
      bool all_same = true;
      for (int i = 1; i < 4; ++i) {
        if (std::abs(quad[i][a] - quad[0][a]) > 0.001f) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        na = a;
        break;
      }
    }
    ASSERT_NE(na, -1) << "quad " << q << " has no constant axis";
    for (int a = 0; a < 3; ++a) {
      if (a == na) continue;
      std::array<float, 4> vals{};
      for (int i = 0; i < 4; ++i) vals[i] = quad[i][a];
      std::sort(vals.begin(), vals.end());
      EXPECT_NEAR(vals[0], vals[1], 0.001f)
          << "quad " << q << " axis " << a << " not 2x2 grid";
      EXPECT_NEAR(vals[2], vals[3], 0.001f)
          << "quad " << q << " axis " << a << " not 2x2 grid";
      EXPECT_GT(vals[3] - vals[0], 0.5f)
          << "quad " << q << " axis " << a << " degenerate";
    }
  }
}

}  // namespace
