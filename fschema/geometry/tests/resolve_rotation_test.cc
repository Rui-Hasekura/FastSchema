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

#include "fschema/base/error.h"
#include "fschema/geometry/resource_pack.h"
#include "fschema/geometry/tests/resolve_test_util.h"
#include "fschema/geometry/types.h"

namespace {

using fschema::ToString;
using fschema::geometry::ModelFace;
using fschema::geometry::ResourcePack;
using fschema::tests::EmptyProps;
using fschema::tests::Props;
using fschema::tests::ResolveFixture;

// The 45-degree north face of rot_slab: corner 0 of kNegZ is
// (0,0,0), which rotates about (0.5,0.5,0.5) to
// (-0.20711, 0, 0.5); a rotated face drops its cull face.
TEST(ResolveTest, ElementRotationAppliesAngleAndClearsCullFace) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:rot_slab", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 1u);

  const ModelFace& f = r->faces[0];
  EXPECT_EQ(f.CullFace(), 0u);
  EXPECT_NEAR(f.corners[0][0], -0.207107f, 1e-4f);
  EXPECT_NEAR(f.corners[0][1], 0.0f, 1e-6f);
  EXPECT_NEAR(f.corners[0][2], 0.5f, 1e-6f);
  EXPECT_NEAR(f.corners[1][0], 0.5f, 1e-4f);
  EXPECT_NEAR(f.corners[1][2], -0.207107f, 1e-4f);
  EXPECT_NEAR(f.corners[2][0], 0.5f, 1e-4f);
  EXPECT_NEAR(f.corners[2][1], 1.0f, 1e-6f);
  EXPECT_NEAR(f.corners[2][2], -0.207107f, 1e-4f);
}

// Angles that are not multiples of 22.5 are legal since 1.21.6:
// corner (0,0,0) under a 30-degree Y rotation lands at
// (-0.18301, 0, 0.31699).
TEST(ResolveTest, ElementRotationArbitraryAngle) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:rot_slab30", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 1u);

  const ModelFace& f = r->faces[0];
  EXPECT_EQ(f.CullFace(), 0u);
  EXPECT_NEAR(f.corners[0][0], -0.183013f, 1e-4f);
  EXPECT_NEAR(f.corners[0][1], 0.0f, 1e-6f);
  EXPECT_NEAR(f.corners[0][2], 0.316987f, 1e-4f);
}

// A zero angle is the identity: corners and cull face stay put.
TEST(ResolveTest, ElementRotationZeroAngleKeepsCullFace) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:rot_zero", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 1u);

  const ModelFace& f = r->faces[0];
  EXPECT_EQ(f.CullFace(), 6u);  // north face keeps its axis cull
  EXPECT_EQ(f.corners[0][0], 0.0f);
  EXPECT_EQ(f.corners[0][1], 0.0f);
  EXPECT_EQ(f.corners[0][2], 0.0f);
  EXPECT_EQ(f.corners[2][0], 1.0f);
  EXPECT_EQ(f.corners[2][1], 1.0f);
  EXPECT_EQ(f.corners[2][2], 0.0f);
}

// rescale multiplies the perpendicular axes by 1/cos(angle), so the
// rotated 0.8..15.2-pixel strip spans the full block diagonal
// [0.05, 0.95] in-plane.
TEST(ResolveTest, ElementRotationRescaleSpansDiagonal) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:rot_cross", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 2u);

  bool has_low = false;
  bool has_high = false;
  for (const ModelFace& f : r->faces) {
    EXPECT_EQ(f.CullFace(), 0u);
    for (const auto& c : f.corners) {
      if (std::abs(c[0] - 0.05f) < 1e-4f && c[1] < 1e-4f &&
          std::abs(c[2] - 0.95f) < 1e-4f) {
        has_low = true;
      }
      if (std::abs(c[0] - 0.95f) < 1e-4f && std::abs(c[1] - 1.0f) < 1e-4f &&
          std::abs(c[2] - 0.05f) < 1e-4f) {
        has_high = true;
      }
    }
  }
  EXPECT_TRUE(has_low);
  EXPECT_TRUE(has_high);
}

