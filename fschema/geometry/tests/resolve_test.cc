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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/geometry/resource_pack.h"
#include "fschema/geometry/tests/geometry_test_util.h"
#include "fschema/geometry/tests/resolve_test_util.h"

namespace {

using fschema::ParseError;
using fschema::ToString;
using fschema::geometry::ResourcePack;
using fschema::tests::EmptyProps;
using fschema::tests::Props;
using fschema::tests::ResolveFixture;
using fschema::tests::StairsProps;

TEST(ResolveTest, StoneResolvesToSixFaces) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:stone", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());

  EXPECT_EQ(r->faces.size(), 6u);
  for (const auto& f : r->faces) {
    EXPECT_EQ(f.TintSlot(), 0u);
    EXPECT_FALSE(f.IsTransparent());
    EXPECT_NE(f.CullFace(), 0u) << "every cube face should cull on its axis";
  }
}

TEST(ResolveTest, DirtResolvesToSixFaces) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:dirt", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  EXPECT_EQ(r->faces.size(), 6u);
}

TEST(ResolveTest, OakStairsMatchesVariantKey) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:oak_stairs", StairsProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  EXPECT_EQ(r->faces.size(), 6u);
}

TEST(ResolveTest, OakStairsRejectsNonMatchingProps) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:oak_stairs", EmptyProps());
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kUnknownBlockState);
}

TEST(ResolveTest, MissingBlockstateYieldsUnknownBlockState) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:does_not_exist", EmptyProps());
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kMalformedBlockstateJson);
}

TEST(ResolveTest, MissingModelYieldsMalformedModelJson) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:missing_model", EmptyProps());
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kMalformedModelJson);
}

TEST(ResolveTest, ChildTextureOverridesParent) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:tex_child", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 1u);
  EXPECT_EQ(r->faces[0].texture_path, "block/child_side");
}

TEST(ResolveTest, TextureReferenceChainResolves) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:chain_leaf", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 1u);
  EXPECT_EQ(r->faces[0].texture_path, "block/leaf_inner");
}

TEST(ResolveTest, MissingParentModelFailsResolve) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:orphan_child", EmptyProps());
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kMalformedModelJson);
}

TEST(ResolveTest, SpriteObjectTextureFormResolves) {
  // Newer vanilla wraps texture paths in an object and names the path
  // in its "sprite" field; the map must store the sprite, not nothing.
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:sprite_form", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 6u);
  for (const auto& f : r->faces) {
    EXPECT_EQ(f.texture_path, "block/glass");
  }
}

TEST(ResolveTest, BareTextureNameResolvesThroughMap) {
  // heavy_core.json style: face "texture" is a variable name without
  // the "#"; it must resolve through the textures map.
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:bare_name", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 1u);
  EXPECT_EQ(r->faces[0].texture_path, "block/heavy_core");
}

TEST(ResolveTest, TintIndexFaceGetsBlockTintSlot) {
  // A face is tinted only when its model declares "tintindex"; the
  // colormap is chosen by block name (oak_leaves -> biome foliage).
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:oak_leaves", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 2u);

  std::array<int, 7> slots{};
  for (const auto& f : r->faces) ++slots[f.TintSlot()];
  EXPECT_EQ(slots[0], 1) << "face without tintindex stays untinted";
  EXPECT_EQ(slots[2], 1) << "tintindex face gets the foliage slot";
}

TEST(ResolveTest, GrassBlockTintsTopButNotBottom) {
  // vanilla grass_block: only the top (and side overlay) faces carry
  // tintindex; the dirt bottom must stay untinted.
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:grass_block", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 2u);

  std::array<int, 7> slots{};
  for (const auto& f : r->faces) ++slots[f.TintSlot()];
  EXPECT_EQ(slots[0], 1) << "dirt bottom stays untinted";
  EXPECT_EQ(slots[1], 1) << "grass top gets the grass slot";
}

