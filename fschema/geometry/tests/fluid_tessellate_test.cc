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
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
using fschema::geometry::Mesh;
using fschema::geometry::ResourcePack;
using fschema::geometry::TessellateRegion;
using fschema::geometry::TessellateSchematic;
using fschema::geometry::TessellationOptions;
using fschema::geometry::TextureAtlas;
using fschema::tests::TempDir;
using fschema::tests::WriteFile;
using fschema::tests::WritePngFile;

// Pack with stone (from ResourcePackFixture's shapes) plus glass and
// water for transparency coverage.
struct GlassWaterPackFixture {
  TempDir root;

  // Vanilla packs ship water_flow/lava_flow; minimal test packs may
  // not. Flow-textured fluid rendering is exercised only when they
  // exist, otherwise sides fall back to the still texture.
  explicit GlassWaterPackFixture(bool with_flow_textures = false,
                                 bool with_overlay_texture = false) {
    const auto base = root.Get();
    for (auto [block, tex] : {std::pair{"stone", "stone"},
                              {"glass", "glass"},
                              {"glass_pane", "glass"}}) {
      const std::string b(block);
      WriteFile(base / "assets" / "minecraft" / "blockstates" / (b + ".json"),
                R"({"variants": {"": {"model": "block/)" + b + R"(_cube"}}})");
      WriteFile(base / "assets" / "minecraft" / "models" / "block" /
                    (b + "_cube.json"),
                R"({"parent": "block/cube", "textures": {"all": "block/)" +
                    std::string(tex) + R"("}})");
    }
    // Leaves: own element models WITH tintindex on every face (vanilla
    // leaf models tint all faces), so tint slots flow into the mesh.
    // Texture sizes 10/14 are distinct from every other slot (16
    // stone/glass/still, 12 overlay, 8 flow) so SlotSizePx identifies
    // them without ambiguity.
    for (auto [block, tex] : {std::pair{"oak_leaves", "leaves_oak"},
                              {"birch_leaves", "leaves_birch"}}) {
      const std::string b(block);
      WriteFile(base / "assets" / "minecraft" / "blockstates" / (b + ".json"),
                R"({"variants": {"": {"model": "block/)" + b + R"(_cube"}}})");
      WriteFile(base / "assets" / "minecraft" / "models" / "block" /
                    (b + "_cube.json"),
                R"({"textures": {"all": "block/)" + std::string(tex) +
                    R"("}, "elements": [{"from": [0,0,0], "to": [16,16,16],)"
                    R"( "faces": {)"
                    R"(   "north": {"texture": "#all", "tintindex": 0},)"
                    R"(   "south": {"texture": "#all", "tintindex": 0},)"
                    R"(   "east":  {"texture": "#all", "tintindex": 0},)"
                    R"(   "west":  {"texture": "#all", "tintindex": 0},)"
                    R"(   "up":    {"texture": "#all", "tintindex": 0},)"
                    R"(   "down":  {"texture": "#all", "tintindex": 0}}}]})");
    }
    WriteFile(base / "assets" / "minecraft" / "blockstates" / "water.json",
              R"({"variants": {"": {"model": "block/water"}}})");
    WriteFile(base / "assets" / "minecraft" / "models" / "block" / "water.json",
              R"({"textures": {"particle": "block/water_still"}})");
    WriteFile(base / "assets" / "minecraft" / "blockstates" / "lava.json",
              R"({"variants": {"": {"model": "block/lava"}}})");
    WriteFile(base / "assets" / "minecraft" / "models" / "block" / "lava.json",
              R"({"textures": {"particle": "block/lava_still"}})");
    WriteFile(base / "assets" / "minecraft" / "models" / "block" / "cube.json",
              R"({"textures": {"particle": "block/stone"},)"
              R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
              R"( "faces": {)"
              R"(   "north": {"texture": "#all"},)"
              R"(   "south": {"texture": "#all"},)"
              R"(   "east":  {"texture": "#all"},)"
              R"(   "west":  {"texture": "#all"},)"
              R"(   "up":    {"texture": "#all"},)"
              R"(   "down":  {"texture": "#all"}}}]})");
    for (const char* tex : {"stone", "glass"}) {
      std::vector<std::uint8_t> rgba(16 * 16 * 4, 0x80);
      WritePngFile(base / "assets" / "minecraft" / "textures" / "block" /
                       (std::string(tex) + ".png"),
                   16,
                   16,
                   rgba.data());
    }
    for (auto [tex, dim] : {std::pair{"leaves_oak", 10},
                            {"leaves_birch", 14}}) {
      std::vector<std::uint8_t> rgba(static_cast<std::size_t>(dim) * dim * 4,
                                     0x70);
      WritePngFile(base / "assets" / "minecraft" / "textures" / "block" /
                       (std::string(tex) + ".png"),
                   dim,
                   dim,
                   rgba.data());
    }
    // Water still texture: a 16x32 animated strip (two frames) to also
    // cover first-frame cropping through the atlas path.
    std::vector<std::uint8_t> water(16 * 32 * 4, 0x60);
    WritePngFile(base / "assets" / "minecraft" / "textures" / "block" /
                     "water_still.png",
                 16,
                 32,
                 water.data());
    std::vector<std::uint8_t> lava(16 * 16 * 4, 0xE0);
    WritePngFile(
        base / "assets" / "minecraft" / "textures" / "block" / "lava_still.png",
        16,
        16,
        lava.data());
    if (with_flow_textures) {
      // 8x8 so atlas slots are distinguishable from the 16x16 still
      // frame by size; water_still's 16x32 strip already covers the
      // animated first-frame crop.
      std::vector<std::uint8_t> water_flow(8 * 8 * 4, 0x61);
      WritePngFile(base / "assets" / "minecraft" / "textures" / "block" /
                       "water_flow.png",
                   8,
                   8,
                   water_flow.data());
      std::vector<std::uint8_t> lava_flow(8 * 8 * 4, 0xE1);
      WritePngFile(base / "assets" / "minecraft" / "textures" / "block" /
                       "lava_flow.png",
                   8,
                   8,
                   lava_flow.data());
    }
    if (with_overlay_texture) {
      // 12x12: size-disjoint from the 16x16 still frame and the 8x8
      // flow textures, so SlotSizePx identifies the overlay slot.
      std::vector<std::uint8_t> water_overlay(12 * 12 * 4, 0x62);
      WritePngFile(base / "assets" / "minecraft" / "textures" / "block" /
                       "water_overlay.png",
                   12,
                   12,
                   water_overlay.data());
    }
  }
};

// Region over a custom palette; `fill` maps each (x,y,z) to a palette
// index (mirror of StoneRegion with a caller-defined palette).
struct MixedRegion {
  std::unique_ptr<fschema::memory::Arena> arena =
      std::make_unique<fschema::memory::Arena>();
  fschema::ir::Region region{};

  MixedRegion(std::vector<fschema::ir::BlockState> palette,
              std::vector<std::uint8_t> air_lut,
              const std::vector<std::uint16_t>& fill,
              std::int32_t sx,
              std::int32_t sy,
              std::int32_t sz) {
    region.bounds = fschema::ir::BoundingBox{{0, 0, 0}, {sx, sy, sz}};
    region.palette = std::move(palette);
    region.is_air_lut = std::move(air_lut);
    region.block_indices = fschema::memory::UnInitBuffer<std::uint16_t>(
        static_cast<std::size_t>(sx) * sy * sz, arena.get());
    std::uint16_t* data =
        reinterpret_cast<std::uint16_t*>(region.block_indices.data());
    for (std::size_t i = 0; i < fill.size(); ++i) data[i] = fill[i];
    region.is_materialized = true;
    region.lazy_source.encoding = fschema::ir::BlockDataEncoding::kNone;
  }
};

fschema::ir::BlockState State(std::string_view name) {
  // BlockState::name is a view. Point it at the caller's string data
  // (every call site passes a literal) rather than a temporary
  // std::string whose buffer dies with this call.
  return fschema::ir::BlockState{
      .name = name, .prop_encoding = fschema::ir::PropertyEncoding::kNone};
}

fschema::ir::BlockState StateWithProps(std::string_view name,
                                       std::string_view kv) {
  return fschema::ir::BlockState{
      .name = name,
      .raw_properties = std::span<const std::byte>(
          reinterpret_cast<const std::byte*>(kv.data()), kv.size()),
      .prop_encoding = fschema::ir::PropertyEncoding::kString,
  };
}