// rescale at 22.5 degrees: vanilla's per-axis factor
// 1 / max(|cos|, |sin|) = 1/cos(22.5deg) = 1.08239 keeps the
// strip's original x extent (corners stay at 0.05/0.95) while the
// in-plane rotation swings z out by 0.45 * tan(22.5deg) = 0.18640.
TEST(ResolveTest, ElementRotationRescaleAtHalfQuarterTurn) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:rot_cross22", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 2u);

  const ModelFace& f = r->faces[0];
  EXPECT_EQ(f.CullFace(), 0u);
  EXPECT_NEAR(f.corners[0][0], 0.05f, 1e-4f);
  EXPECT_NEAR(f.corners[0][1], 0.0f, 1e-6f);
  EXPECT_NEAR(f.corners[0][2], 0.6863961f, 1e-4f);
  EXPECT_NEAR(f.corners[1][0], 0.95f, 1e-4f);
  EXPECT_NEAR(f.corners[1][2], 0.3136039f, 1e-4f);
  EXPECT_NEAR(f.corners[2][0], 0.95f, 1e-4f);
  EXPECT_NEAR(f.corners[2][1], 1.0f, 1e-6f);
  EXPECT_NEAR(f.corners[2][2], 0.3136039f, 1e-4f);
}

// Arbitrary angles are legal since 1.21.6, and rescale follows:
// at 30 degrees the stretch is 1/cos(30deg) = 1.15470, the z
// offset 0.45 * tan(30deg) = 0.25981.
TEST(ResolveTest, ElementRotationRescaleAtArbitraryAngle) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:rot_cross30", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 2u);

  const ModelFace& f = r->faces[0];
  EXPECT_EQ(f.CullFace(), 0u);
  EXPECT_NEAR(f.corners[0][0], 0.05f, 1e-4f);
  EXPECT_NEAR(f.corners[0][2], 0.7598076f, 1e-4f);
  EXPECT_NEAR(f.corners[1][0], 0.95f, 1e-4f);
  EXPECT_NEAR(f.corners[1][2], 0.2401924f, 1e-4f);
  EXPECT_NEAR(f.corners[2][1], 1.0f, 1e-6f);
  EXPECT_NEAR(f.corners[2][2], 0.2401924f, 1e-4f);
}

// "y": 90 rotates corners about the block center ((x,z) ->
// (1-z,x)). The fixture's 45-degree element rotation already cleared
// the cull face, so the variant turn acts on corners only; the cull
// remap itself is covered by VariantRotationRemapsCullFace below.
TEST(ResolveTest, VariantRotationYRemapsCornersAndCull) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:rot_turn", Props("facing=south"));
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 1u);

  const ModelFace& f = r->faces[0];
  EXPECT_EQ(f.CullFace(), 0u);  // element rotation cleared it
  EXPECT_NEAR(f.corners[0][0], 0.5f, 1e-4f);
  EXPECT_NEAR(f.corners[0][1], 0.0f, 1e-6f);
  EXPECT_NEAR(f.corners[0][2], -0.207107f, 1e-4f);
}

// "x": 180 flips the model upside down ((y,z) -> (1-y,1-z)). The
// element rotation cleared the cull face first, so only corners move.
TEST(ResolveTest, VariantRotationX180FlipsUpsideDown) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:rot_turn", Props("facing=up"));
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 1u);

  const ModelFace& f = r->faces[0];
  EXPECT_EQ(f.CullFace(), 0u);  // element rotation cleared it
  EXPECT_NEAR(f.corners[0][0], -0.207107f, 1e-4f);
  EXPECT_NEAR(f.corners[0][1], 1.0f, 1e-4f);
  EXPECT_NEAR(f.corners[0][2], 0.5f, 1e-4f);
}

// Multipart apply objects may carry quarter turns too; corners may
// land outside [0,1] after rotation (vanilla allows it, e.g. wall
// torches reach past the block).
TEST(ResolveTest, MultipartApplyRotationY) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r =
      (*pack)->Resolve("minecraft:rot_multipart", Props("powered=true"));
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 1u);

  const ModelFace& f = r->faces[0];
  EXPECT_EQ(f.CullFace(), 0u);  // element rotation cleared it
  EXPECT_NEAR(f.corners[0][0], 1.207107f, 1e-4f);
  EXPECT_NEAR(f.corners[0][1], 0.0f, 1e-6f);
  EXPECT_NEAR(f.corners[0][2], 0.5f, 1e-4f);
}

// Variant angles that are not quarter turns are ignored rather than
// approximated; only the element rotation applies (and it clears
// the cull face).
TEST(ResolveTest, VariantRotationNonQuarterTurnIgnored) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:rot_turn", Props("facing=skew"));
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 1u);

  const ModelFace& f = r->faces[0];
  EXPECT_EQ(f.CullFace(), 0u);  // element rotation cleared it
  EXPECT_NEAR(f.corners[0][0], -0.207107f, 1e-4f);
  EXPECT_NEAR(f.corners[0][2], 0.5f, 1e-4f);
}

