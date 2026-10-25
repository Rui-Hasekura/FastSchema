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
#include <cstring>
#include <span>
#include <string_view>

#include "fschema/geometry/block_props.h"
#include "fschema/ir/types.h"

namespace {

using fschema::geometry::BlockProps;
using fschema::ir::BlockState;
using fschema::ir::PropertyEncoding;

std::span<const std::byte> AsBytes(std::string_view s) {
  return std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(s.data()), s.size());
}

TEST(BlockPropsTest, EmptyWhenEncodingIsNone) {
  BlockState state{
      .name = "minecraft:stone",
      .raw_properties = {},
      .prop_encoding = PropertyEncoding::kNone,
  };
  BlockProps p(state);
  EXPECT_TRUE(p.Empty());
  EXPECT_EQ(p.Size(), 0u);
  EXPECT_FALSE(p.Get("anything").has_value());
}

TEST(BlockPropsTest, DecodesSpongeStringEncoding) {
  const std::string src = "facing=north,half=bottom,shape=straight";
  BlockState state{
      .name = "minecraft:oak_stairs",
      .raw_properties = AsBytes(src),
      .prop_encoding = PropertyEncoding::kString,
  };
  BlockProps p(state);
  ASSERT_EQ(p.Size(), 3u);
  EXPECT_EQ(p.Get("facing").value_or(""), "north");
  EXPECT_EQ(p.Get("half").value_or(""), "bottom");
  EXPECT_EQ(p.Get("shape").value_or(""), "straight");
}

TEST(BlockPropsTest, EmptyStringIsOk) {
  BlockState state{
      .name = "minecraft:stone",
      .raw_properties = AsBytes(""),
      .prop_encoding = PropertyEncoding::kString,
  };
  BlockProps p(state);
  EXPECT_TRUE(p.Empty());
}

TEST(BlockPropsTest, DropsMalformedPairs) {
  BlockState state{
      .name = "minecraft:test",
      .raw_properties = AsBytes("a=1,broken,c=2"),
      .prop_encoding = PropertyEncoding::kString,
  };
  BlockProps p(state);
  ASSERT_EQ(p.Size(), 2u);
  EXPECT_EQ(p.Get("a").value_or(""), "1");
  EXPECT_EQ(p.Get("c").value_or(""), "2");
}

TEST(BlockPropsTest, TrailingCommaIsTolerated) {
  BlockState state{
      .name = "minecraft:test",
      .raw_properties = AsBytes("a=1,b=2,"),
      .prop_encoding = PropertyEncoding::kString,
  };
  BlockProps p(state);
  EXPECT_EQ(p.Size(), 2u);
}

TEST(BlockPropsTest, EmptyValueIsAllowed) {
  BlockState state{
      .name = "minecraft:test",
      .raw_properties = AsBytes("a="),
      .prop_encoding = PropertyEncoding::kString,
  };
  BlockProps p(state);
  ASSERT_EQ(p.Size(), 1u);
  ASSERT_TRUE(p.Get("a").has_value());
  EXPECT_TRUE(p.Get("a")->empty());
}

TEST(BlockPropsTest, StableHashIsOrderIndependent) {
  BlockState s1{
      .name = "minecraft:test",
      .raw_properties = AsBytes("a=1,b=2,c=3"),
      .prop_encoding = PropertyEncoding::kString,
  };
  BlockState s2{
      .name = "minecraft:test",
      .raw_properties = AsBytes("c=3,a=1,b=2"),
      .prop_encoding = PropertyEncoding::kString,
  };
  BlockProps p1(s1);
  BlockProps p2(s2);
  EXPECT_EQ(p1.StableHash(), p2.StableHash())
      << "BlockProps hash must be order-independent so the resolver can cache";
}

TEST(BlockPropsTest, StableHashDiffersForDifferentContent) {
  BlockState s1{
      .name = "minecraft:test",
      .raw_properties = AsBytes("a=1,b=2"),
      .prop_encoding = PropertyEncoding::kString,
  };
  BlockState s2{
      .name = "minecraft:test",
      .raw_properties = AsBytes("a=1,b=3"),
      .prop_encoding = PropertyEncoding::kString,
  };
  BlockProps p1(s1);
  BlockProps p2(s2);
  EXPECT_NE(p1.StableHash(), p2.StableHash());
}

// Litematica NBT compound body builder. Each string entry is:
//   tag_id(1) + name_len(2 BE) + name + value_len(2 BE) + value
// Trailing kEnd tag (single byte 0).
struct NbtBodyBuilder {
  std::vector<std::byte> bytes;
  void AddString(std::string_view key, std::string_view value) {
    bytes.push_back(std::byte{8});  // kString tag id
    bytes.push_back(std::byte{static_cast<unsigned char>(key.size() >> 8)});
    bytes.push_back(std::byte{static_cast<unsigned char>(key.size() & 0xFF)});
    for (char c : key)
      bytes.push_back(std::byte{static_cast<unsigned char>(c)});
    bytes.push_back(std::byte{static_cast<unsigned char>(value.size() >> 8)});
    bytes.push_back(std::byte{static_cast<unsigned char>(value.size() & 0xFF)});
    for (char c : value)
      bytes.push_back(std::byte{static_cast<unsigned char>(c)});
  }
  void End() { bytes.push_back(std::byte{0}); }
};

TEST(BlockPropsTest, DecodesLitematicaNbtEncoding) {
  NbtBodyBuilder b;
  b.AddString("facing", "north");
  b.AddString("half", "bottom");
  b.End();
  BlockState state{
      .name = "minecraft:oak_stairs",
      .raw_properties = std::span<const std::byte>(b.bytes),
      .prop_encoding = PropertyEncoding::kNbt,
  };
  BlockProps p(state);
  ASSERT_EQ(p.Size(), 2u);
  EXPECT_EQ(p.Get("facing").value_or(""), "north");
  EXPECT_EQ(p.Get("half").value_or(""), "bottom");
}

TEST(BlockPropsTest, EmptyNbtCompoundIsOk) {
  std::byte end{0};
  BlockState state{
      .name = "minecraft:stone",
      .raw_properties = std::span<const std::byte>(&end, 1),
      .prop_encoding = PropertyEncoding::kNbt,
  };
  BlockProps p(state);
  EXPECT_TRUE(p.Empty());
}

}  // namespace
