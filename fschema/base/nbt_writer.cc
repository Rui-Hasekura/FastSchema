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

#include "fschema/base/nbt_writer.h"

#include <bit>
#include <cassert>
#include <cstring>
#include <vector>

#include "fschema/base/byte_order.h"

namespace fschema::base {

NbtWriter::NbtWriter() {
  // Pre-allocate 64 KB buffer to reduce reallocations during initial writes
  buffer_.reserve(1 << 16);
}

NbtWriter::~NbtWriter() = default;

void NbtWriter::EnsureCapacity(std::size_t extra) {
  if (buffer_.size() + extra > buffer_.capacity()) {
    // 1.5x growth strategy or exact required capacity (whichever is larger)
    buffer_.reserve(std::max(buffer_.capacity() * 2, buffer_.size() + extra));
  }
}

void NbtWriter::WriteTagId(TagType type) {
  EnsureCapacity(1);
  buffer_.push_back(static_cast<std::byte>(type));
}

void NbtWriter::WriteName(std::string_view name) {
  if (name.size() > 65535) [[unlikely]] {
    name = name.substr(0, 65535);
  }
  const std::size_t total = 2 + name.size();
  const std::size_t old = buffer_.size();
  EnsureCapacity(total);
  buffer_.resize(old + total);
  const std::uint16_t len_be =
      std::byteswap(static_cast<std::uint16_t>(name.size()));
  std::memcpy(buffer_.data() + old, &len_be, 2);
  if (!name.empty()) {
    std::memcpy(buffer_.data() + old + 2, name.data(), name.size());
  }
}

template <typename T>
void NbtWriter::WriteScalarBE(T value) {
  if constexpr (std::endian::native == std::endian::little && sizeof(T) > 1) {
    // Treat integral and float types uniformly via bit_cast to same-sized
    // uint, byteswap, then bit_cast back. uint8_t case is excluded by the
    // sizeof(T) > 1 branch above; byteswap on uint8_t is a no-op anyway.
    using Uint = std::conditional_t<
        sizeof(T) == 2,
        std::uint16_t,
        std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>;
    Uint bits = std::bit_cast<Uint>(value);
    bits = std::byteswap(bits);
    value = std::bit_cast<T>(bits);
  }
  const std::size_t old = buffer_.size();
  EnsureCapacity(sizeof(T));
  buffer_.resize(old + sizeof(T));
  std::memcpy(buffer_.data() + old, &value, sizeof(T));
}

// Explicit template instantiations
template void NbtWriter::WriteScalarBE<std::int8_t>(std::int8_t);
template void NbtWriter::WriteScalarBE<std::uint8_t>(std::uint8_t);
template void NbtWriter::WriteScalarBE<std::int16_t>(std::int16_t);
template void NbtWriter::WriteScalarBE<std::uint16_t>(std::uint16_t);
template void NbtWriter::WriteScalarBE<std::int32_t>(std::int32_t);
template void NbtWriter::WriteScalarBE<std::uint32_t>(std::uint32_t);
template void NbtWriter::WriteScalarBE<std::int64_t>(std::int64_t);
template void NbtWriter::WriteScalarBE<std::uint64_t>(std::uint64_t);
template void NbtWriter::WriteScalarBE<float>(float);
template void NbtWriter::WriteScalarBE<double>(double);

// Root Boundaries
void NbtWriter::BeginRootCompound(std::string_view name) {
  WriteTagId(TagType::Compound);
  WriteName(name);
  depth_++;
}

void NbtWriter::EndRootCompound() {
  WriteTagId(TagType::End);
  depth_--;
}

// Compound Fields
void NbtWriter::BeginCompoundField(std::string_view name) {
  WriteTagId(TagType::Compound);
  WriteName(name);
  depth_++;
}

void NbtWriter::EndCompoundField() {
  WriteTagId(TagType::End);
  depth_--;
}

void NbtWriter::BeginListField(std::string_view name,
                               TagType elem_type,
                               std::size_t count) {
  WriteTagId(TagType::List);
  WriteName(name);
  WriteScalarBE<std::uint8_t>(static_cast<std::uint8_t>(elem_type));
  WriteScalarBE<std::int32_t>(static_cast<std::int32_t>(count));
}

void NbtWriter::EndListField() {
  // No-op
}

void NbtWriter::WriteByteField(std::string_view name, std::int8_t value) {
  WriteTagId(TagType::Byte);
  WriteName(name);
  WriteScalarBE<std::int8_t>(value);
}

void NbtWriter::WriteShortField(std::string_view name, std::int16_t value) {
  WriteTagId(TagType::Short);
  WriteName(name);
  WriteScalarBE<std::int16_t>(value);
}

void NbtWriter::WriteIntField(std::string_view name, std::int32_t value) {
  WriteTagId(TagType::Int);
  WriteName(name);
  WriteScalarBE<std::int32_t>(value);
}

void NbtWriter::WriteLongField(std::string_view name, std::int64_t value) {
  WriteTagId(TagType::Long);
  WriteName(name);
  WriteScalarBE<std::int64_t>(value);
}

void NbtWriter::WriteFloatField(std::string_view name, float value) {
  WriteTagId(TagType::Float);
  WriteName(name);
  WriteScalarBE<float>(value);
}

void NbtWriter::WriteDoubleField(std::string_view name, double value) {
  WriteTagId(TagType::Double);
  WriteName(name);
  WriteScalarBE<double>(value);
}

void NbtWriter::WriteStringField(std::string_view name,
                                 std::string_view value) {
  WriteTagId(TagType::String);
  WriteName(name);
  WriteName(value);  // NBT String layout is identical to Tag Name (u16 len +
                     // UTF-8 payload)
}

void NbtWriter::WriteByteArrayField(std::string_view name,
                                    std::span<const std::int8_t> data) {
  WriteTagId(TagType::ByteArray);
  WriteName(name);
  WriteScalarBE<std::int32_t>(static_cast<std::int32_t>(data.size()));

  std::size_t old_size = buffer_.size();
  EnsureCapacity(data.size());
  buffer_.resize(old_size + data.size());
  if (!data.empty()) {
    std::memcpy(buffer_.data() + old_size, data.data(), data.size());
  }
}

void NbtWriter::WriteIntArrayField(std::string_view name,
                                   std::span<const std::int32_t> data) {
  WriteTagId(TagType::IntArray);
  WriteName(name);
  WriteScalarBE<std::int32_t>(static_cast<std::int32_t>(data.size()));

  std::size_t bytes = data.size() * 4;
  std::size_t old_size = buffer_.size();
  EnsureCapacity(bytes);
  buffer_.resize(old_size + bytes);

  if (!data.empty()) {
    const std::byte* src = reinterpret_cast<const std::byte*>(data.data());
    std::int32_t* dst =
        reinterpret_cast<std::int32_t*>(buffer_.data() + old_size);
    CopyAndBswap32(src, dst, data.size());
  }
}

void NbtWriter::WriteLongArrayField(std::string_view name,
                                    std::span<const std::int64_t> data) {
  WriteTagId(TagType::LongArray);
  WriteName(name);
  WriteScalarBE<std::int32_t>(static_cast<std::int32_t>(data.size()));

  std::size_t bytes = data.size() * 8;
  std::size_t old_size = buffer_.size();
  EnsureCapacity(bytes);
  buffer_.resize(old_size + bytes);

  if (!data.empty()) {
    const std::byte* src = reinterpret_cast<const std::byte*>(data.data());
    std::int64_t* dst =
        reinterpret_cast<std::int64_t*>(buffer_.data() + old_size);
    CopyAndBswap64(src, dst, data.size());
  }
}

void NbtWriter::WriteRawField(std::string_view name,
                              TagType type,
                              std::span<const std::byte> payload) {
  WriteTagId(type);
  WriteName(name);

  std::size_t old_size = buffer_.size();
  EnsureCapacity(payload.size());
  buffer_.resize(old_size + payload.size());
  if (!payload.empty()) {
    std::memcpy(buffer_.data() + old_size, payload.data(), payload.size());
  }
}

// List Elements
void NbtWriter::WriteListElementByte(std::int8_t value) {
  WriteScalarBE<std::int8_t>(value);
}
void NbtWriter::WriteListElementShort(std::int16_t value) {
  WriteScalarBE<std::int16_t>(value);
}
void NbtWriter::WriteListElementInt(std::int32_t value) {
  WriteScalarBE<std::int32_t>(value);
}
void NbtWriter::WriteListElementLong(std::int64_t value) {
  WriteScalarBE<std::int64_t>(value);
}
void NbtWriter::WriteListElementFloat(float value) {
  WriteScalarBE<float>(value);
}
void NbtWriter::WriteListElementDouble(double value) {
  WriteScalarBE<double>(value);
}
void NbtWriter::WriteListElementString(std::string_view value) {
  WriteName(value);
}

void NbtWriter::BeginListElementCompound() { depth_++; }
void NbtWriter::EndListElementCompound() {
  WriteTagId(TagType::End);
  depth_--;
}

void NbtWriter::WriteListElementRawPayload(std::span<const std::byte> payload) {
  std::size_t old_size = buffer_.size();
  EnsureCapacity(payload.size());
  buffer_.resize(old_size + payload.size());
  if (!payload.empty()) {
    std::memcpy(buffer_.data() + old_size, payload.data(), payload.size());
  }
}

// Finalization
std::vector<std::byte> NbtWriter::Finalize() && {
  assert(depth_ == 0 &&
         "NbtWriter::Finalize called with unclosed compounds/lists; "
         "every BeginRootCompound/BeginCompoundField/"
         "BeginListElementCompound must have a matching End* call");
  return std::move(buffer_);
}

}  // namespace fschema::base