// The cull-face remap in isolation: rot_zero's north face keeps its
// axis cull (zero element angle), so the variant quarter turn is the
// only transform. y=90 maps the north cull to east; x=180 maps it to
// south (north -> up -> south).
TEST(ResolveTest, VariantRotationRemapsCullFace) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto south = (*pack)->Resolve("minecraft:rot_cull", Props("facing=south"));
  ASSERT_TRUE(south.has_value()) << ToString(south.error());
  ASSERT_EQ(south->faces.size(), 1u);
  EXPECT_EQ(south->faces[0].CullFace(), 1u);  // NegZ -> PosX
  EXPECT_NEAR(south->faces[0].corners[0][0], 1.0f, 1e-6f);
  EXPECT_NEAR(south->faces[0].corners[0][2], 0.0f, 1e-6f);

  auto up = (*pack)->Resolve("minecraft:rot_cull", Props("facing=up"));
  ASSERT_TRUE(up.has_value()) << ToString(up.error());
  ASSERT_EQ(up->faces.size(), 1u);
  EXPECT_EQ(up->faces[0].CullFace(), 5u);  // NegZ -> PosZ (via PosY)
  EXPECT_NEAR(up->faces[0].corners[0][1], 1.0f, 1e-6f);
  EXPECT_NEAR(up->faces[0].corners[0][2], 1.0f, 1e-6f);
}

// "y": 90.0 -- real packs occasionally serialize the quarter turn
// as a float; simdjson types it as a double, so the old int-only
// reader silently dropped the whole rotation. The float form must
// parse to the same placement as the int form, cull remap included
// (mirrors VariantRotationRemapsCullFace's two variants).
TEST(ResolveTest, VariantRotationAcceptsFloatQuarterTurns) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto south =
      (*pack)->Resolve("minecraft:rot_cull_float", Props("facing=south"));
  ASSERT_TRUE(south.has_value()) << ToString(south.error());
  ASSERT_EQ(south->faces.size(), 1u);
  EXPECT_EQ(south->faces[0].CullFace(), 1u);  // NegZ -> PosX
  EXPECT_NEAR(south->faces[0].corners[0][0], 1.0f, 1e-6f);
  EXPECT_NEAR(south->faces[0].corners[0][2], 0.0f, 1e-6f);

  auto up = (*pack)->Resolve("minecraft:rot_cull_float", Props("facing=up"));
  ASSERT_TRUE(up.has_value()) << ToString(up.error());
  ASSERT_EQ(up->faces.size(), 1u);
  EXPECT_EQ(up->faces[0].CullFace(), 5u);  // NegZ -> PosZ (via PosY)
  EXPECT_NEAR(up->faces[0].corners[0][1], 1.0f, 1e-6f);
  EXPECT_NEAR(up->faces[0].corners[0][2], 1.0f, 1e-6f);
}

// Fractional (90.5) and integral-but-non-quarter (45.0) floats stay
// ignored exactly like their int counterparts: unrotated corners,
// axis cull intact.
TEST(ResolveTest, VariantRotationIgnoresFractionalAndNonQuarterFloats) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  for (const char* variant : {"facing=skew", "facing=half"}) {
    auto r = (*pack)->Resolve("minecraft:rot_cull_float", Props(variant));
    ASSERT_TRUE(r.has_value()) << ToString(r.error());
    ASSERT_EQ(r->faces.size(), 1u);
    EXPECT_EQ(r->faces[0].CullFace(), 6u) << variant;  // axis cull kept
    EXPECT_NEAR(r->faces[0].corners[0][0], 0.0f, 1e-6f) << variant;
    EXPECT_NEAR(r->faces[0].corners[0][1], 0.0f, 1e-6f) << variant;
    EXPECT_NEAR(r->faces[0].corners[0][2], 0.0f, 1e-6f) << variant;
    EXPECT_NEAR(r->faces[0].corners[2][0], 1.0f, 1e-6f) << variant;
    EXPECT_NEAR(r->faces[0].corners[2][1], 1.0f, 1e-6f) << variant;
    EXPECT_NEAR(r->faces[0].corners[2][2], 0.0f, 1e-6f) << variant;
  }
}