// Locates the atlas slot containing a quad's four uv pairs and
// converts them back to face uvs (0..1 within that slot's texture);
// slot_out receives the slot index. Returns false when no single
// slot holds the whole quad.
[[nodiscard]] bool FaceUvsOfQuad(const Mesh& mesh,
                                 const TextureAtlas& atlas,
                                 std::size_t quad,
                                 float u_out[4],
                                 float v_out[4],
                                 std::size_t* slot_out) {
  constexpr float kEps = 1e-5f;
  for (std::size_t s = 0; s < atlas.slots.size(); ++s) {
    const auto& slot = atlas.slots[s];
    bool inside = true;
    for (int k = 0; k < 4; ++k) {
      const float u = mesh.uvs[(quad * 4 + k) * 2 + 0];
      const float v = mesh.uvs[(quad * 4 + k) * 2 + 1];
      if (u < slot.u0 - kEps || u > slot.u1 + kEps || v < slot.v0 - kEps ||
          v > slot.v1 + kEps) {
        inside = false;
        break;
      }
    }
    if (!inside) continue;
    for (int k = 0; k < 4; ++k) {
      const float u = mesh.uvs[(quad * 4 + k) * 2 + 0];
      const float v = mesh.uvs[(quad * 4 + k) * 2 + 1];
      u_out[k] = (u - slot.u0) / (slot.u1 - slot.u0);
      v_out[k] = (v - slot.v0) / (slot.v1 - slot.v0);
    }
    if (slot_out) *slot_out = s;
    return true;
  }
  return false;
}

// Slot size in texels (the fixture writes known sizes: 8x8 flow
// textures vs the 16x16 still frame).
[[nodiscard]] std::pair<float, float> SlotSizePx(const TextureAtlas& atlas,
                                                 std::size_t slot) {
  const auto& s = atlas.slots[slot];
  return {(s.u1 - s.u0) * static_cast<float>(atlas.width),
          (s.v1 - s.v0) * static_cast<float>(atlas.height)};
}

// True when quad `q`'s four corner positions duplicate an earlier
// quad's: fluid back faces re-emit the front's corners in reverse
// order (the exact same float values), so the sorted position
// signatures match. Distinct faces never share all four corners.
[[nodiscard]] bool IsBackQuad(const Mesh& m, std::size_t q) {
  std::array<std::array<float, 3>, 4> sig;
  for (int k = 0; k < 4; ++k) {
    sig[k] = {m.positions[(q * 4 + k) * 3 + 0],
              m.positions[(q * 4 + k) * 3 + 1],
              m.positions[(q * 4 + k) * 3 + 2]};
  }
  std::sort(sig.begin(), sig.end());
  for (std::size_t j = 0; j < q; ++j) {
    std::array<std::array<float, 3>, 4> other;
    for (int k = 0; k < 4; ++k) {
      other[k] = {m.positions[(j * 4 + k) * 3 + 0],
                  m.positions[(j * 4 + k) * 3 + 1],
                  m.positions[(j * 4 + k) * 3 + 2]};
    }
    std::sort(other.begin(), other.end());
    if (sig == other) return true;
  }
  return false;
}

TEST(TessellateTest, TransparentNeighborDoesNotCullOpaqueFace) {
  GlassWaterPackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // 2x1x1: stone at x=0, glass at x=1.
  MixedRegion sr({State("minecraft:air"),
                  State("minecraft:stone"),
                  State("minecraft:glass")},
                 {1, 0, 0},
                 {1, 2},
                 2,
                 1,
                 1);
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // Stone keeps all 6 faces (a glass neighbor no longer culls them);
  // glass loses only its -X face against the opaque stone.
  EXPECT_EQ(r->opaque_mesh.VertexCount(), 6u * 4u);
  EXPECT_EQ(r->transparent_mesh.VertexCount(), 5u * 4u);
  EXPECT_TRUE(r->HasTransparency());
}

TEST(TessellateTest, SameTransparentBlocksCullEachOther) {
  GlassWaterPackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // 2x1x1 glass column: both interior faces cull (same group).
  MixedRegion gr({State("minecraft:air"), State("minecraft:glass")},
                 {1, 0},
                 {1, 1},
                 2,
                 1,
                 1);
  auto r =
      TessellateRegion(gr.region, *gr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  EXPECT_EQ(r->transparent_mesh.VertexCount(), 10u * 4u);
  EXPECT_EQ(r->opaque_mesh.VertexCount(), 0u);
}

TEST(TessellateTest, WaterGoesToTransparentMeshWithTint) {
  GlassWaterPackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // A single source-water block: synthesized cuboid, transparent mesh,
  // water tint slot.
  MixedRegion wr(
      {State("minecraft:air"), State("minecraft:water")}, {1, 0}, {1}, 1, 1, 1);
  auto r =
      TessellateRegion(wr.region, *wr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  EXPECT_EQ(r->opaque_mesh.VertexCount(), 0u);
  // 6 fronts + 5 back faces: vanilla doubles every fluid face except
  // the down face (up via shouldRenderBackwardUpFace, all 4 sides).
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (6u + 5u) * 4u);
  bool saw_water_tint = false;
  for (std::uint8_t t : r->transparent_mesh.tints) {
    if (t == 3) saw_water_tint = true;
  }
  EXPECT_TRUE(saw_water_tint) << "water vertices carry tint slot 3";
  // Animated strip cropped to one 16x16 frame in the atlas.
  ASSERT_EQ(r->atlas.slots.size(), 1u);
}

TEST(TessellateTest, FluidSurfaceSlopesBetweenHeights) {
  GlassWaterPackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // 2x1x1: source (amount 8) next to level=3 (amount 5).
  MixedRegion fr({State("minecraft:air"),
                  StateWithProps("minecraft:water", "level=0"),
                  StateWithProps("minecraft:water", "level=3")},
                 {1, 0, 0},
                 {1, 2},
                 2,
                 1,
                 1);
  auto r =
      TessellateRegion(fr.region, *fr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // Up/down plus the three outward sides per block; the shared
  // east/west pair culls between same-fluid neighbors. Back faces
  // double both tops and all six outward sides (10 + 8 quads).
  ASSERT_EQ(r->opaque_mesh.VertexCount(), 0u);
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (10u + 8u) * 4u);

  // Quads are 4 sequential vertices; up-face winding is
  // v0=(x0,z0) v1=(x1,z0) v2=(x1,z1) v3=(x0,z1).
  const Mesh& m = r->transparent_mesh;
  constexpr float kEps = 1e-4f;
  const float kDip = 20.0f / 27.0f;      // (8/9*10)/(10+1+1)
  const float kToward = 85.0f / 117.0f;  // (8/9*10 + 5/9)/(10+3)
  const float kWeak = 5.0f / 27.0f;      // (5/9)/(1+1+1)
  int saw_block0_top = 0;
  int backs = 0;
  for (std::size_t i = 0; i < m.VertexCount(); i += 4) {
    if (IsBackQuad(m, i / 4)) {
      ++backs;
      continue;
    }
    bool is_top = true;
    for (int k = 0; k < 4; ++k) {
      if (m.positions[(i + k) * 3 + 1] < 0.15f) is_top = false;
    }
    if (!is_top) continue;
    float xmax = m.positions[i * 3];
    for (int k = 1; k < 4; ++k) {
      const float x = m.positions[(i + k) * 3];
      if (x > xmax) xmax = x;
    }
    if (xmax <= 1.0f + kEps) {
      // Block 0: v0/v3 at x=0 dip, v1/v2 at x=1 toward the neighbor.
      ++saw_block0_top;
      EXPECT_NEAR(m.positions[i * 3 + 1], kDip, kEps);
      EXPECT_NEAR(m.positions[(i + 1) * 3 + 1], kToward, kEps);
      EXPECT_NEAR(m.positions[(i + 2) * 3 + 1], kToward, kEps);
      EXPECT_NEAR(m.positions[(i + 3) * 3 + 1], kDip, kEps);
    } else {
      // Block 1: v0/v3 at x=1 toward the source, v1/v2 at x=2 weak.
      EXPECT_NEAR(m.positions[i * 3 + 1], kToward, kEps);
      EXPECT_NEAR(m.positions[(i + 1) * 3 + 1], kWeak, kEps);
      EXPECT_NEAR(m.positions[(i + 2) * 3 + 1], kWeak, kEps);
      EXPECT_NEAR(m.positions[(i + 3) * 3 + 1], kToward, kEps);
    }
  }
  EXPECT_EQ(saw_block0_top, 1);
  EXPECT_EQ(backs, 8);  // both tops + six outward sides
}

TEST(TessellateTest, FluidColumnFillsSidesAndCullsTopBetween) {
  GlassWaterPackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // 1x2x1 source column: the bottom block has the same fluid above,
  // so all corners reach 1.0 (no 1/9 gap) and its top face culls.
  MixedRegion cr({State("minecraft:air"), State("minecraft:water")},
                 {1, 0},
                 {1, 1},
                 1,
                 2,
                 1);
  auto r =
      TessellateRegion(cr.region, *cr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // Bottom: down + 4 full sides; top: up + 4 sides (its down face
  // culls against the same fluid below). Back faces double the top
  // block's up face (the bottom's up face culls) and all eight sides.
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (10u + 9u) * 4u);

  const Mesh& m = r->transparent_mesh;
  constexpr float kEps = 1e-4f;
  const float kSurface = 1.0f + 20.0f / 27.0f;  // world y of the top
  int full_sides = 0;
  int dip_tops = 0;
  int backs = 0;
  for (std::size_t i = 0; i < m.VertexCount(); i += 4) {
    if (IsBackQuad(m, i / 4)) {
      ++backs;
      continue;
    }
    float ymin = m.positions[i * 3 + 1];
    float ymax = ymin;
    for (int k = 1; k < 4; ++k) {
      const float y = m.positions[(i + k) * 3 + 1];
      if (y < ymin) ymin = y;
      if (y > ymax) ymax = y;
    }
    if (ymin < kEps && ymax > 1.0f - kEps) {
      ++full_sides;  // spans the whole block: the old 1/9 gap is gone
    } else if (ymin > kSurface - kEps && ymax < kSurface + kEps) {
      ++dip_tops;  // isolated-source dip surface at 1 + 20/27
    }
  }
  EXPECT_EQ(full_sides, 4);
  EXPECT_EQ(dip_tops, 1);
  EXPECT_EQ(backs, 9);
}

TEST(TessellateTest, DifferentFluidsAverageIndependently) {
  GlassWaterPackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // 2x1x1: water source + lava source. A different fluid behaves
  // like air in vanilla's averaging (neighbor block !isSolid -> 0.0),
  // so both surfaces stay flat at the isolated-source dip.
  MixedRegion wr({State("minecraft:air"),
                  State("minecraft:water"),
                  State("minecraft:lava")},
                 {1, 0, 0},
                 {1, 2},
                 2,
                 1,
                 1);
  auto r =
      TessellateRegion(wr.region, *wr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // Fluids do not occlude (vanilla canOcclude is false for fluid
  // blocks), so the water|lava interface renders on both sides:
  // water keeps all 6 faces plus 5 back faces (up + 4 sides) in the
  // transparent mesh; lava keeps all 6 plus 5 backs (up + 4 sides)
  // in the opaque mesh.
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (6u + 5u) * 4u);
  ASSERT_EQ(r->opaque_mesh.VertexCount(), (6u + 5u) * 4u);

  constexpr float kEps = 1e-4f;
  const float kDip = 20.0f / 27.0f;
  for (const Mesh* mesh : {&r->transparent_mesh, &r->opaque_mesh}) {
    int tops = 0;
    int backs = 0;
    for (std::size_t i = 0; i < mesh->VertexCount(); i += 4) {
      if (IsBackQuad(*mesh, i / 4)) {
        ++backs;
        continue;
      }
      bool all_high = true;
      for (int k = 0; k < 4; ++k) {
        if (mesh->positions[(i + k) * 3 + 1] < 0.7f) all_high = false;
      }
      if (!all_high) continue;
      ++tops;
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(mesh->positions[(i + k) * 3 + 1], kDip, kEps);
      }
    }
    EXPECT_EQ(tops, 1);
    EXPECT_EQ(backs, mesh == &r->transparent_mesh ? 5 : 5);
  }
}

