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
#include "fschema/geometry/tests/resolve_test_util.h"
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

// ---- carried uv layout under blockstate rotation + atlas tiling ----

// Affine uv map of one quad, in mesh (atlas) uv space, plus its world
// rect: uv = A * (in-plane world) + b. Used to sample the uv at probe
// points inside the quad, for both the direct and the merged mesh.
struct QuadFit {
  float au = 0, bu = 0, cu = 0;
  float av = 0, bv = 0, cv = 0;
  int a1 = 0, a2 = 1;
  float lo1 = 0, hi1 = 0, lo2 = 0, hi2 = 0;
  int nax = -1;
  float plane = 0;
  bool ok = false;
};

QuadFit FitQuad(const fschema::geometry::Mesh& m, std::size_t q) {
  QuadFit r;
  const std::size_t b = q * 4;
  float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
  for (std::size_t i = 0; i < 4; ++i) {
    for (int k = 0; k < 3; ++k) {
      const float v = m.positions[(b + i) * 3 + k];
      mn[k] = std::min(mn[k], v);
      mx[k] = std::max(mx[k], v);
    }
  }
  int nax = 0;
  for (int k = 1; k < 3; ++k) {
    if (mx[k] - mn[k] < mx[nax] - mn[nax]) nax = k;
  }
  r.nax = nax;
  r.plane = m.positions[b * 3 + nax];
  r.a1 = (nax == 0) ? 1 : 0;
  r.a2 = (nax == 2) ? 1 : 2;
  r.lo1 = mn[r.a1];
  r.hi1 = mx[r.a1];
  r.lo2 = mn[r.a2];
  r.hi2 = mx[r.a2];
  const float d1 = r.hi1 - r.lo1, d2 = r.hi2 - r.lo2;
  if (d1 < 1e-4f || d2 < 1e-4f) return r;
  // Match the four bbox corners to their uvs (direct quads and merged
  // quads alike have their corners on the rect).
  int c00 = -1, c01 = -1, c10 = -1, c11 = -1;
  for (std::size_t i = 0; i < 4; ++i) {
    const float p1 = m.positions[(b + i) * 3 + r.a1];
    const float p2 = m.positions[(b + i) * 3 + r.a2];
    const bool hi1 = p1 > r.lo1 + d1 * 0.5f;
    const bool hi2 = p2 > r.lo2 + d2 * 0.5f;
    int& c = !hi1 && !hi2 ? c00 : !hi1 && hi2 ? c01 : hi1 && !hi2 ? c10 : c11;
    if (c != -1) return r;  // ambiguous
    c = static_cast<int>(i);
  }
  if (c00 < 0 || c01 < 0 || c10 < 0 || c11 < 0) return r;
  const auto U = [&](int i) { return m.uvs[(b + i) * 2 + 0]; };
  const auto V = [&](int i) { return m.uvs[(b + i) * 2 + 1]; };
  r.au = (U(c10) - U(c00)) / d1;
  r.bu = (U(c01) - U(c00)) / d2;
  r.cu = U(c00) - r.au * r.lo1 - r.bu * r.lo2;
  r.av = (V(c10) - V(c00)) / d1;
  r.bv = (V(c01) - V(c00)) / d2;
  r.cv = V(c00) - r.av * r.lo1 - r.bv * r.lo2;
  r.ok = true;
  return r;
}

// Slot lookup for a quad (its min-uv corner sits at its slot's u0/v0)
// plus the tile size of that slot, in atlas uv units.
struct SlotRef {
  std::size_t index = 0;
  float tile_u = 0, tile_v = 0;
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
  std::uint32_t repeats_x = 1, repeats_y = 1;
  // max(repeats_x, repeats_y): the slot's tile capacity on its wider
  // axis, for inside-the-slot bounds checks.
  [[nodiscard]] std::uint32_t repeats() const {
    return std::max(repeats_x, repeats_y);
  }
};