TEST(ResolveTest, BirchAndCherryLeavesTintSlots) {
  // Birch uses a fixed-color slot; cherry has no vanilla color handler,
  // so its tintindex faces render untinted.
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto birch = (*pack)->Resolve("minecraft:birch_leaves", EmptyProps());
  ASSERT_TRUE(birch.has_value()) << ToString(birch.error());
  ASSERT_EQ(birch->faces.size(), 2u);
  std::array<int, 7> birch_slots{};
  for (const auto& f : birch->faces) ++birch_slots[f.TintSlot()];
  EXPECT_EQ(birch_slots[0], 1);
  EXPECT_EQ(birch_slots[5], 1) << "birch gets the fixed-color slot";

  auto cherry = (*pack)->Resolve("minecraft:cherry_leaves", EmptyProps());
  ASSERT_TRUE(cherry.has_value()) << ToString(cherry.error());
  ASSERT_EQ(cherry->faces.size(), 2u);
  for (const auto& f : cherry->faces) {
    EXPECT_EQ(f.TintSlot(), 0u) << "unknown tinted block stays untinted";
  }
}

TEST(ResolveTest, MultipartOrConditionMatchesEitherBranch) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // OR first sub-condition: powered=true.
  auto a = (*pack)->Resolve("minecraft:or_gate", Props("powered=true"));
  ASSERT_TRUE(a.has_value()) << ToString(a.error());
  ASSERT_EQ(a->faces.size(), 1u);
  EXPECT_EQ(a->faces[0].texture_path, "block/gate_a_tex");

  // OR second sub-condition: facing=north (powered=false) also matches
  // the plain case, so both models apply.
  auto ab = (*pack)->Resolve("minecraft:or_gate",
                             Props("facing=north,powered=false"));
  ASSERT_TRUE(ab.has_value()) << ToString(ab.error());
  ASSERT_EQ(ab->faces.size(), 2u);

  // Neither OR branch: only the plain powered=false case applies.
  auto b = (*pack)->Resolve("minecraft:or_gate",
                            Props("facing=east,powered=false"));
  ASSERT_TRUE(b.has_value()) << ToString(b.error());
  ASSERT_EQ(b->faces.size(), 1u);
  EXPECT_EQ(b->faces[0].texture_path, "block/gate_b_tex");
}

TEST(ResolveTest, MultipartAndConditionRequiresAllSubconditions) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto ab = (*pack)->Resolve("minecraft:and_gate",
                             Props("facing=north,powered=true"));
  ASSERT_TRUE(ab.has_value()) << ToString(ab.error());
  ASSERT_EQ(ab->faces.size(), 2u);  // AND case + plain powered=true case

  auto b = (*pack)->Resolve("minecraft:and_gate",
                            Props("facing=east,powered=true"));
  ASSERT_TRUE(b.has_value()) << ToString(b.error());
  ASSERT_EQ(b->faces.size(), 1u);  // only the plain case
  EXPECT_EQ(b->faces[0].texture_path, "block/gate_b_tex");

  auto none = (*pack)->Resolve("minecraft:and_gate", Props("powered=false"));
  ASSERT_FALSE(none.has_value());
  EXPECT_EQ(none.error().code, ParseError::Code::kUnknownBlockState);
}

TEST(ResolveTest, MultipartPipeValuesMatchAnyAlternative) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  for (std::string_view state : {"side", "up"}) {
    auto a = (*pack)->Resolve("minecraft:pipe_gate",
                             Props("state=" + std::string(state)));
    ASSERT_TRUE(a.has_value()) << ToString(a.error()) << " state=" << state;
    ASSERT_EQ(a->faces.size(), 1u);
    EXPECT_EQ(a->faces[0].texture_path, "block/gate_a_tex");
  }

  auto b = (*pack)->Resolve("minecraft:pipe_gate", Props("state=none"));
  ASSERT_TRUE(b.has_value()) << ToString(b.error());
  ASSERT_EQ(b->faces.size(), 1u);
  EXPECT_EQ(b->faces[0].texture_path, "block/gate_b_tex");

  auto none = (*pack)->Resolve("minecraft:pipe_gate", Props("state=down"));
  ASSERT_FALSE(none.has_value());
  EXPECT_EQ(none.error().code, ParseError::Code::kUnknownBlockState);
}