TEST(TessellateTest, SolidNeighborExcludedFromFluidAverage) {
  GlassWaterPackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // 2x1x1: water source + stone. Vanilla excludes solid cells from
  // the average entirely (-1 sentinel), so the shared edge RISES:
  // (8/9*10)/11 = 80/99 vs 20/27 on the air side.
  MixedRegion sr({State("minecraft:air"),
                  State("minecraft:water"),
                  State("minecraft:stone")},
                 {1, 0, 0},
                 {1, 2},
                 2,
                 1,
                 1);
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // 5 fronts + 4 back faces (up + the three outward sides; the east
  // face culls against the opaque stone).
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (5u + 4u) * 4u);
  ASSERT_EQ(r->opaque_mesh.VertexCount(), 6u * 4u);

  const Mesh& m = r->transparent_mesh;
  constexpr float kEps = 1e-4f;
  const float kDip = 20.0f / 27.0f;
  const float kSolid = 80.0f / 99.0f;
  int tops = 0;
  int backs = 0;
  for (std::size_t i = 0; i < m.VertexCount(); i += 4) {
    if (IsBackQuad(m, i / 4)) {
      ++backs;
      continue;
    }
    bool all_high = true;
    for (int k = 0; k < 4; ++k) {
      if (m.positions[(i + k) * 3 + 1] < 0.7f) all_high = false;
    }
    if (!all_high) continue;
    ++tops;
    // v0/v3 at x=0 (air side), v1/v2 at x=1 (stone side).
    EXPECT_NEAR(m.positions[i * 3 + 1], kDip, kEps);
    EXPECT_NEAR(m.positions[(i + 1) * 3 + 1], kSolid, kEps);
    EXPECT_NEAR(m.positions[(i + 2) * 3 + 1], kSolid, kEps);
    EXPECT_NEAR(m.positions[(i + 3) * 3 + 1], kDip, kEps);
  }
  EXPECT_EQ(tops, 1);
  EXPECT_EQ(backs, 4);
}

TEST(TessellateTest, FluidFacesBypassCoplanarMerge) {
  GlassWaterPackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // Same pair as FluidSurfaceSlopesBetweenHeights, with greedy merge
  // on: fluid faces must be spliced out before merging, or MergeGroup
  // rebuilds them from block-grid bounds and destroys the slope (and,
  // pre-patch, raised the sub-block sides to full height).
  MixedRegion fr({State("minecraft:air"),
                  StateWithProps("minecraft:water", "level=0"),
                  StateWithProps("minecraft:water", "level=3")},
                 {1, 0, 0},
                 {1, 2},
                 2,
                 1,
                 1);
  TessellationOptions opts;
  opts.merge_coplanar_faces = true;
  auto r = TessellateRegion(fr.region, *fr.arena, **pack, opts);
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // Nothing merges: without the splice the two coplanar north faces
  // would collapse into one quad (9 faces / 36 verts). Back faces add
  // 8 more quads (both tops + six outward sides).
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (10u + 8u) * 4u);

  const Mesh& m = r->transparent_mesh;
  constexpr float kEps = 1e-4f;
  const float kDip = 20.0f / 27.0f;
  const float kToward = 85.0f / 117.0f;
  // Block 0's west side keeps its sub-block top edge (not rebuilt as
  // a full-height quad): v1=(0,nw,0), v2=(0,sw,1), v0/v3 at y=0.
  int west_sides = 0;
  int backs = 0;
  for (std::size_t i = 0; i < m.VertexCount(); i += 4) {
    if (IsBackQuad(m, i / 4)) {
      ++backs;
      continue;
    }
    bool all_x0 = true;
    for (int k = 0; k < 4; ++k) {
      if (m.positions[(i + k) * 3] > kEps) all_x0 = false;
    }
    if (!all_x0) continue;
    ++west_sides;
    EXPECT_NEAR(m.positions[i * 3 + 1], 0.0f, kEps);
    EXPECT_NEAR(m.positions[(i + 1) * 3 + 1], kDip, kEps);
    EXPECT_NEAR(m.positions[(i + 2) * 3 + 1], kDip, kEps);
    EXPECT_NEAR(m.positions[(i + 3) * 3 + 1], 0.0f, kEps);
  }
  EXPECT_EQ(west_sides, 1);
  EXPECT_EQ(backs, 8);
  // And block 0's top face stays sloped.
  int sloped_tops = 0;
  for (std::size_t i = 0; i < m.VertexCount(); i += 4) {
    if (IsBackQuad(m, i / 4)) continue;
    bool all_high = true;
    for (int k = 0; k < 4; ++k) {
      if (m.positions[(i + k) * 3 + 1] < 0.15f) all_high = false;
    }
    if (!all_high) continue;
    float xmax = m.positions[i * 3];
    for (int k = 1; k < 4; ++k) {
      const float x = m.positions[(i + k) * 3];
      if (x > xmax) xmax = x;
    }
    if (xmax > 1.0f + kEps) continue;  // block 1's top
    ++sloped_tops;
    EXPECT_NEAR(m.positions[i * 3 + 1], kDip, kEps);
    EXPECT_NEAR(m.positions[(i + 1) * 3 + 1], kToward, kEps);
  }
  EXPECT_EQ(sloped_tops, 1);
}