// Element rotation about x: corner 0 of the north face is (0,0,0),
// which rotates about (0.5,0.5,0.5) to (0, 0.5, -0.20711); corner 2
// (1,1,0) lands at (1, 1.20711, 0.5). A rotated face drops its cull.
TEST(ResolveTest, ElementRotationXAxisApplies) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:rot_slab_x", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 1u);

  const ModelFace& f = r->faces[0];
  EXPECT_EQ(f.CullFace(), 0u);
  EXPECT_NEAR(f.corners[0][0], 0.0f, 1e-6f);
  EXPECT_NEAR(f.corners[0][1], 0.5f, 1e-6f);
  EXPECT_NEAR(f.corners[0][2], -0.207107f, 1e-4f);
  EXPECT_NEAR(f.corners[2][0], 1.0f, 1e-6f);
  EXPECT_NEAR(f.corners[2][1], 1.207107f, 1e-4f);
  EXPECT_NEAR(f.corners[2][2], 0.5f, 1e-6f);
}

// Element rotation about z: corner 0 rotates to (0.5, -0.20711, 0)
// and corner 2 (1,1,0) to (0.5, 1.20711, 0) -- a 45-degree diamond
// in the xy plane, mirroring the y-axis case cyclically.
TEST(ResolveTest, ElementRotationZAxisApplies) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->Resolve("minecraft:rot_slab_z", EmptyProps());
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 1u);

  const ModelFace& f = r->faces[0];
  EXPECT_EQ(f.CullFace(), 0u);
  EXPECT_NEAR(f.corners[0][0], 0.5f, 1e-6f);
  EXPECT_NEAR(f.corners[0][1], -0.207107f, 1e-4f);
  EXPECT_NEAR(f.corners[0][2], 0.0f, 1e-6f);
  EXPECT_NEAR(f.corners[2][0], 0.5f, 1e-6f);
  EXPECT_NEAR(f.corners[2][1], 1.207107f, 1e-4f);
  EXPECT_NEAR(f.corners[2][2], 0.0f, 1e-6f);
}

// uvlock re-anchors the texture to the block: under x=180 the north
// face lands on the south side and samples uv = (x, 1 - y) per
// corner, exactly like a native south face. Without the lock the quad
// keeps its original UVs.
TEST(ResolveTest, VariantRotationUvlockReanchorsTexture) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto locked = (*pack)->Resolve("minecraft:rot_uvlock", Props("facing=up"));
  ASSERT_TRUE(locked.has_value()) << ToString(locked.error());
  ASSERT_EQ(locked->faces.size(), 6u);

  const ModelFace* south = nullptr;  // remapped north face
  const ModelFace* up = nullptr;     // remapped down face
  for (const ModelFace& f : locked->faces) {
    if (f.CullFace() == 5u) south = &f;
    if (f.CullFace() == 3u) up = &f;
  }
  ASSERT_NE(south, nullptr);
  ASSERT_NE(up, nullptr);

  // Rotated corner 0 is (0,1,1); a native south face samples uv =
  // (x, 1 - y), so it gets (0, 0).
  EXPECT_NEAR(south->corners[0][0], 0.0f, 1e-6f);
  EXPECT_NEAR(south->corners[0][1], 1.0f, 1e-6f);
  EXPECT_NEAR(south->corners[0][2], 1.0f, 1e-6f);
  EXPECT_NEAR(south->uvs[0][0], 0.0f, 1e-5f);
  EXPECT_NEAR(south->uvs[0][1], 0.0f, 1e-5f);
  EXPECT_NEAR(south->uvs[2][0], 1.0f, 1e-5f);
  EXPECT_NEAR(south->uvs[2][1], 1.0f, 1e-5f);

  // The down face flipped to up: uv = (x, z) of the rotated corners.
  EXPECT_NEAR(up->uvs[0][0], 0.0f, 1e-5f);
  EXPECT_NEAR(up->uvs[0][1], 1.0f, 1e-5f);
  EXPECT_NEAR(up->uvs[2][0], 1.0f, 1e-5f);
  EXPECT_NEAR(up->uvs[2][1], 0.0f, 1e-5f);

  // Control: without uvlock the quad keeps its UVs attached to the
  // rotated corners: corner 0 was the north face's bottom-west corner
  // and carries (1, 1) under the vanilla per-direction assignment
  // (image-right column, image-bottom row; the north basis mirrors u).
  auto unlocked =
      (*pack)->Resolve("minecraft:rot_uvlock", Props("facing=down"));
  ASSERT_TRUE(unlocked.has_value()) << ToString(unlocked.error());
  ASSERT_EQ(unlocked->faces.size(), 6u);
  const ModelFace* south2 = nullptr;
  for (const ModelFace& f : unlocked->faces) {
    if (f.CullFace() == 5u) south2 = &f;
  }
  ASSERT_NE(south2, nullptr);
  EXPECT_NEAR(south2->uvs[0][0], 1.0f, 1e-5f);
  EXPECT_NEAR(south2->uvs[0][1], 1.0f, 1e-5f);
}

