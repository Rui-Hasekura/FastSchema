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

#include "fschema/geometry/atlas.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/random.h"
#include "fschema/geometry/types.h"

namespace {

using fschema::ParseError;
using fschema::ToString;
using fschema::geometry::TextureAtlas;
using fschema::geometry::TextureAtlasBuilder;

std::vector<std::uint8_t> SolidTexture(std::uint32_t w,
                                       std::uint32_t h,
                                       std::uint8_t r,
                                       std::uint8_t g,
                                       std::uint8_t b,
                                       std::uint8_t a) {
  std::vector<std::uint8_t> out(w * h * 4);
  for (std::size_t i = 0; i < w * h; ++i) {
    out[i * 4 + 0] = r;
    out[i * 4 + 1] = g;
    out[i * 4 + 2] = b;
    out[i * 4 + 3] = a;
  }
  return out;
}

std::array<std::uint8_t, 4> ReadPixel(const TextureAtlas& atlas,
                                      std::uint32_t x,
                                      std::uint32_t y) {
  const std::size_t idx = (y * atlas.width + x) * 4;
  return {atlas.rgba[idx + 0],
          atlas.rgba[idx + 1],
          atlas.rgba[idx + 2],
          atlas.rgba[idx + 3]};
}

bool SlotsOverlap(const TextureAtlas::Slot& a, const TextureAtlas::Slot& b) {
  const float ax0 = a.u0, ay0 = a.v0, ax1 = a.u1, ay1 = a.v1;
  const float bx0 = b.u0, by0 = b.v0, bx1 = b.u1, by1 = b.v1;
  return !(ax1 <= bx0 || bx1 <= ax0 || ay1 <= by0 || by1 <= ay0);
}

TEST(AtlasTest, SingleTexturePlacement) {
  TextureAtlasBuilder builder(/*max_extent=*/256, /*padding=*/1);
  auto rgba = SolidTexture(16, 16, 0xAA, 0xBB, 0xCC, 0xDD);
  auto r = builder.Insert("block/stone", rgba, 16, 16);
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  EXPECT_EQ(*r, 0u);

  auto atlas = std::move(builder).Finalize();
  EXPECT_EQ(atlas.width, 256u);
  ASSERT_EQ(atlas.slots.size(), 1u);
  const auto& slot = atlas.slots[0];

  const std::uint32_t x0 = 1, y0 = 1;
  EXPECT_FLOAT_EQ(slot.u0, static_cast<float>(x0) / atlas.width);
  EXPECT_FLOAT_EQ(slot.v0, static_cast<float>(y0) / atlas.height);
  EXPECT_FLOAT_EQ(slot.u1, static_cast<float>(x0 + 16) / atlas.width);
  EXPECT_FLOAT_EQ(slot.v1, static_cast<float>(y0 + 16) / atlas.height);

  auto px = ReadPixel(atlas, x0, y0);
  EXPECT_EQ(px[0], 0xAA);
  EXPECT_EQ(px[1], 0xBB);
  EXPECT_EQ(px[2], 0xCC);
  EXPECT_EQ(px[3], 0xDD);
}

TEST(AtlasTest, DuplicatePathDeduplicates) {
  TextureAtlasBuilder builder(256, 1);
  auto rgba1 = SolidTexture(8, 8, 0x11, 0x22, 0x33, 0x44);
  auto rgba2 = SolidTexture(8, 8, 0xFF, 0xFF, 0xFF, 0xFF);

  auto r1 = builder.Insert("block/dirt", rgba1, 8, 8);
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(*r1, 0u);

  auto r2 = builder.Insert("block/dirt", rgba2, 8, 8);
  ASSERT_TRUE(r2.has_value());
  EXPECT_EQ(*r2, 0u);

  auto atlas = std::move(builder).Finalize();
  ASSERT_EQ(atlas.slots.size(), 1u);
  auto px = ReadPixel(atlas, 1, 1);
  EXPECT_EQ(px[0], 0x11);
}

TEST(AtlasTest, MultipleTexturesFitOnSingleShelf) {
  TextureAtlasBuilder builder(64, 1);
  auto a = SolidTexture(8, 8, 0xAA, 0, 0, 0xFF);
  auto b = SolidTexture(8, 8, 0xBB, 0, 0, 0xFF);
  auto c = SolidTexture(8, 8, 0xCC, 0, 0, 0xFF);

  auto ra = builder.Insert("a", a, 8, 8);
  auto rb = builder.Insert("b", b, 8, 8);
  auto rc = builder.Insert("c", c, 8, 8);
  ASSERT_TRUE(ra.has_value());
  ASSERT_TRUE(rb.has_value());
  ASSERT_TRUE(rc.has_value());
  EXPECT_EQ(*ra, 0u);
  EXPECT_EQ(*rb, 1u);
  EXPECT_EQ(*rc, 2u);

  auto atlas = std::move(builder).Finalize();
  ASSERT_EQ(atlas.slots.size(), 3u);

  EXPECT_FALSE(SlotsOverlap(atlas.slots[0], atlas.slots[1]));
  EXPECT_FALSE(SlotsOverlap(atlas.slots[0], atlas.slots[2]));
  EXPECT_FALSE(SlotsOverlap(atlas.slots[1], atlas.slots[2]));

  auto px_a = ReadPixel(atlas, 1, 1);
  EXPECT_EQ(px_a[0], 0xAA);
  auto px_b = ReadPixel(atlas, 11, 1);
  EXPECT_EQ(px_b[0], 0xBB);
  auto px_c = ReadPixel(atlas, 21, 1);
  EXPECT_EQ(px_c[0], 0xCC);
}

TEST(AtlasTest, ShelfWrapsWhenFull) {
  TextureAtlasBuilder builder(22, 1);
  auto a = SolidTexture(8, 8, 0xAA, 0, 0, 0xFF);
  auto b = SolidTexture(8, 8, 0xBB, 0, 0, 0xFF);
  auto c = SolidTexture(8, 8, 0xCC, 0, 0, 0xFF);

  auto ra = builder.Insert("a", a, 8, 8);
  auto rb = builder.Insert("b", b, 8, 8);
  auto rc = builder.Insert("c", c, 8, 8);
  ASSERT_TRUE(ra.has_value());
  ASSERT_TRUE(rb.has_value());
  ASSERT_TRUE(rc.has_value());

  auto atlas = std::move(builder).Finalize();
  ASSERT_EQ(atlas.slots.size(), 3u);

  EXPECT_FLOAT_EQ(atlas.slots[0].v0, 1.0f / atlas.height);
  EXPECT_FLOAT_EQ(atlas.slots[1].v0, 1.0f / atlas.height);
  EXPECT_FLOAT_EQ(atlas.slots[2].v0, 11.0f / atlas.height);

  auto px_c = ReadPixel(atlas, 1, 11);
  EXPECT_EQ(px_c[0], 0xCC);
}

TEST(AtlasTest, RejectsTextureWiderThanCanvas) {
  TextureAtlasBuilder builder(16, 1);
  auto rgba = SolidTexture(16, 16, 0xAA, 0xBB, 0xCC, 0xDD);
  auto r = builder.Insert("huge", rgba, 16, 16);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kAtlasOverflow);
  EXPECT_NE(r.error().path.find("huge"), std::string::npos);
}

