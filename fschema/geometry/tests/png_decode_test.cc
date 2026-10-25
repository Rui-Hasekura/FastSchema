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

#include "fschema/geometry/internal/png_decode.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/geometry/resource_pack.h"
#include "fschema/geometry/tests/geometry_test_util.h"
#include "fschema/geometry/tests/png_writer.h"
#include "fschema/geometry/types.h"

namespace {

using fschema::ParseError;
using fschema::ToString;
using fschema::geometry::DecodedTexture;
using fschema::geometry::ResourcePack;
using fschema::geometry::internal::DecodePng;
using fschema::tests::SerializePng;
using fschema::tests::TempDir;
using fschema::tests::WritePngFile;

// DecodePng direct tests (no ResourcePack involved)

TEST(PngDecodeTest, DecodesSolidColorRgba) {
  constexpr std::uint32_t kW = 4, kH = 4;
  std::vector<std::uint8_t> rgba(kW * kH * 4);
  for (std::size_t i = 0; i < rgba.size(); i += 4) {
    rgba[i + 0] = 0x11;
    rgba[i + 1] = 0x22;
    rgba[i + 2] = 0x33;
    rgba[i + 3] = 0x44;
  }

  auto png = SerializePng(kW, kH, rgba.data());

  std::unique_ptr<std::vector<std::uint8_t>> owned;
  auto r =
      DecodePng(std::span<const std::uint8_t>(png.data(), png.size()), owned);
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  EXPECT_EQ(r->width, kW);
  EXPECT_EQ(r->height, kH);
  ASSERT_EQ(r->rgba.size(), rgba.size());
  EXPECT_EQ(std::memcmp(r->rgba.data(), rgba.data(), rgba.size()), 0);
  EXPECT_EQ(r->rgba.data(), owned->data());
}

TEST(PngDecodeTest, Decodes16x16Checkerboard) {
  constexpr std::uint32_t kW = 16, kH = 16;
  std::vector<std::uint8_t> rgba(kW * kH * 4);
  for (std::uint32_t y = 0; y < kH; ++y) {
    for (std::uint32_t x = 0; x < kW; ++x) {
      const std::size_t idx = (y * kW + x) * 4;
      const bool even = ((x + y) & 1) == 0;
      rgba[idx + 0] = even ? 0x00 : 0xFF;
      rgba[idx + 1] = even ? 0x00 : 0xFF;
      rgba[idx + 2] = even ? 0x00 : 0xFF;
      rgba[idx + 3] = 0xFF;
    }
  }

  auto png = SerializePng(kW, kH, rgba.data());

  std::unique_ptr<std::vector<std::uint8_t>> owned;
  auto r =
      DecodePng(std::span<const std::uint8_t>(png.data(), png.size()), owned);
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  ASSERT_EQ(r->width, kW);
  ASSERT_EQ(r->height, kH);
  ASSERT_EQ(r->rgba.size(), rgba.size());
  EXPECT_EQ(std::memcmp(r->rgba.data(), rgba.data(), rgba.size()), 0);
}

TEST(PngDecodeTest, RejectsEmptyInput) {
  std::unique_ptr<std::vector<std::uint8_t>> owned;
  auto r = DecodePng({}, owned);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kTextureDecodeFailed);
}

TEST(PngDecodeTest, RejectsBadSignature) {
  std::vector<std::uint8_t> bad = {
      0x0A, 0x1A, 0x0A, 0x0D, 0x4E, 0x50, 0x89, 0x00};
  std::unique_ptr<std::vector<std::uint8_t>> owned;
  auto r =
      DecodePng(std::span<const std::uint8_t>(bad.data(), bad.size()), owned);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kTextureDecodeFailed);
  EXPECT_FALSE(r.error().path.empty());
}

TEST(PngDecodeTest, Decodes1x1Rgba) {
  std::vector<std::uint8_t> rgba{0xFF, 0x00, 0x00, 0xFF};
  auto png = SerializePng(1, 1, rgba.data());

  std::unique_ptr<std::vector<std::uint8_t>> owned;
  auto r =
      DecodePng(std::span<const std::uint8_t>(png.data(), png.size()), owned);
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  EXPECT_EQ(r->width, 1u);
  EXPECT_EQ(r->height, 1u);
  ASSERT_EQ(r->rgba.size(), 4u);
  EXPECT_EQ(r->rgba[0], 0xFF);
  EXPECT_EQ(r->rgba[1], 0x00);
  EXPECT_EQ(r->rgba[2], 0x00);
  EXPECT_EQ(r->rgba[3], 0xFF);
}

// ResourcePack::LoadTexturePixels end-to-end tests

