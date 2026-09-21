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

#ifndef FSCHEMA_PARSER_NBT_READER_H_
#define FSCHEMA_PARSER_NBT_READER_H_

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "parser/error.h"
#include "parser/limits.h"
#include "parser/nbt/tag.h"

namespace fschema::parser::nbt {

  class ByteReader {
  public:
    ByteReader(std::span<const std::byte> buffer,
      const DecodeLimits& limits) noexcept;

    // State
    [[nodiscard]] std::size_t pos() const noexcept;
    [[nodiscard]] std::size_t remaining() const noexcept;
    [[nodiscard]] std::size_t depth() const noexcept;
    [[nodiscard]] const DecodeLimits& limits() const noexcept;

    void advance(std::size_t num_bytes) noexcept;
    void push_depth() noexcept;
    void pop_depth() noexcept;

    [[nodiscard]] ParseError Error(ParseError::Code code) const noexcept;

    // Scalar read (Big-Endian -> Host)
    template <typename T>
      requires std::integral<T> || std::floating_point<T>
    [[nodiscard]] ParseResult<T> Read() noexcept;

    // Batch scalar read
    // Read n T values and write to out. out.size() must be >= n.
    template <typename T>
      requires std::integral<T>
    [[nodiscard]] ParseResult<void> ReadBulk(
      std::size_t count, std::span<T> output) noexcept;

    // Length-prefixed read
    [[nodiscard]] ParseResult<std::size_t> ReadLength(
      std::size_t max_allowed) noexcept;

    // String read
    [[nodiscard]] ParseResult<std::string> ReadString();

    // Zero-copy string read
    [[nodiscard]] ParseResult<std::string_view> ReadStringView() noexcept;

    // Zero-copy array read (Returns Big-Endian data!)
    template <typename T>
      requires std::integral<T>
    [[nodiscard]] ParseResult<std::span<const T>> ReadArraySpan(
      std::size_t max_allowed_elements) noexcept;

    // Span truncate
    [[nodiscard]] ParseResult<std::span<const std::byte>> PeekRaw(
      std::size_t length) noexcept;

    // Compound / List 's entry read
    [[nodiscard]] ParseResult<TagType> ReadCompoundEntryHeader(
      std::string& name_out);

    // Zero-copy entry header read
    [[nodiscard]] ParseResult<TagType> ReadCompoundEntryHeaderView(
      std::string_view& name_out) noexcept;

    [[nodiscard]] ParseResult<std::pair<TagType, std::size_t>>
      ReadListHeader();

    [[nodiscard]] std::span<const std::byte> SpanFrom(
      std::size_t start) const noexcept;

  private:
    template <typename T>
      requires std::integral<T>
    [[nodiscard]] T ReadScalarBe() const noexcept;

    std::span<const std::byte> buffer_;
    std::size_t pos_ = 0;
    std::size_t depth_ = 0;
    const DecodeLimits& limits_;
  };

}  // namespace fschema::parser::nbt

#include "parser/nbt/reader-inl.h"

#endif  // FSCHEMA_PARSER_NBT_READER_H_