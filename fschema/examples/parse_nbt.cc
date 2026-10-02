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

// Example: Parse a raw NBT file (gzip-compressed or uncompressed) and print
// the tree structure.
//
// Usage: parse_nbt <file.nbt>

#include <cstdint>
#include <memory>
#include <print>
#include <span>
#include <variant>
#include <vector>

#include "fschema/base/compressor.h"
#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/base/nbt_parse.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/base/nbt_tag.h"

namespace fb = fschema::base;

namespace {

void PrintTag(const fb::NbtTag& tag, int indent) {
  std::string pad(indent * 2, ' ');
  std::println(
      "{}Name: \"{}\"  Type: {}", pad, tag.name, static_cast<int>(tag.type));

  switch (tag.type) {
    case fb::TagType::Byte:
      if (const auto* v = std::get_if<std::int8_t>(&tag.payload))
        std::println("{}  Value: {}", pad, static_cast<int>(*v));
      break;
    case fb::TagType::Short:
      if (const auto* v = std::get_if<std::int16_t>(&tag.payload))
        std::println("{}  Value: {}", pad, *v);
      break;
    case fb::TagType::Int:
      if (const auto* v = std::get_if<std::int32_t>(&tag.payload))
        std::println("{}  Value: {}", pad, *v);
      break;
    case fb::TagType::Long:
      if (const auto* v = std::get_if<std::int64_t>(&tag.payload))
        std::println("{}  Value: {}", pad, *v);
      break;
    case fb::TagType::Float:
      if (const auto* v = std::get_if<float>(&tag.payload))
        std::println("{}  Value: {}", pad, *v);
      break;
    case fb::TagType::Double:
      if (const auto* v = std::get_if<double>(&tag.payload))
        std::println("{}  Value: {}", pad, *v);
      break;
    case fb::TagType::String:
      if (const auto* v = std::get_if<std::string_view>(&tag.payload))
        std::println("{}  Value: \"{}\"", pad, *v);
      break;
    case fb::TagType::ByteArray:
      if (const auto* v =
              std::get_if<std::span<const std::int8_t>>(&tag.payload))
        std::println("{}  Length: {}", pad, v->size());
      break;
    case fb::TagType::IntArray:
    case fb::TagType::LongArray:
      if (const auto* v = std::get_if<std::span<const std::byte>>(&tag.payload))
        std::println("{}  Bytes: {}", pad, v->size());
      break;
    case fb::TagType::Compound: {
      const auto* comp =
          std::get_if<std::unique_ptr<fb::NbtCompound>>(&tag.payload);
      if (comp && *comp) {
        std::println("{}  Children: {}", pad, (*comp)->children.size());
        for (const auto& child : (*comp)->children) {
          PrintTag(child, indent + 1);
        }
        return;
      }
      break;
    }
    case fb::TagType::List: {
      const auto* list =
          std::get_if<std::unique_ptr<fb::NbtList>>(&tag.payload);
      if (list && *list) {
        std::println("{}  Elements: {}  ElemType: {}",
                     pad,
                     (*list)->children.size(),
                     static_cast<int>((*list)->element_type));
        for (std::size_t i = 0; i < (*list)->children.size() && i < 5; ++i) {
          std::println("{}  [{}]", pad, i);
          if (auto* sub_comp = std::get_if<std::unique_ptr<fb::NbtCompound>>(
                  &(*list)->children[i])) {
            std::println("{} (compound)", pad);
            if (*sub_comp) {
              for (const auto& child : (*sub_comp)->children) {
                PrintTag(child, indent + 2);
              }
            }
          } else if (const auto* v =
                         std::get_if<std::int32_t>(&(*list)->children[i])) {
            std::println("{} Int: {}", pad, *v);
          } else if (const auto* v =
                         std::get_if<std::string_view>(&(*list)->children[i])) {
            std::println("{} String: \"{}\"", pad, *v);
          } else {
            std::println("{} (other)", pad);
          }
        }
        if ((*list)->children.size() > 5) {
          std::println("{}  ... ({} more)", pad, (*list)->children.size() - 5);
        }
        return;
      }
      break;
    }
    default:
      break;
  }
  std::println("");
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::println("Usage: {} <file.nbt>", argv[0]);
    return 1;
  }

  // STEP 1: Decompression (try gzip first, fall back to raw bytes)
  std::vector<std::byte> raw_bytes;
  auto decompressed = fb::DecompressGzipFile(argv[1]);
  if (decompressed) {
    raw_bytes = std::move(*decompressed);
  } else {
    // Not gzip — read raw
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) {
      std::println("Failed to open file: {}", argv[1]);
      return 1;
    }
    std::fseek(f, 0, SEEK_END);
    long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    raw_bytes.resize(static_cast<std::size_t>(len));
    std::fread(raw_bytes.data(), 1, raw_bytes.size(), f);
    std::fclose(f);
  }

  // STEP 2: Parse the NBT tree
  std::span<const std::byte> byte_span(raw_bytes);
  fb::DecodeLimits limits;
  fb::ByteReader reader(byte_span, limits);

  auto result = fb::ParseNbt(reader);
  if (!result) {
    const fschema::ParseError& err = result.error();
    std::println("Parse failed: {} at \"{}\" (offset: {})",
                 fschema::ToString(err),
                 err.path,
                 err.offset);
    return 1;
  }

  std::println("Bytes consumed: {} / {}", reader.pos(), raw_bytes.size());

  // STEP 3: Print the tree
  const auto& root_tag = *result;
  PrintTag(root_tag, 0);

  return 0;
}