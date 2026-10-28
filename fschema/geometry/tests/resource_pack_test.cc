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

#include "fschema/geometry/resource_pack.h"

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/geometry/tests/geometry_test_util.h"
#include "fschema/geometry/tests/resolve_test_util.h"

namespace {

using fschema::ParseError;
using fschema::ToString;
using fschema::geometry::ModelFace;
using fschema::geometry::ResourcePack;
using fschema::tests::Props;
using fschema::tests::TempDir;
using fschema::tests::WriteFile;
using fschema::tests::WritePngFile;

// Minimal synthetic pack: just the directory layout Load requires, with
// one trivial blockstate file so subsequent Resolve / LoadTexturePixels
// tests (in their own test files) can find at least one entry.
struct SyntheticPack {
  TempDir root;
  std::filesystem::path stone_blockstate;
  std::filesystem::path cube_all_model;

  SyntheticPack() {
    stone_blockstate =
        root.Get() / "assets" / "minecraft" / "blockstates" / "stone.json";
    cube_all_model = root.Get() / "assets" / "minecraft" / "models" / "block" /
                     "cube_all.json";

    WriteFile(stone_blockstate,
              R"({"variants": {"": {"model": "block/cube_all"}}})");
    WriteFile(
        cube_all_model,
        R"({"parent": "block/cube", "textures": {"all": "block/stone"}})");
  }
};

TEST(ResourcePackTest, LoadFailsOnNonexistentPath) {
  auto r = ResourcePack::Load("/this/path/does/not/exist");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kResourcePackNotFound);
  EXPECT_FALSE(r.error().path.empty());
}

TEST(ResourcePackTest, LoadFailsOnMissingAssetsMinecraft) {
  TempDir empty;
  auto r = ResourcePack::Load(empty.Get().generic_string());
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kResourcePackNotFound);
  EXPECT_NE(r.error().path.find("assets/minecraft"), std::string::npos);
}

TEST(ResourcePackTest, LoadSucceedsOnValidDirectory) {
  SyntheticPack sp;
  auto r = ResourcePack::Load(sp.root.Get().generic_string());
  ASSERT_TRUE(r.has_value())
      << "expected success; got error: " << ToString(r.error());
  EXPECT_NE((*r).get(), nullptr);
}

TEST(ResourcePackTest, LoadRejectsNonDirectoryPath) {
  TempDir root;
  auto zip_path = root.Get() / "fake.zip";
  WriteFile(zip_path, "PK\x03\x04");
  auto r = ResourcePack::Load(zip_path.generic_string());
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kResourcePackNotFound);
}

}  // namespace


// ---- chest-family synthesis from entity textures ----

// Writes a blockstate that points at a block model no vanilla pack
// ships (chests render through the block-entity renderer) plus the
// matching entity texture sheets.
struct ChestPack {
  TempDir root;
  std::vector<std::uint8_t> sheet{};

  ChestPack() {
    const auto base = root.Get();
    WriteFile(base / "assets" / "minecraft" / "blockstates" / "chest.json",
              R"({"variants": {"": {"model": "block/chest"}}})");
    WriteFile(base / "assets" / "minecraft" / "blockstates" /
                  "waxed_weathered_copper_chest.json",
              R"({"variants": {"": {"model": "block/chest"}}})");
    // Vanilla 1.21.2+ ships chest as a particle-only model with no
    // elements (rendering goes through the block-entity renderer);
    // the resolver turns it into zero faces, which is what the
    // synthesis hooks into.
    WriteFile(base / "assets" / "minecraft" / "models" / "block" /
                  "chest.json",
              R"({"textures": {"particle": "block/oak_planks"}})");
    sheet.assign(64 * 64 * 4, 0x7F);
    for (const char* name :
         {"normal", "normal_left", "normal_right", "trapped",
          "copper_weathered"}) {
      WritePngFile(base / "assets" / "minecraft" / "textures" / "entity" /
                       "chest" / (std::string(name) + ".png"),
                   64, 64, sheet.data());
    }
  }
};

