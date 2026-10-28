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
  ASSERT_EQ(left->faces.size(), 18u);
  for (const ModelFace& f : left->faces) {
    EXPECT_EQ(f.texture_path, "minecraft:entity/chest/normal_left");
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

  // The waxed copper family maps to the oxidation-level sheet.
  auto copper = (*pack)->Resolve("minecraft:waxed_weathered_copper_chest",
                                 Props("facing=south,type=single"));
  ASSERT_TRUE(copper.has_value());
  ASSERT_EQ(copper->faces.size(), 18u);
  for (const ModelFace& f : copper->faces) {
    EXPECT_EQ(f.texture_path, "minecraft:entity/chest/copper_weathered");
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