TEST(ResolveTest, MultipartValueArrayMatchesAnyEntry) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  for (std::string_view facing : {"north", "south"}) {
    auto a = (*pack)->Resolve("minecraft:array_gate",
                             Props("facing=" + std::string(facing)));
    ASSERT_TRUE(a.has_value()) << ToString(a.error()) << " facing=" << facing;
    ASSERT_EQ(a->faces.size(), 1u);
    EXPECT_EQ(a->faces[0].texture_path, "block/gate_a_tex");
  }

  auto none = (*pack)->Resolve("minecraft:array_gate", Props("facing=east"));
  ASSERT_FALSE(none.has_value());
  EXPECT_EQ(none.error().code, ParseError::Code::kUnknownBlockState);
}

TEST(ResolveTest, MultipartApplyArrayTakesFirstModel) {
  // An apply array is a weighted random pick in vanilla; the resolver
  // takes the first entry, mirroring the variants form.
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:first_pick", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 1u);
  EXPECT_EQ(r->faces[0].texture_path, "block/gate_a_tex");
}

TEST(ResolveTest, GlassBlockFacesAreTransparent) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:glass", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 6u);
  EXPECT_TRUE(r->is_transparent);
  for (const auto& f : r->faces) {
    EXPECT_TRUE(f.IsTransparent()) << "glass faces render blended";
  }
}

TEST(ResolveTest, ModelRenderTypeMarksAndOverridesTransparency) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // A translucent render_type flags every face the model returns.
  auto t = (*pack)->Resolve("minecraft:rt_block", EmptyProps());
  ASSERT_TRUE(t.has_value()) << ToString(t.error());
  ASSERT_EQ(t->faces.size(), 6u);
  EXPECT_TRUE(t->is_transparent);
  for (const auto& f : t->faces) EXPECT_TRUE(f.IsTransparent());

  // A child's solid render_type replaces the parent's translucent one.
  auto s = (*pack)->Resolve("minecraft:rt_solid", EmptyProps());
  ASSERT_TRUE(s.has_value()) << ToString(s.error());
  ASSERT_EQ(s->faces.size(), 6u);
  EXPECT_FALSE(s->is_transparent);
  for (const auto& f : s->faces) EXPECT_FALSE(f.IsTransparent());
}

TEST(ResolveTest, WaterSynthesizesTransparentTintedCube) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:water", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 6u);
  EXPECT_TRUE(r->is_transparent);
  for (const auto& f : r->faces) {
    EXPECT_EQ(f.texture_path, "minecraft:block/water_still");
    EXPECT_EQ(f.TintSlot(), 3u);  // water colormap
    EXPECT_TRUE(f.IsTransparent());
    EXPECT_NE(f.CullFace(), 0u);
  }

  // Source block: flat top at 8/9 of the block height.
  constexpr float kEps = 1e-4f;
  const float kTop = 8.0f / 9.0f;
  bool saw_top = false;
  for (const auto& f : r->faces) {
    if (f.CullFace() != 3u) continue;  // FaceDir::kPosY
    saw_top = true;
    for (const auto& c : f.corners) EXPECT_NEAR(c[1], kTop, kEps);
  }
  EXPECT_TRUE(saw_top) << "synthesized water must have an up face";

  // Flowing level=3: height (8-3)/9.
  auto lvl = (*pack)->Resolve("minecraft:water", Props("level=3"));
  ASSERT_TRUE(lvl.has_value()) << ToString(lvl.error());
  const float kFlow = 5.0f / 9.0f;
  for (const auto& f : lvl->faces) {
    if (f.CullFace() != 3u) continue;
    for (const auto& c : f.corners) EXPECT_NEAR(c[1], kFlow, kEps);
  }
}