TEST(TessellateTest, FluidFacesPickFlowTextureWhenPackShipsIt) {
  GlassWaterPackFixture fx(/*with_flow_textures=*/true);
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // Sides carry the flow texture, up/down keep the still one (vanilla
  // FluidRenderer); lava mirrors water.
  const fschema::ir::BlockState ws = State("minecraft:water");
  const fschema::geometry::BlockProps wprops(ws);
  auto w = (*pack)->Resolve("minecraft:water", wprops);
  ASSERT_TRUE(w.has_value()) << ToString(w.error());
  ASSERT_EQ(w->faces.size(), 6u);
  for (const auto& f : w->faces) {
    const std::uint8_t cull = f.CullFace();
    const bool is_side = cull != 3u /*kPosY*/ && cull != 4u /*kNegY*/;
    EXPECT_EQ(
        f.texture_path,
        is_side ? "minecraft:block/water_flow" : "minecraft:block/water_still");
  }

  const fschema::ir::BlockState ls = State("minecraft:lava");
  const fschema::geometry::BlockProps lprops(ls);
  auto l = (*pack)->Resolve("minecraft:lava", lprops);
  ASSERT_TRUE(l.has_value()) << ToString(l.error());
  for (const auto& f : l->faces) {
    const std::uint8_t cull = f.CullFace();
    const bool is_side = cull != 3u && cull != 4u;
    EXPECT_EQ(
        f.texture_path,
        is_side ? "minecraft:block/lava_flow" : "minecraft:block/lava_still");
  }

  // Without the flow textures every face keeps the still texture.
  GlassWaterPackFixture plain;
  auto pack2 = ResourcePack::Load(plain.root.Get().generic_string());
  ASSERT_TRUE(pack2.has_value());
  const fschema::ir::BlockState ws2 = State("minecraft:water");
  const fschema::geometry::BlockProps wprops2(ws2);
  auto w2 = (*pack2)->Resolve("minecraft:water", wprops2);
  ASSERT_TRUE(w2.has_value()) << ToString(w2.error());
  for (const auto& f : w2->faces) {
    EXPECT_EQ(f.texture_path, "minecraft:block/water_still");
  }
}

TEST(TessellateTest, FluidSidesSampleFlowWindowByCornerHeight) {
  GlassWaterPackFixture fx(/*with_flow_textures=*/true);
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // An isolated source: every corner dips to 20/27, the surface is
  // still (all neighbors out of bounds), so the top keeps the still
  // texture full rect while the sides sample the flow window.
  MixedRegion wr(
      {State("minecraft:air"), State("minecraft:water")}, {1, 0}, {1}, 1, 1, 1);
  auto r =
      TessellateRegion(wr.region, *wr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // 6 fronts + 5 back faces (up + all 4 sides; no overlay present).
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (6u + 5u) * 4u);
  ASSERT_EQ(r->atlas.slots.size(), 2u);  // still frame + flow texture

  const Mesh& m = r->transparent_mesh;
  constexpr float kEps = 1e-4f;
  const float kTopV = (1.0f - 20.0f / 27.0f) * 0.5f;  // 7/54
  int tops = 0, downs = 0, sides = 0, backs = 0;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    if (IsBackQuad(m, q)) {
      ++backs;
      continue;
    }
    const float nx = m.normals[q * 12 + 0];
    const float ny = m.normals[q * 12 + 1];
    const float nz = m.normals[q * 12 + 2];
    float u[4], v[4];
    std::size_t slot = 0;
    ASSERT_TRUE(FaceUvsOfQuad(m, r->atlas, q, u, v, &slot));
    const auto [sw, sh] = SlotSizePx(r->atlas, slot);
    if (ny > 0.5f) {
      // Top: still texture, full rect; corner order NW, NE, SE, SW.
      ++tops;
      EXPECT_NEAR(sw, 16.0f, 0.5f);
      EXPECT_NEAR(sh, 16.0f, 0.5f);
      const float want[4][2] = {
          {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], want[k][0], kEps);
        EXPECT_NEAR(v[k], want[k][1], kEps);
      }
    } else if (ny < -0.5f) {
      // Down: still texture, full rect in BuildCubeFace corner order
      // (NW, SW, SE, NE) with the vanilla down basis u = +X, v = +Z.
      ++downs;
      EXPECT_NEAR(sw, 16.0f, 0.5f);
      EXPECT_NEAR(sh, 16.0f, 0.5f);
      const float want[4][2] = {
          {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], want[k][0], kEps);
        EXPECT_NEAR(v[k], want[k][1], kEps);
      }
    } else {
      // Sides: flow texture (8x8), u in [0, 1/2], v in [7/54, 1/2].
      ++sides;
      EXPECT_NEAR(sw, 8.0f, 0.5f);
      EXPECT_NEAR(sh, 8.0f, 0.5f);
      const float (*want)[2] = nullptr;
      if (nz < -0.5f) {  // north: bottom-NW, bottom-NE, top-NE, top-NW
        static const float w[4][2] = {
            {0.0f, 0.5f}, {0.5f, 0.5f}, {0.5f, kTopV}, {0.0f, kTopV}};
        want = w;
      } else if (nz > 0.5f) {  // south: bottom-SW, top-SW, top-SE, b-SE
        static const float w[4][2] = {
            {0.5f, 0.5f}, {0.5f, kTopV}, {0.0f, kTopV}, {0.0f, 0.5f}};
        want = w;
      } else if (nx < -0.5f) {  // west: bottom-NW, top-NW, top-SW, b-SW
        static const float w[4][2] = {
            {0.5f, 0.5f}, {0.5f, kTopV}, {0.0f, kTopV}, {0.0f, 0.5f}};
        want = w;
      } else {  // east: bottom-NE, bottom-SE, top-SE, top-NE
        static const float w[4][2] = {
            {0.0f, 0.5f}, {0.5f, 0.5f}, {0.5f, kTopV}, {0.0f, kTopV}};
        want = w;
      }
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], want[k][0], kEps);
        EXPECT_NEAR(v[k], want[k][1], kEps);
      }
    }
  }
  EXPECT_EQ(tops, 1);
  EXPECT_EQ(downs, 1);
  EXPECT_EQ(sides, 4);
  EXPECT_EQ(backs, 5);
}

TEST(TessellateTest, FluidFlowingTopRotatesTowardFlow) {
  GlassWaterPackFixture fx(/*with_flow_textures=*/true);
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // Source (level 0) beside flowing level=3: both surfaces flow east
  // (height drop toward the neighbor / away from the source), so both
  // tops swap to the flow texture and rotate the sampling window
  // (atan2(0, +) - pi/2 = -pi/2: c ~ 0, s = -1/4).
  MixedRegion fr({State("minecraft:air"),
                  StateWithProps("minecraft:water", "level=0"),
                  StateWithProps("minecraft:water", "level=3")},
                 {1, 0, 0},
                 {1, 2},
                 2,
                 1,
                 1);
  auto r =
      TessellateRegion(fr.region, *fr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  ASSERT_EQ(r->transparent_mesh.VertexCount(), (10u + 8u) * 4u);

  const Mesh& m = r->transparent_mesh;
  constexpr float kEps = 1e-4f;
  int flowing_tops = 0;
  int backs = 0;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    if (IsBackQuad(m, q)) {
      ++backs;
      continue;
    }
    if (m.normals[q * 12 + 1] <= 0.5f) continue;  // up faces only
    float u[4], v[4];
    std::size_t slot = 0;
    ASSERT_TRUE(FaceUvsOfQuad(m, r->atlas, q, u, v, &slot));
    const auto [sw, sh] = SlotSizePx(r->atlas, slot);
    EXPECT_NEAR(sw, 8.0f, 0.5f);  // flow texture, not still
    EXPECT_NEAR(sh, 8.0f, 0.5f);
    // Corner order NW, NE, SE, SW; window rotated by -pi/2.
    const float want[4][2] = {
        {0.75f, 0.25f}, {0.75f, 0.75f}, {0.25f, 0.75f}, {0.25f, 0.25f}};
    for (int k = 0; k < 4; ++k) {
      EXPECT_NEAR(u[k], want[k][0], kEps);
      EXPECT_NEAR(v[k], want[k][1], kEps);
    }
    ++flowing_tops;
  }
  EXPECT_EQ(flowing_tops, 2);  // both blocks' surfaces flow east
  EXPECT_EQ(backs, 8);
}