TEST(ResourcePackTest, ChestSynthesizesEntityModelFaces) {
  ChestPack fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto single = (*pack)->Resolve("minecraft:chest",
                                 Props("facing=south,type=single"));
  ASSERT_TRUE(single.has_value()) << ToString(single.error());
  // bottom + lid + latch boxes, six faces each.
  ASSERT_EQ(single->faces.size(), 18u);
  for (const ModelFace& f : single->faces) {
    EXPECT_EQ(f.texture_path, "minecraft:entity/chest/normal");
  }

  // The bottom box's down face sits on the cell boundary and keeps a
  // cull direction; interior faces (the box is 14/16 wide) have none.
  int boundary_cull = 0;
  int interior = 0;
  for (const ModelFace& f : single->faces) {
    if (f.CullFace() != 0) {
      ++boundary_cull;
    } else {
      ++interior;
    }
  }
  // Single chest: down face at y=0 plus the latch's outer face at
  // z=1 (both on cell boundaries); every other face is interior.
  EXPECT_EQ(boundary_cull, 2);
  EXPECT_EQ(interior, 16);

  // Latch: a 2/16 x 4/16 face cluster near z=1, centered on x=8/16.
  bool latch_found = false;
  for (const ModelFace& f : single->faces) {
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& c : f.corners) {
      for (int k = 0; k < 3; ++k) {
        mn[k] = std::min(mn[k], c[k]);
        mx[k] = std::max(mx[k], c[k]);
      }
    }
    if (std::abs(mn[2] - 15.0f / 16.0f) < 1e-4f &&
        std::abs(mx[0] - 9.0f / 16.0f) < 1e-4f &&
        std::abs(mn[0] - 7.0f / 16.0f) < 1e-4f) {
      latch_found = true;
    }
  }
  EXPECT_TRUE(latch_found) << "south-facing latch not at +z";

  // facing=north flips the latch to the -z side.
  auto north =
      (*pack)->Resolve("minecraft:chest", Props("facing=north,type=single"));
  ASSERT_TRUE(north.has_value());
  float min_z = 1e30f, max_z = -1e30f;
  for (const ModelFace& f : north->faces) {
    for (const auto& c : f.corners) {
      min_z = std::min(min_z, c[2]);
      max_z = std::max(max_z, c[2]);
    }
  }
  // The latch spans z [0, 1/16]; the box z [1/16, 15/16].
  EXPECT_NEAR(min_z, 0.0f, 1e-4f);
  EXPECT_NEAR(max_z, 15.0f / 16.0f, 1e-4f);

  // Double chest halves pick their own texture sheets and 15-wide
  // boxes: the left half hugs the cell's low-x edge.
  auto left =
      (*pack)->Resolve("minecraft:chest", Props("facing=south,type=left"));
  ASSERT_TRUE(left.has_value());
  // 3 boxes x 6 faces minus each box's seam-side (west) face, whose
  // strips the double sheets never paint.
  ASSERT_EQ(left->faces.size(), 15u);
  for (const ModelFace& f : left->faces) {
    EXPECT_EQ(f.texture_path, "minecraft:entity/chest/normal_left");
    // The seam-side face must never be emitted (its strip is fully
    // transparent in the sheet).
    bool all_x0 = true;
    for (const auto& c : f.corners) {
      all_x0 = all_x0 && std::abs(c[0]) < 1e-4f;
    }
    EXPECT_FALSE(all_x0) << "left half emitted its west (seam) face";
  }
  float min_x = 1e30f, max_x = -1e30f;
  for (const ModelFace& f : left->faces) {
    for (const auto& c : f.corners) {
      min_x = std::min(min_x, c[0]);
      max_x = std::max(max_x, c[0]);
    }
  }
  EXPECT_NEAR(min_x, 0.0f, 1e-4f);
  EXPECT_NEAR(max_x, 15.0f / 16.0f, 1e-4f);

  auto right =
      (*pack)->Resolve("minecraft:chest", Props("facing=south,type=right"));
  ASSERT_TRUE(right.has_value());
  ASSERT_EQ(right->faces.size(), 15u);
  for (const ModelFace& f : right->faces) {
    EXPECT_EQ(f.texture_path, "minecraft:entity/chest/normal_right");
    bool all_x1 = true;
    for (const auto& c : f.corners) {
      all_x1 = all_x1 && std::abs(c[0] - 1.0f) < 1e-4f;
    }
    EXPECT_FALSE(all_x1) << "right half emitted its east (seam) face";
  }

  // The waxed copper family maps to the oxidation-level sheet.
  auto copper = (*pack)->Resolve("minecraft:waxed_weathered_copper_chest",
                                 Props("facing=south,type=single"));
  ASSERT_TRUE(copper.has_value());
  ASSERT_EQ(copper->faces.size(), 18u);
  for (const ModelFace& f : copper->faces) {
    EXPECT_EQ(f.texture_path, "minecraft:entity/chest/copper_weathered");
  }
}

