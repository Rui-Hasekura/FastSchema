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

#ifndef FSCHEMA_BASE_NBT_WRITER_H_
#define FSCHEMA_BASE_NBT_WRITER_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "fschema/base/nbt_tag.h"

namespace fschema::base {

class NbtWriter {
 public:
  NbtWriter();
  explicit NbtWriter(std::size_t reserve_hint);
  ~NbtWriter();

  // Non-copyable, move-only
  NbtWriter(const NbtWriter&) = delete;
  NbtWriter& operator=(const NbtWriter&) = delete;
  NbtWriter(NbtWriter&&) noexcept = default;
  NbtWriter& operator=(NbtWriter&&) noexcept = default;

  // Root Compound Boundaries
  void BeginRootCompound(std::string_view name);
  void EndRootCompound();

  // Named Compound Fields
  void BeginCompoundField(std::string_view name);
  void EndCompoundField();

  void BeginListField(std::string_view name,
                      TagType elem_type,
                      std::size_t count);
  void EndListField();  // No-op for symmetry

  void WriteByteField(std::string_view name, std::int8_t value);
  void WriteShortField(std::string_view name, std::int16_t value);
  void WriteIntField(std::string_view name, std::int32_t value);
  void WriteLongField(std::string_view name, std::int64_t value);
  void WriteFloatField(std::string_view name, float value);
  void WriteDoubleField(std::string_view name, double value);
  void WriteStringField(std::string_view name, std::string_view value);

  void WriteByteArrayField(std::string_view name,
                           std::span<const std::int8_t> data);
  void WriteIntArrayField(std::string_view name,
                          std::span<const std::int32_t> data);
  void WriteLongArrayField(std::string_view name,
                           std::span<const std::int64_t> data);

  // Writes a complete field with pre-encoded payload (TagId + Name + payload).
  // Note: payload MUST INCLUDE internal length prefixes (for arrays/strings).
  void WriteRawField(std::string_view name,
                     TagType type,
                     std::span<const std::byte> payload);

  // Unnamed List Elements (Writes Payload Only)
  void WriteListElementByte(std::int8_t value);
  void WriteListElementShort(std::int16_t value);
  void WriteListElementInt(std::int32_t value);
  void WriteListElementLong(std::int64_t value);
  void WriteListElementFloat(float value);
  void WriteListElementDouble(double value);
  void WriteListElementString(std::string_view value);

  void BeginListElementCompound();
  void EndListElementCompound();

  // Pass-through raw payload for a single list element (no header)
  void WriteListElementRawPayload(std::span<const std::byte> payload);

  // Finalization
  // Moves and returns the constructed NBT buffer
  [[nodiscard]] std::vector<std::byte> Finalize() &&;

 private:
  void WriteTagId(TagType type);

  // Writes u16 (BE) length prefix + UTF-8 string
  void WriteName(std::string_view name);

  // Writes primitive scalar in Big-Endian
  template <typename T>
  void WriteScalarBE(T value);

  void EnsureCapacity(std::size_t extra);

  std::vector<std::byte> buffer_;
  std::size_t depth_ = 0;  // Current nesting level (Root / Compound / List)
};

}  // namespace fschema::base

#endif  // FSCHEMA_BASE_NBT_WRITER_H_