TEST(TessellateTest, FluidFlowPullsTowardHoleBelowNeighbor) {
  GlassWaterPackFixture fx(/*with_flow_textures=*/true);
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // 1x2x2: stone at (0,0,0), water at (0,0,1) and (0,1,0), air at
  // (0,1,1). The upper water spills south into a hole (air above a
  // source): its surface flows south (angle 0: unrotated window,
  // c = 1/4 exactly), while the hole's own column stays still.
  MixedRegion hr({State("minecraft:air"),
                  State("minecraft:water"),
                  State("minecraft:stone")},
                 {1, 0, 0},
                 {2, 1, 1, 0},
                 1,
                 2,
                 2);
  auto r =
      TessellateRegion(hr.region, *hr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // Water: 5 faces per block (the spiller's down face culls against
  // stone, the hole column's north face culls against stone); stone
  // keeps all 6 (water does not cull opaque faces). Back faces add 9
  // quads (both up faces + 4 spiller sides + 3 hole sides).
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (10u + 9u) * 4u);
  ASSERT_EQ(r->opaque_mesh.VertexCount(), 6u * 4u);

  const Mesh& m = r->transparent_mesh;
  constexpr float kEps = 1e-4f;
  int flow_tops = 0, still_tops = 0, backs = 0;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    if (IsBackQuad(m, q)) {
      ++backs;
      continue;
    }
    if (m.normals[q * 12 + 1] <= 0.5f) continue;  // up faces only
    float u[4], v[4];
    std::size_t slot = 0;
    ASSERT_TRUE(FaceUvsOfQuad(m, r->atlas, q, u, v, &slot));
    const auto [sw, sh] = SlotSizePx(r->atlas, slot);
    if (sw < 12.0f) {
      // The spilling block: flow texture, unrotated window.
      ++flow_tops;
      EXPECT_NEAR(sh, 8.0f, 0.5f);
      const float want[4][2] = {
          {0.25f, 0.25f}, {0.75f, 0.25f}, {0.75f, 0.75f}, {0.25f, 0.75f}};
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], want[k][0], kEps);
        EXPECT_NEAR(v[k], want[k][1], kEps);
      }
    } else {
      // The hole's own column: still texture, full rect.
      ++still_tops;
      EXPECT_NEAR(sw, 16.0f, 0.5f);
      EXPECT_NEAR(sh, 16.0f, 0.5f);
      const float want[4][2] = {
          {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], want[k][0], kEps);
        EXPECT_NEAR(v[k], want[k][1], kEps);
      }
    }
  }
  EXPECT_EQ(flow_tops, 1);
  EXPECT_EQ(still_tops, 1);
  EXPECT_EQ(backs, 9);
}

TEST(TessellateTest, FluidSidesKeepStillWhenPackLacksFlowTexture) {
  GlassWaterPackFixture fx;  // no flow textures shipped
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // The sloped pair from FluidSurfaceSlopesBetweenHeights: without a
  // flow texture every face keeps the still texture and the full uv
  // rect (the fallback contract; orientation follows the vanilla
  // per-direction basis).
  MixedRegion fr({State("minecraft:air"),
                  StateWithProps("minecraft:water", "level=0"),
                  StateWithProps("minecraft:water", "level=3")},
                 {1, 0, 0},
                 {1, 2},
                 2,
                 1,
                 1);
  auto r =
      TessellateRegion(fr.region, *fr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  ASSERT_EQ(r->transparent_mesh.VertexCount(), (10u + 8u) * 4u);
  ASSERT_EQ(r->atlas.slots.size(), 1u);  // only the still texture

  const Mesh& m = r->transparent_mesh;
  constexpr float kEps = 1e-4f;
  int norths = 0;
  int backs = 0;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    if (IsBackQuad(m, q)) {
      ++backs;
      continue;
    }
    float u[4], v[4];
    std::size_t slot = 0;
    ASSERT_TRUE(FaceUvsOfQuad(m, r->atlas, q, u, v, &slot));
    if (m.normals[q * 12 + 2] > -0.5f) continue;  // north faces only
    ++norths;
    // Full rect in BuildCubeFace corner order (bottom-NW, bottom-NE,
    // top-NE, top-NW), upright per the vanilla north basis: the
    // image's top row lands on the face's top edge (v = 0 there).
    const float want_u[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float want_v[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    for (int k = 0; k < 4; ++k) {
      EXPECT_NEAR(u[k], want_u[k], kEps);
      EXPECT_NEAR(v[k], want_v[k], kEps);
    }
  }
  EXPECT_EQ(norths, 2);  // one per water block
  EXPECT_EQ(backs, 8);
}

TEST(TessellateTest, FluidSidesUseOverlayTextureAgainstHalfTransparentBlocks) {
  GlassWaterPackFixture fx(/*with_flow_textures=*/true,
                           /*with_overlay_texture=*/true);
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // 3x1x1: glass | water | stone. The water's west side touches glass
  // (a HalfTransparentBlock), so it swaps to the overlay texture with
  // the same flow-window sampling (FluidRenderer); the east
  // side culls against the opaque stone; north/south face out-of-
  // bounds air and keep the flow texture; up/down keep still. All
  // four water corners are 80/99: the glass AND stone neighbors are
  // solid (isSolid) and drop out of the average while the air above
  // pulls with weight 1.
  MixedRegion sr({State("minecraft:air"),
                  State("minecraft:glass"),
                  State("minecraft:water"),
                  State("minecraft:stone")},
                 {1, 0, 0, 0},
                 {1, 2, 3},
                 3,
                 1,
                 1);
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // Glass keeps all 6 faces (water does not cull it); water keeps 5
  // (the east face culls against stone) plus 3 back faces (up, north,
  // south -- the overlay west side stays single-sided); stone keeps 6.
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (6u + 5u + 3u) * 4u);
  ASSERT_EQ(r->opaque_mesh.VertexCount(), 6u * 4u);

  const Mesh& m = r->transparent_mesh;
  constexpr float kEps = 1e-4f;
  const float kTop = 80.0f / 99.0f;
  const float kTopV = (1.0f - kTop) * 0.5f;  // 19/198
  int glass = 0, tops = 0, downs = 0, overlay_sides = 0, flow_sides = 0;
  int backs = 0;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    if (IsBackQuad(m, q)) {
      ++backs;
      continue;
    }
    float xmin = 1e30f, xmax = -1e30f;
    for (int k = 0; k < 4; ++k) {
      const float px = m.positions[(q * 4 + k) * 3 + 0];
      xmin = std::min(xmin, px);
      xmax = std::max(xmax, px);
    }
    const float ny = m.normals[q * 12 + 1];
    const float nx = m.normals[q * 12 + 0];
    const float nz = m.normals[q * 12 + 2];
    const bool is_water_west = nx < -0.5f && xmin > 1.0f - kEps;
    if (xmax < 1.5f && !is_water_west) {
      ++glass;
      continue;
    }
    float u[4], v[4];
    std::size_t slot = 0;
    ASSERT_TRUE(FaceUvsOfQuad(m, r->atlas, q, u, v, &slot));
    const auto [sw, sh] = SlotSizePx(r->atlas, slot);
    if (ny > 0.5f) {
      // Water top: still texture, full rect, every corner at 80/99
      // (locks the sentinel fix: the glass side no longer dips).
      ++tops;
      EXPECT_NEAR(sw, 16.0f, 0.5f);
      EXPECT_NEAR(sh, 16.0f, 0.5f);
      const float want[4][2] = {
          {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], want[k][0], kEps);
        EXPECT_NEAR(v[k], want[k][1], kEps);
        EXPECT_NEAR(m.positions[(q * 4 + k) * 3 + 1], kTop, kEps);
      }
    } else if (ny < -0.5f) {
      // Water bottom: still texture, full rect.
      ++downs;
      EXPECT_NEAR(sw, 16.0f, 0.5f);
      EXPECT_NEAR(sh, 16.0f, 0.5f);
      const float want[4][2] = {
          {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], want[k][0], kEps);
        EXPECT_NEAR(v[k], want[k][1], kEps);
      }
    } else if (nx < -0.5f) {
      // West (against glass): OVERLAY texture, flow window values.
      ++overlay_sides;
      EXPECT_NEAR(sw, 12.0f, 0.5f);
      EXPECT_NEAR(sh, 12.0f, 0.5f);
      const float want[4][2] = {
          {0.5f, 0.5f}, {0.5f, kTopV}, {0.0f, kTopV}, {0.0f, 0.5f}};
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], want[k][0], kEps);
        EXPECT_NEAR(v[k], want[k][1], kEps);
      }
    } else {
      // North/south (out of bounds): flow texture, same window.
      ++flow_sides;
      EXPECT_NEAR(sw, 8.0f, 0.5f);
      EXPECT_NEAR(sh, 8.0f, 0.5f);
      const float (*want)[2] = nullptr;
      if (nz < -0.5f) {  // north
        static const float w[4][2] = {
            {0.0f, 0.5f}, {0.5f, 0.5f}, {0.5f, kTopV}, {0.0f, kTopV}};
        want = w;
      } else {  // south
        static const float w[4][2] = {
            {0.5f, 0.5f}, {0.5f, kTopV}, {0.0f, kTopV}, {0.0f, 0.5f}};
        want = w;
      }
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], want[k][0], kEps);
        EXPECT_NEAR(v[k], want[k][1], kEps);
      }
    }
  }
  EXPECT_EQ(glass, 6);
  EXPECT_EQ(tops, 1);
  EXPECT_EQ(downs, 1);
  EXPECT_EQ(overlay_sides, 1);
  EXPECT_EQ(flow_sides, 2);
  // up + north + south; the overlay side against glass stays
  // single-sided (vanilla addFace 330: addBackFace = !isOverlay).
  EXPECT_EQ(backs, 3);
}

