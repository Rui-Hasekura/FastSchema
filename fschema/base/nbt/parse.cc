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

#include "fschema/base/nbt/parse.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/nbt/reader.h"
#include "fschema/base/nbt/scope.h"
#include "fschema/base/nbt/tag.h"
#include "fschema/base/nbt/tree.h"

namespace fschema::base {

[[nodiscard]] ParseResult<NbtPayload> ParsePayload(ByteReader& reader,
                                                   TagType tag_type);

[[nodiscard]] ParseResult<NbtCompound> ParseCompound(ByteReader& reader) {
  NbtCompound compound;
  compound.children.reserve(16);

  auto result = ForEachCompoundField(
      reader,
      [&](std::string_view name, TagType tag_type) -> ParseResult<void> {
        auto payload = ParsePayload(reader, tag_type);
        if (!payload) return std::unexpected(payload.error());
        compound.children.push_back(
            NbtTag{tag_type, name, std::move(*payload)});
        return {};
      });
  if (!result) return std::unexpected(result.error());
  return compound;
}

[[nodiscard]] ParseResult<NbtList> ParseList(ByteReader& reader) {
  auto header = reader.ReadListHeader();
  if (!header) return std::unexpected(header.error());
  auto [elem_type, count] = *header;
  if (elem_type == TagType::kEnd || count == 0) {
    return NbtList{elem_type, {}};
  }

  if (count > reader.limits().max_nbt_list_elements) [[unlikely]] {
    return std::unexpected(reader.Error(ParseError::Code::kOversizedPayload));
  }

  NbtList list;
  list.element_type = elem_type;
  list.children.reserve(count);

  DepthGuard guard(reader);
  if (reader.depth() > reader.limits().max_nbt_depth) [[unlikely]] {
    return std::unexpected(reader.Error(ParseError::Code::kDepthLimitExceeded));
  }

  for (std::size_t i = 0; i < count; ++i) {
    auto payload = ParsePayload(reader, elem_type);
    if (!payload) return std::unexpected(payload.error());
    list.children.push_back(std::move(*payload));
  }
  return list;
}

[[nodiscard]] ParseResult<NbtPayload> ParsePayload(ByteReader& reader,
                                                   TagType tag_type) {
  switch (tag_type) {
    case TagType::kEnd:
      return std::monostate{};
    case TagType::kByte:
      return reader.Read<std::int8_t>();
    case TagType::kShort:
      return reader.Read<std::int16_t>();
    case TagType::kInt:
      return reader.Read<std::int32_t>();
    case TagType::kLong:
      return reader.Read<std::int64_t>();
    case TagType::kFloat:
      return reader.Read<float>();
    case TagType::kDouble:
      return reader.Read<double>();
    case TagType::kByteArray: {
      auto arr =
          reader.ReadArraySpan<std::int8_t>(reader.limits().max_array_elements);
      if (!arr) return std::unexpected(arr.error());
      return *arr;
    }
    case TagType::kString: {
      auto str = reader.ReadStringView();
      if (!str) return std::unexpected(str.error());
      return *str;
    }
    case TagType::kList: {
      auto list_result = ParseList(reader);
      if (!list_result) return std::unexpected(list_result.error());
      return std::make_unique<NbtList>(std::move(*list_result));
    }
    case TagType::kCompound: {
      auto comp_result = ParseCompound(reader);
      if (!comp_result) return std::unexpected(comp_result.error());
      return std::make_unique<NbtCompound>(std::move(*comp_result));
    }
    case TagType::kIntArray: {
      auto len_res = reader.ReadLength(reader.limits().max_array_elements);
      if (!len_res) return std::unexpected(len_res.error());
      const std::size_t total = *len_res * sizeof(std::int32_t);
      auto raw_res = reader.PeekRaw(total);
      if (!raw_res) return std::unexpected(raw_res.error());
      reader.Advance(total);
      return *raw_res;
    }
    case TagType::kLongArray: {
      auto len_res = reader.ReadLength(reader.limits().max_array_elements);
      if (!len_res) return std::unexpected(len_res.error());
      const std::size_t total = *len_res * sizeof(std::int64_t);
      auto raw_res = reader.PeekRaw(total);
      if (!raw_res) return std::unexpected(raw_res.error());
      reader.Advance(total);
      return *raw_res;
    }
    default:
      [[unlikely]] return std::unexpected(
          reader.Error(ParseError::Code::kInvalidTagId));
  }
}

[[nodiscard]] ParseResult<NbtTag> ParseNbt(ByteReader& reader) {
  auto tag_raw = reader.Read<std::uint8_t>();
  if (!tag_raw) return std::unexpected(tag_raw.error());
  if (!IsValidTagType(*tag_raw)) [[unlikely]] {
    return std::unexpected(reader.Error(ParseError::Code::kInvalidTagId));
  }

  auto tag_type = static_cast<TagType>(*tag_raw);
  auto name = reader.ReadStringView();
  if (!name) return std::unexpected(name.error());

  auto payload_result = ParsePayload(reader, tag_type);
  if (!payload_result) return std::unexpected(payload_result.error());

  return NbtTag{tag_type, *name, std::move(*payload_result)};
}

}  // namespace fschema::base