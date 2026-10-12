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

#include "fschema/base/nbt/skip.h"

#include <cstdint>
#include <expected>
#include <string_view>

#include "fschema/base/error.h"
#include "fschema/base/nbt/reader.h"
#include "fschema/base/nbt/scope.h"
#include "fschema/base/nbt/tag.h"

namespace fschema::base {

[[nodiscard]] ParseResult<void> SkipCompound(ByteReader& reader) {
  return ForEachCompoundField(
      reader,
      [&](std::string_view /*name=*/, TagType tag) -> ParseResult<void> {
        return SkipPayload(reader, tag);
      });
}

ParseResult<void> SkipList(ByteReader& reader) {
  auto header = reader.ReadListHeader();
  if (!header) return std::unexpected(header.error());
  auto [elem_type, count] = *header;
  if (elem_type == TagType::kEnd || count == 0) return {};

  DepthGuard guard(reader);
  if (reader.depth() > reader.limits().max_nbt_depth) [[unlikely]] {
    return std::unexpected(reader.Error(ParseError::Code::kDepthLimitExceeded));
  }
  for (std::size_t i = 0; i < count; ++i) {
    auto r = SkipPayload(reader, elem_type);
    if (!r) return std::unexpected(r.error());
  }
  return {};
}

[[nodiscard]] ParseResult<void> SkipScalar(ByteReader& reader,
                                           TagType tag_type) {
  const auto size = FixedPayloadSize(tag_type);
  if (reader.Remaining() < size) [[unlikely]] {
    return std::unexpected(reader.Error(ParseError::Code::kTruncated));
  }
  reader.Advance(size);
  return {};
}

[[nodiscard]] ParseResult<void> SkipString(ByteReader& reader) {
  auto len_raw = reader.Read<std::uint16_t>();
  if (!len_raw) {
    return std::unexpected(len_raw.error());
  }
  const auto len = static_cast<std::size_t>(*len_raw);
  if (reader.Remaining() < len) [[unlikely]] {
    return std::unexpected(reader.Error(ParseError::Code::kTruncated));
  }
  reader.Advance(len);
  return {};
}

[[nodiscard]] ParseResult<void> SkipArray(ByteReader& reader,
                                          TagType tag_type) {
  const auto elem_size = ElementSize(tag_type);
  auto len = reader.ReadLength(reader.limits().max_array_elements);
  if (!len) {
    return std::unexpected(len.error());
  }
  const auto total = (*len) * elem_size;
  if (reader.Remaining() < total) [[unlikely]] {
    return std::unexpected(reader.Error(ParseError::Code::kTruncated));
  }
  reader.Advance(total);
  return {};
}

[[nodiscard]] ParseResult<void> SkipPayload(ByteReader& reader,
                                            TagType tag_type) {
  switch (tag_type) {
    case TagType::kEnd:
      return {};
    case TagType::kByte:
    case TagType::kShort:
    case TagType::kInt:
    case TagType::kLong:
    case TagType::kFloat:
    case TagType::kDouble:
      return SkipScalar(reader, tag_type);
    case TagType::kByteArray:
    case TagType::kIntArray:
    case TagType::kLongArray:
      return SkipArray(reader, tag_type);
    case TagType::kString:
      return SkipString(reader);
    case TagType::kList:
      return SkipList(reader);
    case TagType::kCompound:
      return SkipCompound(reader);
    default: [[unlikely]] {
      return std::unexpected(
          reader.Error(ParseError::Code::kInvalidTagId));
    }
  }
}

}  // namespace fschema::base