// y=90 + uvlock on the top face: the plane rotates in place, so the
// lock re-anchors its UVs to the block axes; rotated corner 0 is
// (1,1,0) and samples uv (1, 0) on the up basis (x, z).
TEST(ResolveTest, VariantRotationUvlockTopFaceStaysAnchored) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto locked = (*pack)->Resolve("minecraft:rot_uvlock", Props("facing=south"));
  ASSERT_TRUE(locked.has_value()) << ToString(locked.error());
  ASSERT_EQ(locked->faces.size(), 6u);

  const ModelFace* top = nullptr;
  for (const ModelFace& f : locked->faces) {
    if (f.CullFace() == 3u) top = &f;
  }
  ASSERT_NE(top, nullptr);
  EXPECT_NEAR(top->corners[0][0], 1.0f, 1e-6f);
  EXPECT_NEAR(top->corners[0][2], 0.0f, 1e-6f);
  EXPECT_NEAR(top->uvs[0][0], 1.0f, 1e-5f);
  EXPECT_NEAR(top->uvs[0][1], 0.0f, 1e-5f);
  EXPECT_NEAR(top->uvs[2][0], 0.0f, 1e-5f);
  EXPECT_NEAR(top->uvs[2][1], 1.0f, 1e-5f);
}

// y=90 on a side face: under the model bases the uvlock re-anchor
// exactly reproduces the UVs the rotated face already carries (the
// texture turns with the block and stays upright either way). Locks
// the basis table against regressions; the uvlock patch noted this
// identity as a convention accident, with vanilla grounding it is a
// property worth testing.
TEST(ResolveTest, VariantRotationUvlockSideFaceIsIdentity) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto locked = (*pack)->Resolve("minecraft:rot_uvlock", Props("facing=south"));
  ASSERT_TRUE(locked.has_value()) << ToString(locked.error());
  ASSERT_EQ(locked->faces.size(), 6u);

  // The y=90 turn moves the north face to the east side; corner 0
  // (north bottom-west) lands at (1, 0, 0). The model east basis
  // (u = -Z, v = -Y) gives the quad corners (1,1) (0,1) (0,0) (1,0),
  // matching what the unlocked rotation would keep attached.
  const ModelFace* east = nullptr;
  for (const ModelFace& f : locked->faces) {
    if (f.CullFace() == 1u) east = &f;
  }
  ASSERT_NE(east, nullptr);
  EXPECT_NEAR(east->corners[0][0], 1.0f, 1e-6f);
  EXPECT_NEAR(east->corners[0][1], 0.0f, 1e-6f);
  EXPECT_NEAR(east->corners[0][2], 0.0f, 1e-6f);
  const float want[4][2] = {
      {1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}};
  for (int k = 0; k < 4; ++k) {
    EXPECT_NEAR(east->uvs[k][0], want[k][0], 1e-5f);
    EXPECT_NEAR(east->uvs[k][1], want[k][1], 1e-5f);
  }
}

// Multipart apply objects carry uvlock too; same math through the
// multipart path.
TEST(ResolveTest, MultipartApplyUvlock) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r =
      (*pack)->Resolve("minecraft:rot_uvlock_multipart", Props("powered=true"));
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->faces.size(), 6u);

  const ModelFace* south = nullptr;
  for (const ModelFace& f : r->faces) {
    if (f.CullFace() == 5u) south = &f;
  }
  ASSERT_NE(south, nullptr);
  EXPECT_NEAR(south->uvs[0][0], 0.0f, 1e-5f);
  EXPECT_NEAR(south->uvs[0][1], 0.0f, 1e-5f);
}

// y=270 normalizes to three quarter steps: north cull maps to west
// and corner 0 (0,0,0) lands at (0,0,1).
TEST(ResolveTest, VariantRotationY270NormalizesAndRemaps) {
  ResolveFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto west = (*pack)->Resolve("minecraft:rot_cull", Props("facing=west"));
  ASSERT_TRUE(west.has_value()) << ToString(west.error());
  ASSERT_EQ(west->faces.size(), 1u);
  EXPECT_EQ(west->faces[0].CullFace(), 2u);  // NegZ -> NegX
  EXPECT_NEAR(west->faces[0].corners[0][0], 0.0f, 1e-6f);
  EXPECT_NEAR(west->faces[0].corners[0][1], 0.0f, 1e-6f);
  EXPECT_NEAR(west->faces[0].corners[0][2], 1.0f, 1e-6f);
}

}  // namespace
