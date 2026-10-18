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

#ifndef FSCHEMA_REMAP_NBT_UTILS_H_
#define FSCHEMA_REMAP_NBT_UTILS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/nbt/tag.h"
#include "fschema/base/nbt/tree.h"

namespace fschema::remap::internal {

  // Deep-parses a raw compound body (children followed by a terminating
// TAG_End byte), matching the layout of BlockEntity::raw_nbt and
// Entity::raw_nbt.
[[nodiscard]] ParseResult<base::NbtCompound> MaterializeCompound(
    std::span<const std::byte> body);

// Serializes a compound back into the body form used by the IR (children
// followed by a terminating TAG_End). Field order is preserved.
[[nodiscard]] std::vector<std::byte> SerializeCompoundBody(
    const base::NbtCompound& compound);

// Returns true when `haystack` contains `pattern` as a byte substring.
[[nodiscard]] bool ContainsPattern(std::span<const std::byte> haystack,
                                   std::string_view pattern) noexcept;

// Finds a direct child by name. Returns nullptr when absent.
[[nodiscard]] base::NbtTag* FindChild(base::NbtCompound& c,
                                      std::string_view name) noexcept;
[[nodiscard]] const base::NbtTag* FindChild(const base::NbtCompound& c,
                                            std::string_view name) noexcept;

// Typed payload getters. Return nullptr on type mismatch.
[[nodiscard]] const base::NbtCompound* AsCompound(
    const base::NbtTag& tag) noexcept;
[[nodiscard]] const base::NbtList* AsList(const base::NbtTag& tag) noexcept;
[[nodiscard]] std::string_view AsString(const base::NbtTag& tag) noexcept;

// Mutable variants (used for subtree moves during transforms).
[[nodiscard]] base::NbtCompound* AsCompoundMutable(base::NbtTag& tag) noexcept;
[[nodiscard]] base::NbtList* AsListMutable(base::NbtTag& tag) noexcept;

// Builds scalar tag nodes.
[[nodiscard]] base::NbtTag MakeIntTag(std::string_view name,
                                      std::int32_t value) noexcept;
[[nodiscard]] base::NbtTag MakeShortTag(std::string_view name,
                                        std::int16_t value) noexcept;
[[nodiscard]] base::NbtTag MakeByteTag(std::string_view name,
                                       std::int8_t value) noexcept;

// Takes ownership of a compound subtree into a tag node.
[[nodiscard]] base::NbtTag MakeCompoundTag(
    std::string_view name,
    std::unique_ptr<base::NbtCompound> c) noexcept;

// Removes and returns a compound child by name. Returns nullptr when absent.
[[nodiscard]] std::unique_ptr<base::NbtCompound> TakeCompoundChild(
    base::NbtCompound& c,
    std::string_view name) noexcept;

// HideFlags bit values.
enum HideFlag : std::uint8_t {
  kHideEnchantments = 1,
  kHideAttributeModifiers = 2,
  kHideUnbreakable = 4,
  kHideCanDestroy = 8,
  kHideCanPlaceOn = 16,
  kHideAdditional = 32,
  kHideDyed = 64,
  kHideAll = 127,
};

// Removes a direct child by name (any type).
void RemoveChild(base::NbtCompound& parent, std::string_view name) noexcept;

// Builds a TAG_String node. The payload is a view; backing storage must
// outlive tree serialization.
[[nodiscard]] base::NbtTag StringTag(std::string_view name,
                                     std::string_view value) noexcept;

// Gets-or-creates a compound child; returns a mutable reference.
[[nodiscard]] base::NbtCompound& EnsureCompound(base::NbtCompound& parent,
                                                std::string_view name) noexcept;

// Gets-or-creates a list child; returns a mutable reference.
[[nodiscard]] base::NbtList& EnsureList(base::NbtCompound& parent,
                                        std::string_view name,
                                        base::TagType elem) noexcept;

// Appends an empty compound element to a list; returns a mutable reference.
[[nodiscard]] base::NbtCompound& AppendListElementCompound(
    base::NbtList& list) noexcept;

// Reads an int-ish scalar payload (byte / short / int).
[[nodiscard]] std::optional<std::int64_t> IntOf(
    const base::NbtTag& tag) noexcept;

// Reads a float / double scalar payload (widened to double).
[[nodiscard]] std::optional<double> FloatOf(const base::NbtTag& tag) noexcept;

// Reads a byte-as-boolean payload.
[[nodiscard]] std::optional<bool> BoolOf(const base::NbtTag& tag) noexcept;

// Builds long / float / double scalar tag nodes.
[[nodiscard]] base::NbtTag MakeLongTag(std::string_view name,
                                       std::int64_t value) noexcept;
[[nodiscard]] base::NbtTag MakeFloatTag(std::string_view name,
                                        float value) noexcept;
[[nodiscard]] base::NbtTag MakeDoubleTag(std::string_view name,
                                         double value) noexcept;

// Builds a TAG_Int_Array node. The payload variant holds a raw big-endian
// byte span, so the bytes are first serialized into `scratch` and the span
// points into it. `scratch` must outlive the tree that holds the node.
[[nodiscard]] base::NbtTag MakeIntArrayTag(
    std::string_view name,
    const std::int32_t* values,
    std::size_t n,
    std::deque<std::string>& scratch) noexcept;

// Builds a {X:int, Y:int, Z:int} compound from an IntArray[3] tag.
// Returns false when `src` is not an IntArray of exactly 3 elements.
[[nodiscard]] bool MakeBlockPosCompound(const base::NbtTag& src,
                                        base::NbtCompound& out);

// Parses a raw big-endian IntArray payload span into int32 values.
[[nodiscard]] std::vector<std::int32_t> ReadIntArray(
    std::span<const std::byte> raw) noexcept;

// Parses "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" into the four big-endian
// int32 words of the legacy UUID IntArray form. Returns nullopt on any
// shape mismatch.
[[nodiscard]] std::optional<std::array<std::int32_t, 4>> ParseUuidIntArray(
    std::string_view s) noexcept;

// Deterministic fallback for non-UUID modifier ids: 128-bit FNV-1a (two
// 64-bit lanes) forced into RFC-4122 version-3 shape. Same input always
// maps to the same UUID.
[[nodiscard]] std::array<std::int32_t, 4> HashUuidIntArray(
    std::string_view s) noexcept;

}  // namespace fschema::remap::internal

#endif