TEST(TessellateTest, FluidOverlayMissingKeepsFlowWindow) {
  GlassWaterPackFixture fx(/*with_flow_textures=*/true);  // no overlay
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // Same layout as FluidSidesUseOverlayTextureAgainstHalfTransparentBlocks;
  // without water_overlay in the pack the glass-adjacent side falls
  // back to the flow texture with the identical window.
  MixedRegion sr({State("minecraft:air"),
                  State("minecraft:glass"),
                  State("minecraft:water"),
                  State("minecraft:stone")},
                 {1, 0, 0, 0},
                 {1, 2, 3},
                 3,
                 1,
                 1);
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  const Mesh& m = r->transparent_mesh;
  constexpr float kEps = 1e-4f;
  const float kTopV = (1.0f - 80.0f / 99.0f) * 0.5f;  // 19/198
  int west = 0;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    if (m.normals[q * 12 + 0] > -0.5f) continue;  // west faces only
    float ymax = -1e30f;
    for (int k = 0; k < 4; ++k) {
      ymax = std::max(ymax, m.positions[(q * 4 + k) * 3 + 1]);
    }
    if (ymax > 0.9f) continue;  // the glass block's own west face
    ++west;
    float u[4], v[4];
    std::size_t slot = 0;
    ASSERT_TRUE(FaceUvsOfQuad(m, r->atlas, q, u, v, &slot));
    const auto [sw, sh] = SlotSizePx(r->atlas, slot);
    EXPECT_NEAR(sw, 8.0f, 0.5f);  // flow texture, NOT a 12x12 overlay
    EXPECT_NEAR(sh, 8.0f, 0.5f);
    const float want[4][2] = {
        {0.5f, 0.5f}, {0.5f, kTopV}, {0.0f, kTopV}, {0.0f, 0.5f}};
    for (int k = 0; k < 4; ++k) {
      EXPECT_NEAR(u[k], want[k][0], kEps);
      EXPECT_NEAR(v[k], want[k][1], kEps);
    }
  }
  EXPECT_EQ(west, 1);  // the water's west face only
}

