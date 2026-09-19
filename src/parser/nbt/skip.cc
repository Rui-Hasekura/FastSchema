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

#include "parser/nbt/skip.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

#include "parser/error.h"
#include "parser/nbt/reader.h"
#include "parser/nbt/tag.h"

namespace fschema::parser::nbt {

  [[nodiscard]] ParseResult<void> SkipCompound(ByteReader& reader) {
    reader.push_depth();
    if (reader.depth() > reader.limits().max_nbt_depth) [[unlikely]] {
      reader.pop_depth();
      return std::unexpected(reader.Error(ParseError::Code::DepthLimitExceeded));
    }
    for (;;) {
      std::string name;
      auto tag = reader.ReadCompoundEntryHeader(name);
      if (!tag) {
        reader.pop_depth();
        return std::unexpected(tag.error());
      }
      if (*tag == TagType::End) {
        break;
      }
      auto skip_result = SkipPayload(reader, *tag);
      if (!skip_result) {
        reader.pop_depth();
        return std::unexpected(skip_result.error());
      }
    }
    reader.pop_depth();
    return {};
  }

  [[nodiscard]] ParseResult<void> SkipList(ByteReader& reader) {
    auto header = reader.ReadListHeader();
    if (!header) {
      return std::unexpected(header.error());
    }
    auto [elem_type, count] = *header;
    if (elem_type == TagType::End || count == 0) {
      return {};
    }

    reader.push_depth();
    if (reader.depth() > reader.limits().max_nbt_depth) [[unlikely]] {
      reader.pop_depth();
      return std::unexpected(reader.Error(ParseError::Code::DepthLimitExceeded));
    }
    for (std::size_t i = 0; i < count; ++i) {
      auto skip_result = SkipPayload(reader, elem_type);
      if (!skip_result) {
        reader.pop_depth();
        return std::unexpected(skip_result.error());
      }
    }
    reader.pop_depth();
    return {};
  }

  [[nodiscard]] ParseResult<void> SkipScalar(ByteReader& reader, TagType tag_type) {
    const auto size = FixedPayloadSize(tag_type);
    if (reader.remaining() < size) [[unlikely]] {
      return std::unexpected(reader.Error(ParseError::Code::Truncated));
    }
    reader.advance(size);
    return {};
  }

  [[nodiscard]] ParseResult<void> SkipString(ByteReader& reader) {
    auto len_raw = reader.Read<uint16_t>();
    if (!len_raw) {
      return std::unexpected(len_raw.error());
    }
    const auto len = static_cast<std::size_t>(*len_raw);
    if (reader.remaining() < len) [[unlikely]] {
      return std::unexpected(reader.Error(ParseError::Code::Truncated));
    }
    reader.advance(len);
    return {};
  }

  [[nodiscard]] ParseResult<void> SkipArray(ByteReader& reader, TagType tag_type) {
    const auto elem_size = ElementSize(tag_type);
    auto len = reader.ReadLength(reader.limits().max_array_elements);
    if (!len) {
      return std::unexpected(len.error());
    }
    const auto total = (*len) * elem_size;
    if (reader.remaining() < total) [[unlikely]] {
      return std::unexpected(reader.Error(ParseError::Code::Truncated));
    }
    reader.advance(total);
    return {};
  }

  [[nodiscard]] ParseResult<void> SkipPayload(ByteReader& reader, TagType tag_type) {
    switch (tag_type) {
    case TagType::End:
      return {};
    case TagType::Byte:
    case TagType::Short:
    case TagType::Int:
    case TagType::Long:
    case TagType::Float:
    case TagType::Double:
      return SkipScalar(reader, tag_type);
    case TagType::ByteArray:
    case TagType::IntArray:
    case TagType::LongArray:
      return SkipArray(reader, tag_type);
    case TagType::String:
      return SkipString(reader);
    case TagType::List:
      return SkipList(reader);
    case TagType::Compound:
      return SkipCompound(reader);
    default: {
      [[unlikely]] return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
    }
    }
  }

} // namespace fschema::parser::nbt