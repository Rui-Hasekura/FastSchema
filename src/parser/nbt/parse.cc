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

#include "parser/nbt/parse.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "parser/error.h"
#include "parser/nbt/reader.h"
#include "parser/nbt/tag.h"
#include "parser/nbt_tree.h"

namespace fschema::parser::nbt {

  [[nodiscard]] ParseResult<NbtPayload> ParsePayload(ByteReader& reader, TagType tag_type);

  [[nodiscard]] ParseResult<NbtCompound> ParseCompound(ByteReader& reader) {
    reader.push_depth();
    if (reader.depth() > reader.limits().max_nbt_depth) [[unlikely]] {
      reader.pop_depth();
      return std::unexpected(reader.Error(ParseError::Code::DepthLimitExceeded));
    }

    NbtCompound compound;
    compound.children.reserve(8);
    for (;;) {
      std::string_view name;
      auto tag_result = reader.ReadCompoundEntryHeaderView(name);
      if (!tag_result) {
        reader.pop_depth();
        return std::unexpected(tag_result.error());
      }
      if (*tag_result == TagType::End) {
        break;
      }

      auto payload_result = ParsePayload(reader, *tag_result);
      if (!payload_result) {
        reader.pop_depth();
        return std::unexpected(payload_result.error());
      }

      compound.children.push_back(NbtTag{
          *tag_result, name, std::move(*payload_result)
        });
    }

    reader.pop_depth();
    return compound;
  }

  [[nodiscard]] ParseResult<NbtList> ParseList(ByteReader& reader) {
    auto header = reader.ReadListHeader();
    if (!header) {
      return std::unexpected(header.error());
    }
    auto [elem_type, count] = *header;
    if (elem_type == TagType::End || count == 0) {
      return NbtList{ elem_type, {} };
    }

    reader.push_depth();
    if (reader.depth() > reader.limits().max_nbt_depth) [[unlikely]] {
      reader.pop_depth();
      return std::unexpected(reader.Error(ParseError::Code::DepthLimitExceeded));
    }

    NbtList list;
    list.element_type = elem_type;
    list.children.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
      auto payload_result = ParsePayload(reader, elem_type);
      if (!payload_result) {
        reader.pop_depth();
        return std::unexpected(payload_result.error());
      }
      list.children.push_back(std::move(*payload_result));
    }

    reader.pop_depth();
    return list;
  }

  [[nodiscard]] ParseResult<NbtPayload> ParsePayload(ByteReader& reader, TagType tag_type) {
    switch (tag_type) {
    case TagType::End:
      return std::monostate{};
    case TagType::Byte:
      return reader.Read<std::int8_t>();
    case TagType::Short:
      return reader.Read<std::int16_t>();
    case TagType::Int:
      return reader.Read<std::int32_t>();
    case TagType::Long:
      return reader.Read<std::int64_t>();
    case TagType::Float:
      return reader.Read<float>();
    case TagType::Double:
      return reader.Read<double>();
    case TagType::ByteArray: {
      auto arr = reader.ReadArraySpan<std::int8_t>(reader.limits().max_array_elements);
      if (!arr) return std::unexpected(arr.error());
      return *arr;
    }
    case TagType::String: {
      auto str = reader.ReadStringView();
      if (!str) return std::unexpected(str.error());
      return *str;
    }
    case TagType::List: {
      auto list_result = ParseList(reader);
      if (!list_result) return std::unexpected(list_result.error());
      return std::make_unique<NbtList>(std::move(*list_result));
    }
    case TagType::Compound: {
      auto comp_result = ParseCompound(reader);
      if (!comp_result) return std::unexpected(comp_result.error());
      return std::make_unique<NbtCompound>(std::move(*comp_result));
    }
    case TagType::IntArray: {
      auto len_res = reader.ReadLength(reader.limits().max_array_elements);
      if (!len_res) return std::unexpected(len_res.error());

      const std::size_t total = *len_res * sizeof(std::int32_t);
      auto raw_res = reader.PeekRaw(total);
      if (!raw_res) return std::unexpected(raw_res.error());

      reader.advance(total);
      return *raw_res;
    }
    case TagType::LongArray: {
      auto len_res = reader.ReadLength(reader.limits().max_array_elements);
      if (!len_res) return std::unexpected(len_res.error());

      const std::size_t total = *len_res * sizeof(std::int64_t);
      auto raw_res = reader.PeekRaw(total);
      if (!raw_res) return std::unexpected(raw_res.error());

      reader.advance(total);
      return *raw_res;
    }
    default:
      [[unlikely]] return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
    }
  }

  // Entry point for generic NBT parsing
  [[nodiscard]] ParseResult<NbtTag> ParseNbt(ByteReader& reader) {
    auto tag_raw = reader.Read<std::uint8_t>();
    if (!tag_raw) return std::unexpected(tag_raw.error());
    if (!IsValidTagType(*tag_raw)) [[unlikely]] {
      return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
    }

    auto tag_type = static_cast<TagType>(*tag_raw);
    auto name = reader.ReadStringView();
    if (!name) return std::unexpected(name.error());

    auto payload_result = ParsePayload(reader, tag_type);
    if (!payload_result) return std::unexpected(payload_result.error());

    return NbtTag{ tag_type, *name, std::move(*payload_result) };
  }

} // namespace fschema::parser::nbt