SlotRef SlotOfQuad(const fschema::geometry::TessellationResult& res,
                   std::size_t q) {
  const std::size_t b = q * 4;
  float umin = res.opaque_mesh.uvs[b * 2 + 0];
  float vmin = res.opaque_mesh.uvs[b * 2 + 1];
  for (std::size_t i = 1; i < 4; ++i) {
    umin = std::min(umin, res.opaque_mesh.uvs[(b + i) * 2 + 0]);
    vmin = std::min(vmin, res.opaque_mesh.uvs[(b + i) * 2 + 1]);
  }
  for (std::size_t s = 0; s < res.atlas.slots.size(); ++s) {
    const auto& slot = res.atlas.slots[s];
    if (std::abs(umin - slot.u0) < 1e-6f && std::abs(vmin - slot.v0) < 1e-6f) {
      SlotRef r;
      r.index = s;
      r.u0 = slot.u0;
      r.v0 = slot.v0;
      r.u1 = slot.u1;
      r.v1 = slot.v1;
      r.tile_u = (slot.u1 - slot.u0) / slot.repeats_x;
      r.tile_v = (slot.v1 - slot.v0) / slot.repeats_y;
      r.repeats_x = slot.repeats_x;
      r.repeats_y = slot.repeats_y;
      return r;
    }
  }
  return {};  // not found: leaves index 0, tile 0 -> caller asserts
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
  // Merged UVs address the slot in tile units (texture repeats once
  // per block), so in tile units some UV > 1 on multi-block rects,
  // while every UV stays inside the slot rect (>= 0, <= repeats).
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(4, 4, 1);

  auto merged = TessellateRegion(sr.region, *sr.arena, **pack, MergeOpts());
  ASSERT_TRUE(merged.has_value());

  bool found_gt_1 = false;
  for (std::size_t q = 0; q < merged->opaque_mesh.VertexCount() / 4; ++q) {
    const SlotRef sr2 = SlotOfQuad(*merged, q);
    ASSERT_GT(sr2.tile_u, 0.0f) << "quad " << q << ": slot not found";
    for (std::size_t i = 0; i < 4; ++i) {
      const std::size_t vi = q * 4 + i;
      const float u =
          (merged->opaque_mesh.uvs[vi * 2 + 0] - sr2.u0) / sr2.tile_u;
      const float v =
          (merged->opaque_mesh.uvs[vi * 2 + 1] - sr2.v0) / sr2.tile_v;
      EXPECT_GE(u, 0.0f) << "quad " << q << " vertex " << i << " u < 0";
      EXPECT_GE(v, 0.0f) << "quad " << q << " vertex " << i << " v < 0";
      EXPECT_LE(u, static_cast<float>(sr2.repeats()))
          << "quad " << q << " vertex " << i << " u past the slot";
      EXPECT_LE(v, static_cast<float>(sr2.repeats()))
          << "quad " << q << " vertex " << i << " v past the slot";
      if (u > 1.0f || v > 1.0f) found_gt_1 = true;
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


// Regression (barrel texture orientation): a blockstate rotation
// re-orients the texture with the geometry and the uv layout travels
// per corner. The greedy merge used to re-lay merged quads by the
// FINAL direction's native image axes, silently rotating the texture
// of rotated models (a lying barrel's side read upright instead of
// along the barrel axis). The merged quad must instead keep the
// source faces' carried layout: for every visible face, the merged
// mesh's uv at five probe points must equal the direct mesh's uv for
// the same face, modulo whole texture tiles.
TEST(MergeTest, MergeKeepsRotatedModelCarriedUvLayout) {
  // A dedicated pack with REAL decodable PNGs (ResolveFixture ships
  // placeholder bytes): rot_uvlock(facing=north) = cube_all under
  // x=90, no uvlock -- the lying-barrel family.
  fschema::tests::TempDir root;
  const auto base = root.Get();
  fschema::tests::WriteFile(
      base / "assets" / "minecraft" / "blockstates" / "rot_uvlock.json",
      R"({"variants": {"facing=north": {"model": "block/cube_all",)"
      R"("x": 90}}})");
  fschema::tests::WriteFile(
      base / "assets" / "minecraft" / "models" / "block" / "cube_all.json",
      R"({"parent": "block/cube", "textures": {"all": "block/stone"}})");
  fschema::tests::WriteFile(
      base / "assets" / "minecraft" / "models" / "block" / "cube.json",
      R"({"textures": {"particle": "block/stone"},)"
      R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
      R"( "faces": {)"
      R"(   "north": {"texture": "#all", "uv": [0,0,16,16]},)"
      R"(   "south": {"texture": "#all", "uv": [0,0,16,16]},)"
      R"(   "east":  {"texture": "#all", "uv": [0,0,16,16]},)"
      R"(   "west":  {"texture": "#all", "uv": [0,0,16,16]},)"
      R"(   "up":    {"texture": "#all", "uv": [0,0,16,16]},)"
      R"(   "down":  {"texture": "#all", "uv": [0,0,16,16]}}}]})");
  {
    std::vector<std::uint8_t> rgba(16 * 16 * 4, 0x80);
    fschema::tests::WritePngFile(
        base / "assets" / "minecraft" / "textures" / "block" / "stone.png",
        16, 16, rgba.data());
  }
  auto pack = ResourcePack::Load(base.generic_string());
  ASSERT_TRUE(pack.has_value());

  // Region 2x1x1: two adjacent rot_uvlock(facing=north) blocks
  // (x=90, no uvlock -- the lying-barrel family).
  static constexpr std::string_view kv = "facing=north";
  std::vector<fschema::ir::BlockState> palette = {
      fschema::ir::BlockState{.name = "minecraft:air",
                              .prop_encoding = fschema::ir::PropertyEncoding::kNone},
      fschema::ir::BlockState{
          .name = "minecraft:rot_uvlock",
          .raw_properties = std::span<const std::byte>(
              reinterpret_cast<const std::byte*>(kv.data()), kv.size()),
          .prop_encoding = fschema::ir::PropertyEncoding::kString},
  };
  auto arena = std::make_unique<fschema::memory::Arena>();
  fschema::ir::Region region{};
  region.bounds = fschema::ir::BoundingBox{{0, 0, 0}, {2, 1, 1}};
  region.palette = palette;
  region.is_air_lut = {1, 0};
  region.block_indices = fschema::memory::UnInitBuffer<std::uint16_t>(
      2, arena.get());
  {
    std::uint16_t* data =
        reinterpret_cast<std::uint16_t*>(region.block_indices.data());
    data[0] = 1;
    data[1] = 1;
  }
  region.is_materialized = true;
  region.lazy_source.encoding = fschema::ir::BlockDataEncoding::kNone;

  auto direct = TessellateRegion(region, *arena, **pack, DirectOpts());
  ASSERT_TRUE(direct.has_value()) << ToString(direct.error());
  auto merged = TessellateRegion(region, *arena, **pack, MergeOpts());
  ASSERT_TRUE(merged.has_value()) << ToString(merged.error());

  // 10 visible faces (the shared x=1 pair culls); the merge collapses
  // the four paired planes to one quad each -> 6 quads.
  ASSERT_EQ(direct->opaque_mesh.VertexCount(), 10u * 4u);
  ASSERT_EQ(merged->opaque_mesh.VertexCount(), 6u * 4u);

  // Probe each face at its center and 0.3/0.7 along both in-plane
  // axes; the uv (converted to tile units of the quad's slot) must
  // agree between the two meshes modulo 1.
  for (int bx = 0; bx < 2; ++bx) {
    for (int d = 0; d < 6; ++d) {
      // face rect: x [bx, bx+1], y [0,1], z [0,1]; drop the shared
      // plane at x=1 (culled, absent from both meshes).
      const float x0 = static_cast<float>(bx), x1 = x0 + 1.0f;
      struct {
        int nax;
        float plane;
      } face[6] = {{0, 0.0f}, {0, 2.0f}, {1, 1.0f},
                   {1, 0.0f}, {2, 1.0f}, {2, 0.0f}};
      (void)x0;
      (void)x1;
      // Skip the culled pair: block0 east (x=1) and block1 west (x=1).
      if (d == 0 && bx == 0) continue;  // east of block0 is interior
      if (d == 1 && bx == 1) continue;  // west of block1 is interior
      // d: 0=east(x=2) 1=west(x=0) 2=up(y=1) 3=down(y=0) 4=south(z=1)
      //    5=north(z=0) -- using the rect of THIS block.
      const float fx0 = x0, fx1 = x1;
      float p1lo = 0, p1hi = 1, p2lo = 0, p2hi = 1;
      int nax;
      float plane;
      if (d == 0) { nax = 0; plane = 2.0f; }
      else if (d == 1) { nax = 0; plane = 0.0f; }
      else if (d == 2) { nax = 1; plane = 1.0f; }
      else if (d == 3) { nax = 1; plane = 0.0f; }
      else if (d == 4) { nax = 2; plane = 1.0f; }
      else { nax = 2; plane = 0.0f; }
      if (nax == 0) { p1lo = 0; p1hi = 1; p2lo = 0; p2hi = 1; }
      if (nax == 1) { p1lo = fx0; p1hi = fx1; p2lo = 0; p2hi = 1; }
      if (nax == 2) { p1lo = fx0; p1hi = fx1; p2lo = 0; p2hi = 1; }
      const float probes[5][2] = {{0.5f, 0.5f}, {0.3f, 0.5f}, {0.7f, 0.5f},
                                  {0.5f, 0.3f}, {0.5f, 0.7f}};
      for (const auto& pr : probes) {
        float P[3];
        P[nax] = plane;
        int ax1 = (nax == 0) ? 1 : 0;
        int ax2 = (nax == 2) ? 1 : 2;
        P[ax1] = p1lo + pr[0] * (p1hi - p1lo);
        P[ax2] = p2lo + pr[1] * (p2hi - p2lo);
        // Find the containing quad in each mesh.
        auto sample = [&](const fschema::geometry::TessellationResult& res,
                           float* tu, float* tv) -> bool {
          const auto& m = res.opaque_mesh;
          for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
            const QuadFit f = FitQuad(m, q);
            if (!f.ok) continue;
            if (f.nax != nax || std::abs(f.plane - plane) > 1e-4f) continue;
            const float q1 = P[f.a1], q2 = P[f.a2];
            if (q1 < f.lo1 - 1e-4f || q1 > f.hi1 + 1e-4f) continue;
            if (q2 < f.lo2 - 1e-4f || q2 > f.hi2 + 1e-4f) continue;
            const SlotRef sr = SlotOfQuad(res, q);
            const float u = f.au * q1 + f.bu * q2 + f.cu;
            const float v = f.av * q1 + f.bv * q2 + f.cv;
            *tu = (u - sr.u0) / sr.tile_u;
            *tv = (v - sr.v0) / sr.tile_v;
            return true;
          }
          return false;
        };
        float ud = 0, vd = 0, um = 0, vm = 0;
        ASSERT_TRUE(sample(*direct, &ud, &vd))
            << "no direct quad contains probe on face d" << d << " bx" << bx;
        ASSERT_TRUE(sample(*merged, &um, &vm))
            << "no merged quad contains probe on face d" << d << " bx" << bx;
        for (float dlt : {um - ud, vm - vd}) {
          EXPECT_LT(std::abs(dlt - std::round(dlt)), 1e-4f)
              << "uv disagrees mod tile: face d" << d << " bx" << bx
              << " probe (" << pr[0] << "," << pr[1] << ")"
              << " direct=(" << ud << "," << vd << ")"
              << " merged=(" << um << "," << vm << ")";
        }
      }
    }
  }
}

// Regression (merged quads sampling outside their atlas slot): a
// merged wall quad addresses its slot in tile units, so the atlas
// packs tiled texture copies and the merge caps its rectangles at the
// slot's tile capacity. Every emitted quad must keep all four uvs
// inside its slot's rect; the old linear-into-slot mapping ran merged
// wall uvs past the slot into neighbouring atlas regions (rendered
// garbage on large glass walls).
TEST(MergeTest, MergedQuadUvsStayInsideAtlasSlots) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(4, 4, 1);

  auto merged = TessellateRegion(sr.region, *sr.arena, **pack, MergeOpts());
  ASSERT_TRUE(merged.has_value()) << ToString(merged.error());

  ASSERT_GE(merged->atlas.slots.size(), 1u);
  EXPECT_GE(merged->atlas.slots[0].repeats_x, 4u);
  EXPECT_GE(merged->atlas.slots[0].repeats_y, 4u);

  const auto& m = merged->opaque_mesh;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    const SlotRef sr2 = SlotOfQuad(*merged, q);
    ASSERT_GT(sr2.tile_u, 0.0f) << "quad " << q << ": slot not found";
    for (std::size_t i = 0; i < 4; ++i) {
      const std::size_t vi = q * 4 + i;
      const float u = m.uvs[vi * 2 + 0];
      const float v = m.uvs[vi * 2 + 1];
      EXPECT_GE(u, sr2.u0 - 1e-6f) << "quad " << q << " u below slot";
      EXPECT_LE(u, sr2.u1 + 1e-6f) << "quad " << q << " u past slot";
      EXPECT_GE(v, sr2.v0 - 1e-6f) << "quad " << q << " v below slot";
      EXPECT_LE(v, sr2.v1 + 1e-6f) << "quad " << q << " v past slot";
    }
  }
}
}  // namespace