TEST(ResourcePackTest, ChestFacesAreRectsOnBoxPlanes) {
  // Regression for the corner-index convention: every face must sit
  // exactly on one of the box planes (no diagonal "fin" quads slicing
  // through the box, no missing north/south planes), be a rectangle,
  // wind outward per the flipped-triangle convention, and sample
  // exactly its box-UV strip with the texture upright.
  ChestPack fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  auto single = (*pack)->Resolve("minecraft:chest",
                                 Props("facing=south,type=single"));
  ASSERT_TRUE(single.has_value()) << ToString(single.error());
  ASSERT_EQ(single->faces.size(), 18u);

  struct Box {
    const char* name;
    float lo[3], hi[3];  // extents, /16 block units
  };
  const Box kBoxes[] = {
      {"bottom", {1, 0, 1}, {15, 10, 15}},
      {"lid", {1, 9, 1}, {15, 14, 15}},
      {"latch", {7, 7, 15}, {9, 11, 16}},
  };
  const float kEps = 1e-4f;
  const ModelFace* face_of[3][6] = {};  // [box][axis*2 + hi]

  for (const ModelFace& f : single->faces) {
    // Match the face to exactly one (box, plane): all corners on the
    // plane and inside the box.
    int matches = 0, box = -1, plane = -1;
    for (int bi = 0; bi < 3; ++bi) {
      for (int pl = 0; pl < 6; ++pl) {
        const int axis = pl / 2;
        const float pv = (pl % 2 ? kBoxes[bi].hi[axis] : kBoxes[bi].lo[axis]) /
                         16.0f;
        bool on_plane = true, inside = true;
        for (const auto& c : f.corners) {
          if (std::abs(c[axis] - pv) > kEps) on_plane = false;
          for (int k = 0; k < 3; ++k) {
            if (c[k] < kBoxes[bi].lo[k] / 16.0f - kEps ||
                c[k] > kBoxes[bi].hi[k] / 16.0f + kEps) {
              inside = false;
            }
          }
        }
        if (on_plane && inside) {
          ++matches;
          box = bi;
          plane = pl;
        }
      }
    }
    ASSERT_EQ(matches, 1) << "face belongs to " << matches << " box planes";
    ASSERT_EQ(face_of[box][plane], nullptr)
        << "duplicate face for " << kBoxes[box].name << " plane " << plane;
    face_of[box][plane] = &f;

    // Rectangle: adjacent edges perpendicular (a 45-degree sheared
    // "fin" fails this).
    const auto& c = f.corners;
    const float e1[3] = {c[1][0] - c[0][0], c[1][1] - c[0][1],
                         c[1][2] - c[0][2]};
    const float e3[3] = {c[3][0] - c[0][0], c[3][1] - c[0][1],
                         c[3][2] - c[0][2]};
    EXPECT_NEAR(e1[0] * e3[0] + e1[1] * e3[1] + e1[2] * e3[2], 0.0f, kEps)
        << "quad is not a rectangle";

    // Outward winding: n = (c2-c0) x (c1-c0) is the geometric normal
    // of PushQuadIndices' first flipped triangle; it must point away
    // from the box centre.
    const float a[3] = {c[2][0] - c[0][0], c[2][1] - c[0][1],
                        c[2][2] - c[0][2]};
    const float n[3] = {a[1] * e1[2] - a[2] * e1[1],
                        a[2] * e1[0] - a[0] * e1[2],
                        a[0] * e1[1] - a[1] * e1[0]};
    float fc[3] = {0, 0, 0}, bc[3] = {0, 0, 0};
    for (int i = 0; i < 4; ++i) {
      for (int k = 0; k < 3; ++k) fc[k] += c[i][k] / 4.0f;
    }
    for (int k = 0; k < 3; ++k) {
      bc[k] = (kBoxes[box].lo[k] + kBoxes[box].hi[k]) / 32.0f;
    }
    const float away[3] = {fc[0] - bc[0], fc[1] - bc[1], fc[2] - bc[2]};
    EXPECT_GT(n[0] * away[0] + n[1] * away[1] + n[2] * away[2], 0.0f)
        << "winding points into the box";
  }

  // All 18 (box, plane) combos covered exactly once.
  for (int bi = 0; bi < 3; ++bi) {
    for (int pl = 0; pl < 6; ++pl) {
      EXPECT_NE(face_of[bi][pl], nullptr)
          << kBoxes[bi].name << " missing plane " << pl;
    }
  }

  // Strip rects (texels on the 64x64 sheet, /64): bottom north/up,
  // lid south, latch north. v is measured from the sheet's top row.
  auto expect_strip = [&](const ModelFace* f, float l, float r, float t,
                          float b) {
    ASSERT_NE(f, nullptr);
    float umn = 1e30f, umx = -1e30f, vmn = 1e30f, vmx = -1e30f;
    for (const auto& uv : f->uvs) {
      umn = std::min(umn, uv[0]);
      umx = std::max(umx, uv[0]);
      vmn = std::min(vmn, uv[1]);
      vmx = std::max(vmx, uv[1]);
    }
    EXPECT_NEAR(umn, l / 64.0f, kEps);
    EXPECT_NEAR(umx, r / 64.0f, kEps);
    EXPECT_NEAR(vmn, t / 64.0f, kEps);
    EXPECT_NEAR(vmx, b / 64.0f, kEps);
  };
  expect_strip(face_of[0][4], 14, 28, 33, 43);   // bottom north
  expect_strip(face_of[0][3], 28, 42, 19, 33);   // bottom up
  expect_strip(face_of[1][5], 42, 56, 14, 19);   // lid south
  expect_strip(face_of[2][4], 1, 3, 1, 5);       // latch north

  // Upright: on the bottom box's north face the strip's top row (v =
  // 33/64) lands on the face's top edge (y = 10/16).
  const ModelFace* bn = face_of[0][4];
  ASSERT_NE(bn, nullptr);
  for (int i = 0; i < 4; ++i) {
    const float want = bn->corners[i][1] > 0.5f ? 33.0f / 64.0f
                                                : 43.0f / 64.0f;
    EXPECT_NEAR(bn->uvs[i][1], want, kEps) << "corner " << i;
  }
}

TEST(ResourcePackTest, ChestStaysEmptyWithoutEntityTexture) {
  // Blockstate referencing the never-shipped block model, and no
  // entity texture either: the zero-face behaviour must stay (no
  // lava fallback, no synthesis).
  TempDir root;
  WriteFile(root.Get() / "assets" / "minecraft" / "blockstates" /
                "chest.json",
            R"({"variants": {"": {"model": "block/chest"}}})");
  WriteFile(root.Get() / "assets" / "minecraft" / "models" / "block" /
                "chest.json",
            R"({"textures": {"particle": "block/oak_planks"}})");
  auto pack = ResourcePack::Load(root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  auto r = (*pack)->Resolve("minecraft:chest", Props("facing=south"));
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  EXPECT_TRUE(r->faces.empty());
}