TEST(AtlasTest, RejectsHeightOverflow) {
  TextureAtlasBuilder builder(16, 1);
  auto a = SolidTexture(8, 8, 0xAA, 0, 0, 0xFF);
  auto b = SolidTexture(8, 8, 0xBB, 0, 0, 0xFF);
  auto ra = builder.Insert("a", a, 8, 8);
  ASSERT_TRUE(ra.has_value());
  auto rb = builder.Insert("b", b, 8, 8);
  ASSERT_FALSE(rb.has_value());
  EXPECT_EQ(rb.error().code, ParseError::Code::kAtlasOverflow);
}

TEST(AtlasTest, PaddingPreventsBleeding) {
  TextureAtlasBuilder builder(16, 2);
  auto rgba = SolidTexture(4, 4, 0xFF, 0x00, 0x00, 0xFF);
  auto r = builder.Insert("red", rgba, 4, 4);
  ASSERT_TRUE(r.has_value());

  auto atlas = std::move(builder).Finalize();
  ASSERT_EQ(atlas.slots.size(), 1u);

  auto px_pad = ReadPixel(atlas, 0, 0);
  EXPECT_EQ(px_pad[0], 0u);
  EXPECT_EQ(px_pad[3], 0u);

  auto px_tex = ReadPixel(atlas, 2, 2);
  EXPECT_EQ(px_tex[0], 0xFF);
  EXPECT_EQ(px_tex[1], 0x00);
  EXPECT_EQ(px_tex[2], 0x00);
  EXPECT_EQ(px_tex[3], 0xFF);

  auto px_br = ReadPixel(atlas, 5, 5);
  EXPECT_EQ(px_br[0], 0xFF);

  auto px_pad_br = ReadPixel(atlas, 6, 6);
  EXPECT_EQ(px_pad_br[0], 0u);
  EXPECT_EQ(px_pad_br[3], 0u);
}

TEST(AtlasTest, RandomTexturesNeverOverlap) {
  fschema::base::Xoshiro256PP gen(42);
  std::uniform_int_distribution<std::uint32_t> size_dist(1, 16);

  TextureAtlasBuilder builder(128, 1);
  std::vector<std::uint32_t> slot_indices;

  for (int i = 0; i < 64; ++i) {
    const std::uint32_t w = size_dist(gen);
    const std::uint32_t h = size_dist(gen);
    auto rgba = SolidTexture(w,
                             h,
                             static_cast<std::uint8_t>(i),
                             static_cast<std::uint8_t>(i * 2),
                             0,
                             0xFF);
    auto r = builder.Insert("tex_" + std::to_string(i), rgba, w, h);
    ASSERT_TRUE(r.has_value()) << "insert " << i << ": " << ToString(r.error());
    slot_indices.push_back(*r);
  }

  auto atlas = std::move(builder).Finalize();
  ASSERT_EQ(atlas.slots.size(), 64u);

  for (std::size_t i = 0; i < atlas.slots.size(); ++i) {
    for (std::size_t j = i + 1; j < atlas.slots.size(); ++j) {
      EXPECT_FALSE(SlotsOverlap(atlas.slots[i], atlas.slots[j]))
          << "slots " << i << " and " << j << " overlap";
    }
  }

  EXPECT_LE(atlas.width, 128u);
  EXPECT_LE(atlas.height, 128u);
}

}  // namespace