TEST(TessellateTest, FluidOverlayIsWaterOnlyAndSkipsPanes) {
  GlassWaterPackFixture fx(/*with_flow_textures=*/true,
                           /*with_overlay_texture=*/true);
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  constexpr float kEps = 1e-4f;
  const float kTopV = (1.0f - 80.0f / 99.0f) * 0.5f;  // 19/198
  const float want_west[4][2] = {
      {0.5f, 0.5f}, {0.5f, kTopV}, {0.0f, kTopV}, {0.0f, 0.5f}};

  // (a) Lava against glass: overlayMaterial is water-only in vanilla,
  // so the lava side keeps lava_flow.
  MixedRegion lr({State("minecraft:air"),
                  State("minecraft:glass"),
                  State("minecraft:lava"),
                  State("minecraft:stone")},
                 {1, 0, 0, 0},
                 {1, 2, 3},
                 3,
                 1,
                 1);
  auto l =
      TessellateRegion(lr.region, *lr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(l.has_value()) << ToString(l.error());
  // Lava renders in the OPAQUE group but, being a fluid, occludes
  // nothing (vanilla canOcclude is false for fluids): glass keeps its
  // east face against lava (6 faces, transparent mesh), lava loses
  // only its east face to the opaque stone (5 faces), stone keeps all
  // 6 (its west face against lava renders), and lava doubles its 5
  // faces except the down face (4 back faces, in the opaque mesh;
  // lava never has an overlay).
  ASSERT_EQ(l->opaque_mesh.VertexCount(), (5u + 6u + 4u) * 4u);
  ASSERT_EQ(l->transparent_mesh.VertexCount(), 6u * 4u);
  int lava_west = 0;
  int backs = 0;
  for (std::size_t q = 0; q < l->opaque_mesh.VertexCount() / 4; ++q) {
    if (IsBackQuad(l->opaque_mesh, q)) {
      ++backs;
      continue;
    }
    if (l->opaque_mesh.normals[q * 12 + 0] > -0.5f) continue;
    // The lava block sits at x = 1 (glass lives in the transparent
    // mesh), so its west face lies on the x = 1 plane; stone's west
    // face (now visible, since lava occludes nothing) sits at x = 2
    // and must not be counted here.
    if (std::abs(l->opaque_mesh.positions[(q * 4) * 3 + 0] - 1.0f) >
        kEps) {
      continue;
    }
    ++lava_west;
    float u[4], v[4];
    std::size_t slot = 0;
    ASSERT_TRUE(FaceUvsOfQuad(l->opaque_mesh, l->atlas, q, u, v, &slot));
    const auto [sw, sh] = SlotSizePx(l->atlas, slot);
    EXPECT_NEAR(sw, 8.0f, 0.5f);  // lava_flow, never the 12x12 overlay
    EXPECT_NEAR(sh, 8.0f, 0.5f);
    for (int k = 0; k < 4; ++k) {
      EXPECT_NEAR(u[k], want_west[k][0], kEps);
      EXPECT_NEAR(v[k], want_west[k][1], kEps);
    }
  }
  EXPECT_EQ(lava_west, 1);
  EXPECT_EQ(backs, 4);

  // (b) Water against a glass pane: IronBarsBlock is not
  // HalfTransparentBlock in vanilla, so the side keeps the flow
  // texture (while the pane still acts as the solid sentinel for the
  // corner average).
  MixedRegion pr({State("minecraft:air"),
                  State("minecraft:glass_pane"),
                  State("minecraft:water"),
                  State("minecraft:stone")},
                 {1, 0, 0, 0},
                 {1, 2, 3},
                 3,
                 1,
                 1);
  auto p =
      TessellateRegion(pr.region, *pr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(p.has_value()) << ToString(p.error());
  int pane_west = 0;
  for (std::size_t q = 0; q < p->transparent_mesh.VertexCount() / 4; ++q) {
    const Mesh& tm = p->transparent_mesh;
    if (tm.normals[q * 12 + 0] > -0.5f) continue;  // west faces only
    float ymax = -1e30f;
    for (int k = 0; k < 4; ++k) {
      ymax = std::max(ymax, tm.positions[(q * 4 + k) * 3 + 1]);
    }
    if (ymax > 0.9f) continue;  // the pane's own west face
    ++pane_west;
    float u[4], v[4];
    std::size_t slot = 0;
    ASSERT_TRUE(FaceUvsOfQuad(tm, p->atlas, q, u, v, &slot));
    const auto [sw, sh] = SlotSizePx(p->atlas, slot);
    EXPECT_NEAR(sw, 8.0f, 0.5f);  // flow, NOT the overlay
    EXPECT_NEAR(sh, 8.0f, 0.5f);
    for (int k = 0; k < 4; ++k) {
      EXPECT_NEAR(u[k], want_west[k][0], kEps);
      EXPECT_NEAR(v[k], want_west[k][1], kEps);  // 19/198: pane = sentinel
    }
  }
  EXPECT_EQ(pane_west, 1);
}

TEST(TessellateTest, SchematicOverlaysWaterAgainstGlass) {
  GlassWaterPackFixture fx(/*with_flow_textures=*/true,
                           /*with_overlay_texture=*/true);
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // Single-region schematic mirroring T1's layout: the schematic path
  // must produce the same overlay face (TessellateSchematic carries
  // its own per-block loop). Metadata fields mirror the construction
  // pattern of schematic_test.cc.
  fschema::ir::Schematic schematic{};
  schematic.source_format = fschema::ir::SourceFormat::kLitematica;
  schematic.source_version = 6;
  schematic.data_version = 3105;
  auto arena = std::make_unique<fschema::memory::Arena>();
  schematic.arena = std::move(arena);
  schematic.regions.resize(1);
  fschema::ir::Region& region = schematic.regions[0];
  region.bounds = fschema::ir::BoundingBox{{0, 0, 0}, {3, 1, 1}};
  region.palette = {State("minecraft:air"),
                    State("minecraft:glass"),
                    State("minecraft:water"),
                    State("minecraft:stone")};
  region.is_air_lut = {1, 0, 0, 0};
  region.block_indices =
      fschema::memory::UnInitBuffer<std::uint16_t>(3, schematic.arena.get());
  std::uint16_t* data =
      reinterpret_cast<std::uint16_t*>(region.block_indices.data());
  data[0] = 1;
  data[1] = 2;
  data[2] = 3;
  region.is_materialized = true;
  region.lazy_source.encoding = fschema::ir::BlockDataEncoding::kNone;

  auto r = TessellateSchematic(schematic, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  // Same as the region path: glass 6 + water 5 fronts + 3 back faces
  // (up, north, south; the overlay west stays single-sided). The
  // count also proves the schematic loop emits back faces at all.
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (6u + 5u + 3u) * 4u);

  const Mesh& m = r->transparent_mesh;
  constexpr float kEps = 1e-4f;
  const float kTopV = (1.0f - 80.0f / 99.0f) * 0.5f;
  int overlay_sides = 0;
  int backs = 0;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    if (IsBackQuad(m, q)) {
      ++backs;
      continue;
    }
    if (m.normals[q * 12 + 0] > -0.5f) continue;  // west faces only
    float ymax = -1e30f;
    for (int k = 0; k < 4; ++k) {
      ymax = std::max(ymax, m.positions[(q * 4 + k) * 3 + 1]);
    }
    if (ymax > 0.9f) continue;
    ++overlay_sides;
    float u[4], v[4];
    std::size_t slot = 0;
    ASSERT_TRUE(FaceUvsOfQuad(m, r->atlas, q, u, v, &slot));
    const auto [sw, sh] = SlotSizePx(r->atlas, slot);
    EXPECT_NEAR(sw, 12.0f, 0.5f);  // the overlay slot
    EXPECT_NEAR(sh, 12.0f, 0.5f);
    const float want[4][2] = {
        {0.5f, 0.5f}, {0.5f, kTopV}, {0.0f, kTopV}, {0.0f, 0.5f}};
    for (int k = 0; k < 4; ++k) {
      EXPECT_NEAR(u[k], want[k][0], kEps);
      EXPECT_NEAR(v[k], want[k][1], kEps);
    }
  }
  EXPECT_EQ(overlay_sides, 1);
  EXPECT_EQ(backs, 3);
}

TEST(TessellateTest, FluidCornersTreatSolidTransparentNeighborAsExcluded) {
  GlassWaterPackFixture fx(/*with_flow_textures=*/true,
                           /*with_overlay_texture=*/true);
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // 2x1x1: water | glass. The water's west half sees only out-of-
  // bounds air (corners 20/27); the east half sees the glass, which
  // isSolid() drops out of the average (corners 80/99) -- the
  // pre-patch rule would have pulled the glass side down to 20/27.
  // The east face swaps to the overlay at v = 19/198, the west face
  // keeps the flow window at 7/54, and north/south mix both.
  MixedRegion sr({State("minecraft:air"),
                  State("minecraft:water"),
                  State("minecraft:glass")},
                 {1, 0, 0},
                 {1, 2},
                 2,
                 1,
                 1);
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // Nothing culls the water here (glass is a different transparent
  // group), so the transparent mesh holds 6 glass + 6 water fronts +
  // 4 water back faces (up, west, north, south; the east overlay side
  // stays single-sided).
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (6u + 6u + 4u) * 4u);

  const Mesh& m = r->transparent_mesh;
  constexpr float kEps = 1e-4f;
  const float kAir = 20.0f / 27.0f;
  const float kSolid = 80.0f / 99.0f;
  const float kAirV = (1.0f - kAir) * 0.5f;      // 7/54
  const float kSolidV = (1.0f - kSolid) * 0.5f;  // 19/198
  int tops = 0, east = 0, west = 0;
  int backs = 0;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    if (IsBackQuad(m, q)) {
      ++backs;
      continue;
    }
    float ymax = -1e30f;
    for (int k = 0; k < 4; ++k) {
      ymax = std::max(ymax, m.positions[(q * 4 + k) * 3 + 1]);
    }
    if (ymax > 0.9f) continue;  // glass faces
    const float ny = m.normals[q * 12 + 1];
    const float nx = m.normals[q * 12 + 0];
    if (ny > 0.5f) {
      // Water top: NW, NE, SE, SW = [20/27, 80/99, 80/99, 20/27].
      ++tops;
      const float want_y[4] = {kAir, kSolid, kSolid, kAir};
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(m.positions[(q * 4 + k) * 3 + 1], want_y[k], kEps);
      }
    } else if (ny < -0.5f) {
      // Water bottom at y = 0.
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(m.positions[(q * 4 + k) * 3 + 1], 0.0f, kEps);
      }
    } else if (nx > 0.5f) {
      // East (against glass): overlay, both top corners 80/99.
      ++east;
      float u[4], v[4];
      std::size_t slot = 0;
      ASSERT_TRUE(FaceUvsOfQuad(m, r->atlas, q, u, v, &slot));
      const auto [sw, sh] = SlotSizePx(r->atlas, slot);
      EXPECT_NEAR(sw, 12.0f, 0.5f);
      EXPECT_NEAR(sh, 12.0f, 0.5f);
      const float want[4][2] = {
          {0.0f, 0.5f}, {0.5f, 0.5f}, {0.5f, kSolidV}, {0.0f, kSolidV}};
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], want[k][0], kEps);
        EXPECT_NEAR(v[k], want[k][1], kEps);
      }
    } else if (nx < -0.5f) {
      // West (out of bounds): flow, both top corners 20/27.
      ++west;
      float u[4], v[4];
      std::size_t slot = 0;
      ASSERT_TRUE(FaceUvsOfQuad(m, r->atlas, q, u, v, &slot));
      const auto [sw, sh] = SlotSizePx(r->atlas, slot);
      EXPECT_NEAR(sw, 8.0f, 0.5f);
      EXPECT_NEAR(sh, 8.0f, 0.5f);
      const float want[4][2] = {
          {0.5f, 0.5f}, {0.5f, kAirV}, {0.0f, kAirV}, {0.0f, 0.5f}};
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], want[k][0], kEps);
        EXPECT_NEAR(v[k], want[k][1], kEps);
      }
    }
  }
  EXPECT_EQ(tops, 1);
  EXPECT_EQ(east, 1);
  EXPECT_EQ(west, 1);
  EXPECT_EQ(backs, 4);
}

