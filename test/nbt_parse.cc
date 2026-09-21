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
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "parser/error.h"
#include "parser/limits.h"
#include "parser/nbt/parse.h"
#include "parser/nbt/reader.h"
#include "parser/nbt/tag.h"
#include "parser/nbt_tree.h"

namespace nbt = fschema::parser::nbt;
namespace fp = fschema::parser;

template <typename T>
void PushBE(std::vector<std::byte>& buf, T val) {
  for (int i = sizeof(T) - 1; i >= 0; --i) {
    buf.push_back(static_cast<std::byte>((val >> (i * 8)) & 0xFF));
  }
}

void PushString(std::vector<std::byte>& buf, const std::string& str) {
  PushBE<std::uint16_t>(buf, static_cast<std::uint16_t>(str.length()));
  for (char c : str) {
    buf.push_back(static_cast<std::byte>(c));
  }
}

[[nodiscard]] std::vector<std::byte> MakeValidSimpleNbt() {
  std::vector<std::byte> buf;

  buf.push_back(static_cast<std::byte>(nbt::TagType::Compound));
  PushString(buf, "Root");

  buf.push_back(static_cast<std::byte>(nbt::TagType::Byte));
  PushString(buf, "byte_val");
  buf.push_back(static_cast<std::byte>(42));

  buf.push_back(static_cast<std::byte>(nbt::TagType::String));
  PushString(buf, "name");
  PushString(buf, "Hello");

  buf.push_back(static_cast<std::byte>(nbt::TagType::List));
  PushString(buf, "list");
  buf.push_back(static_cast<std::byte>(nbt::TagType::Int));
  PushBE<std::int32_t>(buf, 2);
  PushBE<std::int32_t>(buf, 10);
  PushBE<std::int32_t>(buf, -20);

  buf.push_back(static_cast<std::byte>(nbt::TagType::End));

  return buf;
}

TEST(NbtParseTest, ParsesValidSimpleNbt) {
  auto bytes = MakeValidSimpleNbt();
  std::span<const std::byte> byte_span(bytes);
  fp::DecodeLimits limits;

  nbt::ByteReader reader(byte_span, limits);
  auto result = nbt::ParseNbt(reader);

  ASSERT_TRUE(result.has_value()) << "Parse failed unexpectedly";

  const auto& root_tag = result.value();
  EXPECT_EQ(root_tag.type, nbt::TagType::Compound);
  EXPECT_EQ(root_tag.name, "Root");

  const auto* comp_ptr = std::get_if<std::unique_ptr<nbt::NbtCompound>>(&root_tag.payload);
  ASSERT_NE(comp_ptr, nullptr);
  ASSERT_NE(comp_ptr->get(), nullptr);

  const auto& comp = *comp_ptr->get();
  ASSERT_EQ(comp.children.size(), 3);

  EXPECT_EQ(comp.children[0].type, nbt::TagType::Byte);
  EXPECT_EQ(comp.children[0].name, "byte_val");
  const auto* byte_val = std::get_if<std::int8_t>(&comp.children[0].payload);
  ASSERT_NE(byte_val, nullptr);
  EXPECT_EQ(*byte_val, 42);

  EXPECT_EQ(comp.children[1].type, nbt::TagType::String);
  EXPECT_EQ(comp.children[1].name, "name");
  const auto* str_val = std::get_if<std::string>(&comp.children[1].payload);
  ASSERT_NE(str_val, nullptr);
  EXPECT_EQ(*str_val, "Hello");

  EXPECT_EQ(comp.children[2].type, nbt::TagType::List);
  EXPECT_EQ(comp.children[2].name, "list");
  const auto* list_ptr = std::get_if<std::unique_ptr<nbt::NbtList>>(&comp.children[2].payload);
  ASSERT_NE(list_ptr, nullptr);
  ASSERT_NE(list_ptr->get(), nullptr);

  const auto& list = *list_ptr->get();
  EXPECT_EQ(list.element_type, nbt::TagType::Int);
  ASSERT_EQ(list.children.size(), 2);

  const auto* item1 = std::get_if<std::int32_t>(&list.children[0]);
  ASSERT_NE(item1, nullptr);
  EXPECT_EQ(*item1, 10);

  const auto* item2 = std::get_if<std::int32_t>(&list.children[1]);
  ASSERT_NE(item2, nullptr);
  EXPECT_EQ(*item2, -20);
}

TEST(NbtParseTest, EmptyBytesInput) {
  std::vector<std::byte> bytes;
  std::span<const std::byte> byte_span(bytes);
  fp::DecodeLimits limits;

  nbt::ByteReader reader(byte_span, limits);
  auto result = nbt::ParseNbt(reader);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, fp::ParseError::Code::Truncated);
}

TEST(NbtParseTest, InvalidRootTagId) {
  std::vector<std::byte> bytes = { static_cast<std::byte>(13) };  // 13 is invalid
  std::span<const std::byte> byte_span(bytes);
  fp::DecodeLimits limits;

  nbt::ByteReader reader(byte_span, limits);
  auto result = nbt::ParseNbt(reader);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, fp::ParseError::Code::InvalidTagId);
}

TEST(NbtParseTest, TruncatedDuringString) {
  auto bytes = MakeValidSimpleNbt();
  bytes.resize(15);
  std::span<const std::byte> byte_span(bytes);
  fp::DecodeLimits limits;

  nbt::ByteReader reader(byte_span, limits);
  auto result = nbt::ParseNbt(reader);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, fp::ParseError::Code::Truncated);
}

TEST(NbtParseTest, DepthLimitExceeded) {
  fp::DecodeLimits limits;
  limits.max_nbt_depth = 0;

  auto bytes = MakeValidSimpleNbt();
  std::span<const std::byte> byte_span(bytes);

  nbt::ByteReader reader(byte_span, limits);
  auto result = nbt::ParseNbt(reader);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, fp::ParseError::Code::DepthLimitExceeded);
}