TEST(ResolveTest, FluidFallingLevelsUseLegacyAmountMapping) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // FlowingFluid#getLegacyLevel inverted: falling level L (8..15)
  // encodes amount 16 - L, so the isolated-cell own height is
  // (16 - L)/9, not 1.0. Full 1.0 is only reached through the "same
  // fluid above" rule, applied per corner at tessellate time.
  constexpr float kEps = 1e-4f;
  const std::pair<const char*, float> cases[] = {
      {"level=8", 8.0f / 9.0f},
      {"level=11", 5.0f / 9.0f},
      {"level=15", 1.0f / 9.0f},
  };
  for (const auto& [kv, want] : cases) {
    auto r = (*pack)->Resolve("minecraft:water", Props(kv));
    ASSERT_TRUE(r.has_value()) << ToString(r.error());
    bool saw_top = false;
    for (const auto& f : r->faces) {
      if (f.CullFace() != 3u) continue;  // FaceDir::kPosY
      saw_top = true;
      for (const auto& c : f.corners) EXPECT_NEAR(c[1], want, kEps);
    }
    EXPECT_TRUE(saw_top) << kv << " must have an up face";
  }
}

TEST(ResolveTest, SynthesizedFluidFacesCarryFluidFlag) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto w = (*pack)->Resolve("minecraft:water", EmptyProps());
  ASSERT_TRUE(w.has_value()) << ToString(w.error());
  ASSERT_EQ(w->faces.size(), 6u);
  for (const auto& f : w->faces) EXPECT_TRUE(f.IsFluidFace());

  auto s = (*pack)->Resolve("minecraft:stone", EmptyProps());
  ASSERT_TRUE(s.has_value()) << ToString(s.error());
  for (const auto& f : s->faces) EXPECT_FALSE(f.IsFluidFace());
}

TEST(ResolveTest, AnimatedTextureCropsToFirstFrame) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  // A 16x48 strip of three 16x16 frames: red, green, blue.
  std::vector<std::uint8_t> rgba(16 * 48 * 4);
  for (std::size_t frame = 0; frame < 3; ++frame) {
    for (std::size_t i = 0; i < 16 * 16; ++i) {
      std::uint8_t* px =
          rgba.data() + (frame * 16 * 16 + i) * 4;
      px[0] = static_cast<std::uint8_t>(frame == 0 ? 0xFF : 0x00);
      px[1] = static_cast<std::uint8_t>(frame == 1 ? 0xFF : 0x00);
      px[2] = static_cast<std::uint8_t>(frame == 2 ? 0xFF : 0x00);
      px[3] = 0xFF;
    }
  }
  fschema::tests::WritePngFile(
      fx.root.Get() / "assets" / "minecraft" / "textures" / "block" /
          "strip.png",
      16, 48, rgba.data());

  auto tex = (*pack)->LoadTexturePixels("minecraft:block/strip");
  ASSERT_TRUE(tex.has_value()) << ToString(tex.error());
  EXPECT_EQ(tex->width, 16u);
  EXPECT_EQ(tex->height, 16u);  // first frame, not the whole strip
  ASSERT_EQ(tex->rgba.size(), 16u * 16u * 4u);
  EXPECT_EQ(tex->rgba[0], 0xFF);   // red survives
  EXPECT_EQ(tex->rgba[1], 0x00);
  EXPECT_EQ(tex->rgba[2], 0x00);
}

TEST(ResolveTest, L2CacheReturnsSameSpanOnRepeatedCalls) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r1 = (*pack)->Resolve("minecraft:stone", EmptyProps());
  ASSERT_TRUE(r1.has_value());
  auto r2 = (*pack)->Resolve("minecraft:stone", EmptyProps());
  ASSERT_TRUE(r2.has_value());

  EXPECT_EQ(r1->faces.data(), r2->faces.data());
  EXPECT_EQ(r1->faces.size(), r2->faces.size());
}

