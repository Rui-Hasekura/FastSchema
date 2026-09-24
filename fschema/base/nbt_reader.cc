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

#include "fschema/base/nbt_reader.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/base/nbt_tag.h"

namespace fschema::base {

  ByteReader::ByteReader(std::span<const std::byte> buffer,
    const DecodeLimits& limits) noexcept
    : buffer_(buffer), limits_(limits) {
  }

  // State
  [[nodiscard]] std::size_t ByteReader::pos() const noexcept { return pos_; }
  [[nodiscard]] std::size_t ByteReader::remaining() const noexcept {
    return buffer_.size() - pos_;
  }
  [[nodiscard]] std::size_t ByteReader::depth() const noexcept { return depth_; }
  [[nodiscard]] const DecodeLimits& ByteReader::limits() const noexcept {
    return limits_;
  }

  void ByteReader::advance(std::size_t num_bytes) noexcept { pos_ += num_bytes; }
  void ByteReader::push_depth() noexcept { ++depth_; }
  void ByteReader::pop_depth() noexcept { --depth_; }

  [[nodiscard]] ParseError ByteReader::Error(ParseError::Code code) const
    noexcept {
    return ParseError{ code, std::string{}, pos_ };
  }

  // Length-prefixed read
  [[nodiscard]] ParseResult<std::size_t> ByteReader::ReadLength(
    std::size_t max_allowed) noexcept {
    auto raw = Read<std::int32_t>();
    if (!raw) {
      return std::unexpected(raw.error());
    }
    const auto length = static_cast<std::int64_t>(*raw);
    if (length < 0) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::NegativeLength));
    }
    if (static_cast<std::uint64_t>(length) > max_allowed) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::OversizedPayload));
    }
    return static_cast<std::size_t>(length);
  }

  // String read
  [[nodiscard]] ParseResult<std::string> ByteReader::ReadString() {
    auto raw_len = Read<std::uint16_t>();
    if (!raw_len) {
      return std::unexpected(raw_len.error());
    }
    const auto length = static_cast<std::size_t>(*raw_len);
    if (length > limits_.max_string_bytes) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::OversizedPayload));
    }
    if (remaining() < length) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::Truncated));
    }
    std::string str(reinterpret_cast<const char*>(buffer_.data() + pos_), length);
    pos_ += length;
    return str;
  }

  // Zero-copy string read
  [[nodiscard]] ParseResult<std::string_view> ByteReader::ReadStringView()
    noexcept {
    auto raw_len = Read<std::uint16_t>();
    if (!raw_len) {
      return std::unexpected(raw_len.error());
    }
    const auto length = static_cast<std::size_t>(*raw_len);
    if (length > limits_.max_string_bytes) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::OversizedPayload));
    }
    if (remaining() < length) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::Truncated));
    }
    std::string_view sv(reinterpret_cast<const char*>(buffer_.data() + pos_),
      length);
    pos_ += length;
    return sv;
  }

  // Span truncate
  [[nodiscard]] ParseResult<std::span<const std::byte>> ByteReader::PeekRaw(
    std::size_t length) noexcept {
    if (remaining() < length) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::Truncated));
    }
    return buffer_.subspan(pos_, length);
  }

  // Compound / List entry read
  [[nodiscard]] ParseResult<TagType> ByteReader::ReadCompoundEntryHeader(
    std::string& name_out) {
    auto raw_tag = Read<std::uint8_t>();
    if (!raw_tag) {
      return std::unexpected(raw_tag.error());
    }
    const auto tag = static_cast<TagType>(*raw_tag);
    if (!IsValidTagType(*raw_tag)) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::InvalidTagId));
    }
    if (tag == TagType::End) {
      return tag;
    }

    auto name = ReadString();
    if (!name) {
      return std::unexpected(name.error());
    }
    name_out = std::move(*name);
    return tag;
  }

  // Zero-copy entry header read
  [[nodiscard]] ParseResult<TagType> ByteReader::ReadCompoundEntryHeaderView(
    std::string_view& name_out) noexcept {
    auto raw_tag = Read<std::uint8_t>();
    if (!raw_tag) {
      return std::unexpected(raw_tag.error());
    }
    const auto tag = static_cast<TagType>(*raw_tag);
    if (!IsValidTagType(*raw_tag)) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::InvalidTagId));
    }
    if (tag == TagType::End) {
      return tag;
    }

    auto name_res = ReadStringView();
    if (!name_res) {
      return std::unexpected(name_res.error());
    }
    name_out = *name_res;
    return tag;
  }

  [[nodiscard]] ParseResult<std::pair<TagType, std::size_t>>
    ByteReader::ReadListHeader() {
    auto raw_tag = Read<std::uint8_t>();
    if (!raw_tag) {
      return std::unexpected(raw_tag.error());
    }
    if (!IsValidTagType(*raw_tag)) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::InvalidTagId));
    }
    const auto element_type = static_cast<TagType>(*raw_tag);

    auto raw_len = Read<std::int32_t>();
    if (!raw_len) {
      return std::unexpected(raw_len.error());
    }
    const auto length = static_cast<std::int64_t>(*raw_len);
    if (length < 0) {
      if (length == -1) {
        return std::make_pair(element_type, std::size_t{ 0 });
      }
      return std::unexpected(Error(ParseError::Code::NegativeLength));
    }
    return std::make_pair(element_type, static_cast<std::size_t>(length));
  }

  [[nodiscard]] std::span<const std::byte> ByteReader::SpanFrom(
    std::size_t start) const noexcept {
    return buffer_.subspan(start, pos_ - start);
  }

}  // namespace fschema::base