TEST(TessellateTest, FluidFacesEmitBackFaces) {
  GlassWaterPackFixture fx(/*with_flow_textures=*/true);
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  MixedRegion wr(
      {State("minecraft:air"), State("minecraft:water")}, {1, 0}, {1}, 1, 1, 1);
  auto r =
      TessellateRegion(wr.region, *wr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // 6 fronts + 5 back faces (up + 4 sides); the down face never doubles.
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (6u + 5u) * 4u);

  const Mesh& m = r->transparent_mesh;
  const float kDip = 20.0f / 27.0f;
  std::size_t up_front = SIZE_MAX;
  std::size_t west_front = SIZE_MAX;
  int backs = 0;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    if (IsBackQuad(m, q)) {
      ++backs;
      continue;
    }
    if (m.normals[q * 12 + 1] > 0.5f) up_front = q;
    if (m.normals[q * 12 + 0] < -0.5f) west_front = q;
  }
  ASSERT_NE(up_front, SIZE_MAX);
  ASSERT_NE(west_front, SIZE_MAX);
  EXPECT_EQ(backs, 5);

  // The up face's back copy: same four corners and uvs in reverse
  // order (3, 2, 1, 0), with the DOWN normal (the visible side faces
  // into the fluid). Locate it by geometry + normal.
  std::size_t up_back = SIZE_MAX;
  std::size_t west_back = SIZE_MAX;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    if (!IsBackQuad(m, q)) continue;
    const float ny = m.normals[q * 12 + 1];
    const float nx = m.normals[q * 12 + 0];
    bool at_surface = true;
    for (int k = 0; k < 4; ++k) {
      if (std::abs(m.positions[(q * 4 + k) * 3 + 1] - kDip) > 1e-4f) {
        at_surface = false;
      }
    }
    if (ny < -0.5f && at_surface) up_back = q;
    if (nx > 0.5f) west_back = q;
  }
  ASSERT_NE(up_back, SIZE_MAX);
  ASSERT_NE(west_back, SIZE_MAX);

  for (int k = 0; k < 4; ++k) {
    for (int c = 0; c < 3; ++c) {
      EXPECT_EQ(m.positions[(up_back * 4 + k) * 3 + c],
                m.positions[(up_front * 4 + (3 - k)) * 3 + c]);
    }
    for (int c = 0; c < 2; ++c) {
      EXPECT_EQ(m.uvs[(up_back * 4 + k) * 2 + c],
                m.uvs[(up_front * 4 + (3 - k)) * 2 + c]);
    }
    EXPECT_EQ(m.ao[up_back * 4 + k], m.ao[up_front * 4 + (3 - k)]);
  }
  // The west side's back copy carries the EAST normal.
  EXPECT_GT(m.normals[west_back * 12 + 0], 0.5f);
  for (int k = 0; k < 4; ++k) {
    for (int c = 0; c < 3; ++c) {
      EXPECT_EQ(m.positions[(west_back * 4 + k) * 3 + c],
                m.positions[(west_front * 4 + (3 - k)) * 3 + c]);
    }
  }
}

TEST(TessellateTest, LeavesRenderTransparentWithoutSelfCull) {
  GlassWaterPackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  MixedRegion lr({State("minecraft:air"),
                  State("minecraft:oak_leaves"),
                  State("minecraft:birch_leaves")},
                 {1, 0, 0},
                 {1, 2},
                 2,
                 1,
                 1);
  auto r =
      TessellateRegion(lr.region, *lr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // Fancy-graphics leaves: nothing culls (different leaf types share
  // the non-self-culling group, and even same-type pairs would not
  // cull), and every face lands in the transparent mesh.
  ASSERT_EQ(r->opaque_mesh.VertexCount(), 0u);
  ASSERT_EQ(r->transparent_mesh.VertexCount(), 12u * 4u);

  bool saw_oak_tint = false, saw_birch_tint = false;
  for (std::uint8_t t : r->transparent_mesh.tints) {
    if (t == 2) saw_oak_tint = true;    // biome foliage colormap
    if (t == 5) saw_birch_tint = true;  // fixed birch color
  }
  EXPECT_TRUE(saw_oak_tint);
  EXPECT_TRUE(saw_birch_tint);
}

TEST(TessellateTest, LeavesDoNotCullNeighborFaces) {
  GlassWaterPackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  MixedRegion sr({State("minecraft:air"),
                  State("minecraft:stone"),
                  State("minecraft:oak_leaves")},
                 {1, 0, 0},
                 {1, 2},
                 2,
                 1,
                 1);
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // Stone keeps all 6 faces (leaves do not occlude); leaves keep 5
  // (the opaque stone still hides their shared face). Pre-patch this
  // was 10 quads all in the opaque mesh.
  ASSERT_EQ(r->opaque_mesh.VertexCount(), 6u * 4u);
  ASSERT_EQ(r->transparent_mesh.VertexCount(), 5u * 4u);
}

TEST(TessellateTest, FluidSidesUseOverlayTextureAgainstLeaves) {
  GlassWaterPackFixture fx(/*with_flow_textures=*/true,
                           /*with_overlay_texture=*/true);
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  MixedRegion sr({State("minecraft:air"),
                  State("minecraft:oak_leaves"),
                  State("minecraft:water"),
                  State("minecraft:stone")},
                 {1, 0, 0, 0},
                 {1, 2, 3},
                 3,
                 1,
                 1);
  auto r =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // Leaves keep all 6 faces (water does not cull them either); water
  // keeps 5 fronts + 3 back faces (the east face culls against the
  // stone, the overlay west side stays single-sided).
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (6u + 5u + 3u) * 4u);
  ASSERT_EQ(r->opaque_mesh.VertexCount(), 6u * 4u);

  const Mesh& m = r->transparent_mesh;
  constexpr float kEps = 1e-4f;
  const float kTopV = (1.0f - 80.0f / 99.0f) * 0.5f;  // 19/198
  int leaves = 0, overlay_sides = 0, backs = 0;
  for (std::size_t q = 0; q < m.VertexCount() / 4; ++q) {
    if (IsBackQuad(m, q)) {
      ++backs;
      continue;
    }
    float xmin = 1e30f, xmax = -1e30f;
    for (int k = 0; k < 4; ++k) {
      const float px = m.positions[(q * 4 + k) * 3 + 0];
      xmin = std::min(xmin, px);
      xmax = std::max(xmax, px);
    }
    const float nx = m.normals[q * 12 + 0];
    const bool is_water_west = nx < -0.5f && xmin > 1.0f - kEps;
    if (xmax < 1.5f && !is_water_west) {
      ++leaves;
      continue;
    }
    if (nx < -0.5f) {  // water west side
      ++overlay_sides;
      float u[4], v[4];
      std::size_t slot = 0;
      ASSERT_TRUE(FaceUvsOfQuad(m, r->atlas, q, u, v, &slot));
      const auto [sw, sh] = SlotSizePx(r->atlas, slot);
      EXPECT_NEAR(sw, 12.0f, 0.5f);  // the overlay slot, not the flow
      EXPECT_NEAR(sh, 12.0f, 0.5f);
      const float want[4][2] = {
          {0.5f, 0.5f}, {0.5f, kTopV}, {0.0f, kTopV}, {0.0f, 0.5f}};
      for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(u[k], want[k][0], kEps);
        EXPECT_NEAR(v[k], want[k][1], kEps);
      }
    }
  }
  EXPECT_EQ(leaves, 6);
  EXPECT_EQ(overlay_sides, 1);
  EXPECT_EQ(backs, 3);
}

// glTF front-face convention on the fluid paths: front quads, their
// back-face copies (corners reversed, cull face flipped), and the
// flow-texture repaints must all wind so that
// cross(v1-v0, v2-v0) . normal > 0 for every emitted triangle;
// backface-culling consumers show fluids inside-out otherwise.
TEST(TessellateTest, FluidQuadWindingMatchesNormals) {
  GlassWaterPackFixture fx(/*with_flow_textures=*/true);
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  MixedRegion wr({State("minecraft:air"),
                  State("minecraft:water"),
                  State("minecraft:stone")},
                 {1, 0, 0},
                 {1, 0, 0},
                 1,
                 1,
                 1);
  auto r =
      TessellateRegion(wr.region, *wr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  // 6 front faces + 5 back faces (up + 4 sides), all in the
  // transparent mesh.
  ASSERT_EQ(r->transparent_mesh.VertexCount(), (6u + 5u) * 4u);
  const Mesh& m = r->transparent_mesh;
  for (std::size_t t = 0; t < m.indices.size() / 3; ++t) {
    const std::uint32_t i0 = m.indices[t * 3 + 0];
    const std::uint32_t i1 = m.indices[t * 3 + 1];
    const std::uint32_t i2 = m.indices[t * 3 + 2];
    const float ax = m.positions[i1 * 3 + 0] - m.positions[i0 * 3 + 0];
    const float ay = m.positions[i1 * 3 + 1] - m.positions[i0 * 3 + 1];
    const float az = m.positions[i1 * 3 + 2] - m.positions[i0 * 3 + 2];
    const float bx = m.positions[i2 * 3 + 0] - m.positions[i0 * 3 + 0];
    const float by = m.positions[i2 * 3 + 1] - m.positions[i0 * 3 + 1];
    const float bz = m.positions[i2 * 3 + 2] - m.positions[i0 * 3 + 2];
    const float gx = ay * bz - az * by;
    const float gy = az * bx - ax * bz;
    const float gz = ax * by - ay * bx;
    const float dot = gx * m.normals[i0 * 3 + 0] + gy * m.normals[i0 * 3 + 1] +
                      gz * m.normals[i0 * 3 + 2];
    EXPECT_GT(dot, 0.0f) << "fluid triangle " << t
                          << " winds against its normal (dot=" << dot << ")";
  }
}

}  // namespace
