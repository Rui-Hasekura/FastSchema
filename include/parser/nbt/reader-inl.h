/*
 * Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
      using UintType = std::conditional_t<kSize == 4, std::uint32_t, std::uint64_t>;
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
  // Read n T values and write to out. out.size() must be >= n.
  template <typename T>
    requires std::integral<T>
  [[nodiscard]] ParseResult<void> ByteReader::ReadBulk(
    std::size_t count, std::span<T> output) noexcept {
    constexpr std::size_t kSize = sizeof(T);
    const std::size_t total = count * kSize;
    if (remaining() < total) [[unlikely]] {
      return std::unexpected(Error(ParseError::Code::Truncated));
    }
    if constexpr (kSize == 1) {
      std::memcpy(output.data(), buffer_.data() + pos_, total);
    }
    else {
      for (std::size_t i = 0; i < count; ++i) {
        std::array<std::byte, kSize> temp;
        std::memcpy(temp.data(), buffer_.data() + pos_ + i * kSize, kSize);
        if constexpr (std::endian::native == std::endian::little) {
          for (std::size_t j = 0; j < kSize / 2; ++j) {
            std::swap(temp[j], temp[kSize - 1 - j]);
          }
        }
        std::memcpy(&output[i], temp.data(), kSize);
      }
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
    else if constexpr (std::endian::native == std::endian::little) {
      T value = 0;
      for (std::size_t i = 0; i < kSize; ++i) {
        value = static_cast<T>(
          (value << 8) | static_cast<std::make_unsigned_t<T>>(buffer_[pos_ + i]));
      }
      return value;
    }
    else {
      T value;
      std::memcpy(&value, buffer_.data() + pos_, kSize);
      return value;
    }
  }

} // namespace fschema::parser::nbt

#endif // FSCHEMA_PARSER_NBT_READER_INL_H_