TEST(ResolveTest, FaceCornerOrderingIsCcwFromOutside) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:stone", EmptyProps());
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->faces.size(), 6u);

  std::array<int, 7> counts{};
  for (const auto& f : r->faces) {
    ASSERT_GE(f.CullFace(), 1u);
    ASSERT_LE(f.CullFace(), 6u);
    counts[f.CullFace()]++;
  }
  for (int i = 1; i <= 6; ++i) {
    EXPECT_EQ(counts[i], 1) << "cull_face index " << i << " should appear once";
  }
}

TEST(ResolveTest, FaceUVsAreNormalizedTo01) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:stone", EmptyProps());
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->faces.size(), 6u);

  for (const auto& f : r->faces) {
    for (const auto& uv : f.uvs) {
      EXPECT_GE(uv[0], 0.0f);
      EXPECT_LE(uv[0], 1.0f);
      EXPECT_GE(uv[1], 0.0f);
      EXPECT_LE(uv[1], 1.0f);
    }
  }
}

TEST(ResolveTest, CubeFaceCornersAreInside01Block) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:stone", EmptyProps());
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->faces.size(), 6u);

  for (const auto& f : r->faces) {
    for (const auto& c : f.corners) {
      EXPECT_GE(c[0], 0.0f);
      EXPECT_LE(c[0], 1.0f);
      EXPECT_GE(c[1], 0.0f);
      EXPECT_LE(c[1], 1.0f);
      EXPECT_GE(c[2], 0.0f);
      EXPECT_LE(c[2], 1.0f);
    }
  }
}

// A face that omits "uv" derives its rect from the element's bounds,
// exactly vanilla FaceBakery.defaultFaceUV: north/east mirror their
// axes (1 - coord in block units), sides stand the image upright, up
// is a north-up map and down reads with south up. Bounds from [4,6,8]
// to [12,14,15] make every corner value distinguishable.
TEST(ResolveTest, ElementFaceUvDefaultsFollowVanillaBounds) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:auto_uv", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 6u);

  for (const ModelFace& f : r->faces) {
    const float (*want)[2] = nullptr;
    switch (f.CullFace()) {
      case 6: {  // north: bW, bE, tE, tW
        static const float w[4][2] = {
            {0.75f, 0.625f}, {0.25f, 0.625f}, {0.25f, 0.125f}, {0.75f, 0.125f}};
        want = w;
        break;
      }
      case 5: {  // south: bW, tW, tE, bE
        static const float w[4][2] = {
            {0.25f, 0.625f}, {0.25f, 0.125f}, {0.75f, 0.125f}, {0.75f, 0.625f}};
        want = w;
        break;
      }
      case 1: {  // east: bN, bS, tS, tN
        static const float w[4][2] = {{0.5f, 0.625f},
                                      {0.0625f, 0.625f},
                                      {0.0625f, 0.125f},
                                      {0.5f, 0.125f}};
        want = w;
        break;
      }
      case 2: {  // west: bN, tN, tS, bS
        static const float w[4][2] = {{0.5f, 0.625f},
                                      {0.5f, 0.125f},
                                      {0.9375f, 0.125f},
                                      {0.9375f, 0.625f}};
        want = w;
        break;
      }
      case 3: {  // up: NW, NE, SE, SW
        static const float w[4][2] = {
            {0.25f, 0.5f}, {0.75f, 0.5f}, {0.75f, 0.9375f}, {0.25f, 0.9375f}};
        want = w;
        break;
      }
      default: {  // down (4): NW, SW, SE, NE
        static const float w[4][2] = {
            {0.25f, 0.5f}, {0.25f, 0.0625f}, {0.75f, 0.0625f}, {0.75f, 0.5f}};
        want = w;
        break;
      }
    }
    for (int k = 0; k < 4; ++k) {
      EXPECT_NEAR(f.uvs[k][0], want[k][0], 1e-5f);
      EXPECT_NEAR(f.uvs[k][1], want[k][1], 1e-5f);
    }
  }
}

}  // namespace
