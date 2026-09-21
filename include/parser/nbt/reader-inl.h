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

#ifndef FSCHEMA_PARSER_NBT_READER_INL_H_
#define FSCHEMA_PARSER_NBT_READER_INL_H_

#include "parser/nbt/reader.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <type_traits>
#include <utility>

#include "parser/error.h"
#include "parser/nbt/simd_bswap.h"

namespace fschema::parser::nbt {

  // Scalar read (Big-Endian -> Host)
  template <typename T>
    requires std::integral<T> || std::floating_point<T>
  [[nodiscard]] ParseResult<T> ByteReader::Read() noexcept {
    constexpr std::size_t kSize = sizeof(T);
    if (remaining() < kSize) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::Truncated));
    }
    if constexpr (std::floating_point<T>) {
      using UintType =
        std::conditional_t<kSize == 4, std::uint32_t, std::uint64_t>;
      auto bits = ReadScalarBe<UintType>();
      pos_ += kSize;
      return std::bit_cast<T>(bits);
    }
    else {
      auto value = ReadScalarBe<T>();
      pos_ += kSize;
      return value;
    }
  }

  // Batch scalar read
  template <typename T>
    requires std::integral<T>
  [[nodiscard]] ParseResult<void> ByteReader::ReadBulk(
    std::size_t count, std::span<T> output) noexcept {
    constexpr std::size_t kSize = sizeof(T);
    const std::size_t total = count * kSize;
    if (remaining() < total) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::Truncated));
    }

    const std::byte* src = buffer_.data() + pos_;

    // Copy + SIMD Bswap
    if constexpr (std::endian::native == std::endian::little && kSize > 1) {
      if constexpr (std::is_same_v<T, std::int32_t>) {
        CopyAndBswap32(src, output.data(), count);
      }
      else if constexpr (std::is_same_v<T, std::int64_t>) {
        CopyAndBswap64(src, output.data(), count);
      }
      else {
        std::memcpy(output.data(), src, total);
        for (std::size_t i = 0; i < count; ++i) {
          output[i] = std::byteswap(output[i]);
        }
      }
    }
    else {
      std::memcpy(output.data(), src, total);
    }

    pos_ += total;
    return {};
  }

  template <typename T>
    requires std::integral<T>
  [[nodiscard]] T ByteReader::ReadScalarBe() const noexcept {
    constexpr std::size_t kSize = sizeof(T);
    if constexpr (kSize == 1) {
      return static_cast<T>(buffer_[pos_]);
    }
    else {
      T value;
      std::memcpy(&value, buffer_.data() + pos_, kSize);
      if constexpr (std::endian::native == std::endian::little) {
        return std::byteswap(value);
      }
      else {
        return value;
      }
    }
  }

  // Zero-copy array read implementation
  template <typename T>
    requires std::integral<T>
  [[nodiscard]] ParseResult<std::span<const T>> ByteReader::ReadArraySpan(
    std::size_t max_allowed_elements) noexcept {
    auto len = ReadLength(max_allowed_elements);
    if (!len) return std::unexpected(len.error());

    constexpr std::size_t kSize = sizeof(T);
    const std::size_t total = *len * kSize;
    if (remaining() < total) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::Truncated));
    }

    const auto* ptr = reinterpret_cast<const T*>(buffer_.data() + pos_);
    pos_ += total;
    return std::span<const T>(ptr, *len);
  }

}  // namespace fschema::parser::nbt

#endif  // FSCHEMA_PARSER_NBT_READER_INL_H_