TEST(LoadTexturePixelsTest, DecodesCachedRgbaPixels) {
  TempDir root;
  constexpr std::uint32_t kW = 8, kH = 8;
  std::vector<std::uint8_t> rgba(kW * kH * 4);
  for (std::size_t i = 0; i < rgba.size(); i += 4) {
    rgba[i + 0] = 0xAA;
    rgba[i + 1] = 0xBB;
    rgba[i + 2] = 0xCC;
    rgba[i + 3] = 0xDD;
  }
  WritePngFile(
      root.Get() / "assets" / "minecraft" / "textures" / "block" / "test.png",
      kW,
      kH,
      rgba.data());

  auto pack = ResourcePack::Load(root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->LoadTexturePixels("minecraft:block/test");
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  EXPECT_EQ(r->width, kW);
  EXPECT_EQ(r->height, kH);
  ASSERT_EQ(r->rgba.size(), rgba.size());
  EXPECT_EQ(std::memcmp(r->rgba.data(), rgba.data(), rgba.size()), 0);
}

TEST(LoadTexturePixelsTest, CacheReturnsSameSpanOnSecondCall) {
  TempDir root;
  constexpr std::uint32_t kW = 4, kH = 4;
  std::vector<std::uint8_t> rgba(kW * kH * 4, 0xEE);
  WritePngFile(
      root.Get() / "assets" / "minecraft" / "textures" / "block" / "t.png",
      kW,
      kH,
      rgba.data());

  auto pack = ResourcePack::Load(root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r1 = (*pack)->LoadTexturePixels("minecraft:block/t");
  ASSERT_TRUE(r1.has_value());
  auto r2 = (*pack)->LoadTexturePixels("minecraft:block/t");
  ASSERT_TRUE(r2.has_value());

  EXPECT_EQ(r1->rgba.data(), r2->rgba.data());
}

TEST(LoadTexturePixelsTest, ReturnsErrorForMissingFile) {
  TempDir root;
  std::filesystem::create_directories(root.Get() / "assets" / "minecraft");
  auto pack = ResourcePack::Load(root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->LoadTexturePixels("minecraft:block/missing");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kModelMissingTexture);
  EXPECT_NE(r.error().path.find("missing.png"), std::string::npos);
}

TEST(LoadTexturePixelsTest, ReturnsErrorForNonPngBytes) {
  TempDir root;
  std::vector<std::uint8_t> garbage(256);
  for (std::size_t i = 0; i < garbage.size(); ++i)
    garbage[i] = static_cast<std::uint8_t>(i);
  std::filesystem::create_directories(root.Get() / "assets" / "minecraft" /
                                      "textures" / "block");
  std::ofstream f(
      root.Get() / "assets" / "minecraft" / "textures" / "block" / "junk.png",
      std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<const char*>(garbage.data()),
          static_cast<std::streamsize>(garbage.size()));

  auto pack = ResourcePack::Load(root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  auto r = (*pack)->LoadTexturePixels("minecraft:block/junk");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kTextureDecodeFailed);
}

TEST(LoadTexturePixelsTest, RejectsPathTraversal) {
  TempDir root;
  std::filesystem::create_directories(root.Get() / "assets" / "minecraft");
  auto pack = ResourcePack::Load(root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  EXPECT_FALSE((*pack)->LoadTexturePixels("/etc/passwd").has_value());
  EXPECT_FALSE((*pack)->LoadTexturePixels("../../etc/passwd").has_value());
  EXPECT_FALSE((*pack)->LoadTexturePixels("C:/windows/evil.png").has_value());
}

TEST(PngDecodeTest, DecodesLargeImageAcrossMultipleStoredBlocks) {
  // 128x128 RGBA = 64 KiB; raw with filter bytes = 128 * 513 = 65664,
  // which exceeds the 65535-byte single-block limit.
  constexpr std::uint32_t kW = 128, kH = 128;
  std::vector<std::uint8_t> rgba(kW * kH * 4);
  for (std::size_t i = 0; i < rgba.size(); i += 4) {
    rgba[i + 0] = static_cast<std::uint8_t>(i & 0xFF);
    rgba[i + 1] = static_cast<std::uint8_t>((i >> 8) & 0xFF);
    rgba[i + 2] = 0x5A;
    rgba[i + 3] = 0xFF;
  }
  auto png = SerializePng(kW, kH, rgba.data());

  std::unique_ptr<std::vector<std::uint8_t>> owned;
  auto r =
      DecodePng(std::span<const std::uint8_t>(png.data(), png.size()), owned);
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  EXPECT_EQ(r->width, kW);
  EXPECT_EQ(r->height, kH);
  ASSERT_EQ(r->rgba.size(), rgba.size());
  EXPECT_EQ(std::memcmp(r->rgba.data(), rgba.data(), rgba.size()), 0);
}

TEST(PngDecodeTest, DecodesNoDistanceCodeDeflateBlock) {
  constexpr std::uint8_t kPng[] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10,
      0x08, 0x00, 0x00, 0x00, 0x00, 0x3A, 0x98, 0xA0, 0xBD, 0x00, 0x00, 0x00,
      0x9C, 0x49, 0x44, 0x41, 0x54, 0x78, 0x01, 0x05, 0xC0, 0xB7, 0x0D, 0x00,
      0x00, 0x0C, 0xC3, 0xB0, 0xF4, 0xFC, 0xFF, 0x31, 0x11, 0x91, 0xD5, 0xB3,
      0xF7, 0x91, 0xD5, 0xB3, 0xF7, 0x91, 0xD5, 0xB3, 0xF7, 0x91, 0xD5, 0xB3,
      0xF7, 0x11, 0xD5, 0xB3, 0xF7, 0x91, 0xD5, 0xB3, 0xF7, 0x91, 0xD1, 0xB3,
      0xF7, 0x91, 0xD5, 0xB3, 0xF7, 0x91, 0x15, 0xB3, 0xF7, 0x91, 0xD5, 0xB3,
      0xF7, 0x91, 0xD5, 0xB1, 0xF7, 0x91, 0xD5, 0xB3, 0xF7, 0x91, 0xD5, 0x13,
      0xF7, 0x91, 0xD5, 0xB3, 0xF7, 0x91, 0xD5, 0xB3, 0xF1, 0x91, 0xD5, 0xB3,
      0xF7, 0x91, 0xD5, 0xB3, 0x17, 0x91, 0xD5, 0xB3, 0xF7, 0x91, 0xD5, 0xB3,
      0xF7, 0x91, 0xD5, 0xB3, 0xF7, 0x91, 0xD5, 0xB3, 0xF7, 0x11, 0xD5, 0xB3,
      0xF7, 0x91, 0xD5, 0xB3, 0xF7, 0x91, 0xD1, 0xB3, 0xF7, 0x91, 0xD5, 0xB3,
      0xF7, 0x91, 0x15, 0xB3, 0xF7, 0x91, 0xD5, 0xB3, 0xF7, 0x91, 0xD5, 0xB1,
      0xF7, 0x91, 0xD5, 0xB3, 0xF7, 0x91, 0xD5, 0x13, 0xF7, 0x91, 0xD5, 0xB3,
      0xF7, 0x91, 0xD5, 0xB3, 0xF1, 0x91, 0xD5, 0xB3, 0xF7, 0x91, 0xD5, 0xB3,
      0x07, 0xDD, 0x1F, 0x03, 0x81, 0xE0, 0x40, 0x7A, 0x2E, 0x00, 0x00, 0x00,
      0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
  };

  std::unique_ptr<std::vector<std::uint8_t>> owned;
  auto r = DecodePng(std::span<const std::uint8_t>(kPng, sizeof(kPng)), owned);
  ASSERT_TRUE(r.has_value()) << ToString(r.error());
  EXPECT_EQ(r->width, 16u);
  EXPECT_EQ(r->height, 16u);
  ASSERT_EQ(r->rgba.size(), 16u * 16u * 4u);
  // Pixel (x, y) is grayscale (x + y) % 8 replicated across RGBA.
  for (std::uint32_t y = 0; y < 16; ++y) {
    for (std::uint32_t x = 0; x < 16; ++x) {
      const std::size_t idx = (y * 16u + x) * 4u;
      const std::uint8_t gray = static_cast<std::uint8_t>((x + y) & 7u);
      EXPECT_EQ(r->rgba[idx + 0], gray);
      EXPECT_EQ(r->rgba[idx + 1], gray);
      EXPECT_EQ(r->rgba[idx + 2], gray);
      EXPECT_EQ(r->rgba[idx + 3], 0xFF);
    }
  }
}

TEST(PngDecodeTest, RejectsMatchAfterNoDistanceCodes) {
  constexpr std::uint8_t kPng[] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
      0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
      0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02,
      0x08, 0x00, 0x00, 0x00, 0x00, 0x40, 0xFF, 0xC2,
      0x31, 0x00, 0x00, 0x00, 0x16, 0x49, 0x44, 0x41,
      0x54, 0x78, 0x01, 0x0D, 0xC0, 0xB7, 0x0D, 0x00,
      0x00, 0x0C, 0xC3, 0x30, 0x29, 0xF5, 0xFF, 0x8F,
      0x13, 0xE0, 0x03, 0x00, 0x00, 0x00, 0x01, 0x9E,
      0xA2, 0x20, 0xEA, 0x00, 0x00, 0x00, 0x00, 0x49,
      0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
  };

  std::unique_ptr<std::vector<std::uint8_t>> owned;
  auto r =
      DecodePng(std::span<const std::uint8_t>(kPng, sizeof(kPng)), owned);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kTextureDecodeFailed);
  EXPECT_FALSE(r.error().path.empty());
}

}  // namespace
