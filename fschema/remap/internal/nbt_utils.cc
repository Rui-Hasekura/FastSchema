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

#include "fschema/remap/internal/nbt_utils.h"

#include <bit>
#include <cstring>
#include <memory>
#include <utility>

#include "fschema/base/nbt/parse.h"
#include "fschema/base/nbt/reader.h"

namespace fschema::remap::internal {

ParseResult<base::NbtCompound> MaterializeCompound(
    std::span<const std::byte> body) {
  if (body.empty()) return base::NbtCompound{};
  base::ByteReader reader(body, base::DecodeLimits{});
  auto tree = base::ParseCompound(reader);
  if (!tree) return std::unexpected(tree.error());
  return tree;
}

namespace {

// Big-endian body writer. Emits the byte layout of a raw compound body:
// children followed by a terminating TAG_End.
class BodyWriter {
 public:
  void U8(std::uint8_t v) { buf_.push_back(std::byte{v}); }
  void U16(std::uint16_t v) {
    buf_.push_back(std::byte{static_cast<unsigned char>(v >> 8)});
    buf_.push_back(std::byte{static_cast<unsigned char>(v & 0xFF)});
  }
  void U32(std::uint32_t v) {
    buf_.push_back(std::byte{static_cast<unsigned char>(v >> 24)});
    buf_.push_back(std::byte{static_cast<unsigned char>((v >> 16) & 0xFF)});
    buf_.push_back(std::byte{static_cast<unsigned char>((v >> 8) & 0xFF)});
    buf_.push_back(std::byte{static_cast<unsigned char>(v & 0xFF)});
  }
  void U64(std::uint64_t v) {
    for (int i = 7; i >= 0; --i) {
      buf_.push_back(
          std::byte{static_cast<unsigned char>((v >> (8 * i)) & 0xFF)});
    }
  }
  void F32(float v) { U32(std::bit_cast<std::uint32_t>(v)); }
  void F64(double v) { U64(std::bit_cast<std::uint64_t>(v)); }

  void Str(std::string_view s) {
    U16(static_cast<std::uint16_t>(s.size()));
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    buf_.insert(buf_.end(), p, p + s.size());
  }

  void FieldHeader(base::TagType t, std::string_view name) {
    U8(static_cast<std::uint8_t>(t));
    Str(name);
  }

  void Payload(base::TagType t, const base::NbtPayload& p) {
    switch (t) {
      case base::TagType::kEnd:
        break;
      case base::TagType::kByte:
        U8(static_cast<std::uint8_t>(
            std::get_if<std::int8_t>(&p) ? *std::get_if<std::int8_t>(&p) : 0));
        break;
      case base::TagType::kShort: {
        const auto* v = std::get_if<std::int16_t>(&p);
        U16(static_cast<std::uint16_t>(v ? *v : 0));
        break;
      }
      case base::TagType::kInt: {
        const auto* v = std::get_if<std::int32_t>(&p);
        U32(static_cast<std::uint32_t>(v ? *v : 0));
        break;
      }
      case base::TagType::kLong: {
        const auto* v = std::get_if<std::int64_t>(&p);
        U64(static_cast<std::uint64_t>(v ? *v : 0));
        break;
      }
      case base::TagType::kFloat: {
        const auto* v = std::get_if<float>(&p);
        F32(v ? *v : 0.0f);
        break;
      }
      case base::TagType::kDouble: {
        const auto* v = std::get_if<double>(&p);
        F64(v ? *v : 0.0);
        break;
      }
      case base::TagType::kString: {
        const auto* v = std::get_if<std::string_view>(&p);
        Str(v ? *v : std::string_view{});
        break;
      }
      case base::TagType::kByteArray: {
        const auto* v = std::get_if<std::span<const std::int8_t>>(&p);
        const std::size_t n = v ? v->size() : 0;
        U32(static_cast<std::uint32_t>(n));
        if (n != 0) {
          const auto* p8 = reinterpret_cast<const std::byte*>(v->data());
          buf_.insert(buf_.end(), p8, p8 + n);
        }
        break;
      }
      case base::TagType::kIntArray:
      case base::TagType::kLongArray: {
        // IntArray / LongArray payloads are kept as raw big-endian byte
        // spans in the tree variant.
        const auto* v = std::get_if<std::span<const std::byte>>(&p);
        const std::size_t raw = v ? v->size() : 0;
        const std::size_t elem = t == base::TagType::kIntArray
                                     ? sizeof(std::int32_t)
                                     : sizeof(std::int64_t);
        U32(static_cast<std::uint32_t>(raw / elem));
        if (raw != 0) {
          const auto* pb = reinterpret_cast<const std::byte*>(v->data());
          buf_.insert(buf_.end(), pb, pb + raw);
        }
        break;
      }
      case base::TagType::kList: {
        const auto* v = std::get_if<std::unique_ptr<base::NbtList>>(&p);
        if (!v || !(*v)) {
          U8(0);
          U32(0);
          break;
        }
        U8(static_cast<std::uint8_t>((*v)->element_type));
        U32(static_cast<std::uint32_t>((*v)->children.size()));
        for (const auto& e : (*v)->children) {
          Payload((*v)->element_type, e);
        }
        break;
      }
      case base::TagType::kCompound: {
        const auto* v = std::get_if<std::unique_ptr<base::NbtCompound>>(&p);
        if (v && *v) {
          for (const auto& child : (*v)->children) {
            Field(child);
          }
        }
        U8(0);  // TAG_End terminator
        break;
      }
    }
  }

  void Field(const base::NbtTag& tag) {
    FieldHeader(tag.type, tag.name);
    Payload(tag.type, tag.payload);
  }

  [[nodiscard]] std::vector<std::byte> Take() && { return std::move(buf_); }

 private:
  std::vector<std::byte> buf_;
};

}  // namespace

std::vector<std::byte> SerializeCompoundBody(
    const base::NbtCompound& compound) {
  BodyWriter w;
  for (const auto& child : compound.children) {
    w.Field(child);
  }
  w.U8(0);  // terminating TAG_End
  return std::move(w).Take();
}

bool ContainsPattern(std::span<const std::byte> haystack,
                     std::string_view pattern) noexcept {
  if (pattern.empty() || haystack.size() < pattern.size()) return false;
  const auto* h = reinterpret_cast<const unsigned char*>(haystack.data());
  const auto* n = reinterpret_cast<const unsigned char*>(pattern.data());
  const std::size_t nlen = pattern.size();
  const std::size_t hlen = haystack.size();
  const unsigned char first = n[0];
  std::size_t i = 0;
  while (i + nlen <= hlen) {
    const void* hit = std::memchr(h + i, first, hlen - i - (nlen - 1));
    if (hit == nullptr) return false;
    i = static_cast<std::size_t>(static_cast<const unsigned char*>(hit) - h);
    if (std::memcmp(h + i, n, nlen) == 0) return true;
    ++i;
  }
  return false;
}

base::NbtTag* FindChild(base::NbtCompound& c, std::string_view name) noexcept {
  for (auto& child : c.children) {
    if (child.name == name) return &child;
  }
  return nullptr;
}

const base::NbtTag* FindChild(const base::NbtCompound& c,
                              std::string_view name) noexcept {
  for (const auto& child : c.children) {
    if (child.name == name) return &child;
  }
  return nullptr;
}

const base::NbtCompound* AsCompound(const base::NbtTag& tag) noexcept {
  if (tag.type != base::TagType::kCompound) return nullptr;
  const auto* p = std::get_if<std::unique_ptr<base::NbtCompound>>(&tag.payload);
  return (p && *p) ? p->get() : nullptr;
}

const base::NbtList* AsList(const base::NbtTag& tag) noexcept {
  if (tag.type != base::TagType::kList) return nullptr;
  const auto* p = std::get_if<std::unique_ptr<base::NbtList>>(&tag.payload);
  return (p && *p) ? p->get() : nullptr;
}

std::string_view AsString(const base::NbtTag& tag) noexcept {
  if (tag.type != base::TagType::kString) return {};
  const auto* p = std::get_if<std::string_view>(&tag.payload);
  return p ? *p : std::string_view{};
}

base::NbtCompound* AsCompoundMutable(base::NbtTag& tag) noexcept {
  if (tag.type != base::TagType::kCompound) return nullptr;
  auto* p = std::get_if<std::unique_ptr<base::NbtCompound>>(&tag.payload);
  return (p != nullptr && *p != nullptr) ? p->get() : nullptr;
}

base::NbtList* AsListMutable(base::NbtTag& tag) noexcept {
  if (tag.type != base::TagType::kList) return nullptr;
  auto* p = std::get_if<std::unique_ptr<base::NbtList>>(&tag.payload);
  return (p != nullptr && *p != nullptr) ? p->get() : nullptr;
}

base::NbtTag MakeIntTag(std::string_view name, std::int32_t value) noexcept {
  base::NbtTag t;
  t.type = base::TagType::kInt;
  t.name = name;
  t.payload = value;
  return t;
}

base::NbtTag MakeShortTag(std::string_view name, std::int16_t value) noexcept {
  base::NbtTag t;
  t.type = base::TagType::kShort;
  t.name = name;
  t.payload = value;
  return t;
}

base::NbtTag MakeByteTag(std::string_view name, std::int8_t value) noexcept {
  base::NbtTag t;
  t.type = base::TagType::kByte;
  t.name = name;
  t.payload = value;
  return t;
}

base::NbtTag MakeCompoundTag(std::string_view name,
                             std::unique_ptr<base::NbtCompound> c) noexcept {
  base::NbtTag t;
  t.type = base::TagType::kCompound;
  t.name = name;
  t.payload = std::move(c);
  return t;
}

std::unique_ptr<base::NbtCompound> TakeCompoundChild(
    base::NbtCompound& c,
    std::string_view name) noexcept {
  for (auto it = c.children.begin(); it != c.children.end(); ++it) {
    if (it->name == name && it->type == base::TagType::kCompound) {
      auto* p = std::get_if<std::unique_ptr<base::NbtCompound>>(&it->payload);
      if (p && *p) {
        auto out = std::move(*p);
        c.children.erase(it);
        return out;
      }
    }
  }
  return nullptr;
}

void RemoveChild(base::NbtCompound& parent, std::string_view name) noexcept {
  for (auto it = parent.children.begin(); it != parent.children.end(); ++it) {
    if (it->name == name) {
      parent.children.erase(it);
      return;
    }
  }
}

base::NbtTag StringTag(std::string_view name, std::string_view value) noexcept {
  base::NbtTag t;
  t.type = base::TagType::kString;
  t.name = name;
  t.payload = value;
  return t;
}

base::NbtCompound& EnsureCompound(base::NbtCompound& parent,
                                  std::string_view name) noexcept {
  for (auto& child : parent.children) {
    if (child.name == name && child.type == base::TagType::kCompound) {
      if (auto* p =
              std::get_if<std::unique_ptr<base::NbtCompound>>(&child.payload);
          p != nullptr && *p != nullptr) {
        return **p;
      }
    }
  }
  auto c = std::make_unique<base::NbtCompound>();
  base::NbtTag t;
  t.type = base::TagType::kCompound;
  t.name = name;
  auto& ref = *c;
  t.payload = std::move(c);
  parent.children.push_back(std::move(t));
  return ref;
}

base::NbtList& EnsureList(base::NbtCompound& parent,
                          std::string_view name,
                          base::TagType elem) noexcept {
  for (auto& child : parent.children) {
    if (child.name == name && child.type == base::TagType::kList) {
      if (auto* p = std::get_if<std::unique_ptr<base::NbtList>>(&child.payload);
          p != nullptr && *p != nullptr) {
        return **p;
      }
    }
  }
  auto list = std::make_unique<base::NbtList>();
  list->element_type = elem;
  base::NbtTag t;
  t.type = base::TagType::kList;
  t.name = name;
  auto& ref = *list;
  t.payload = std::move(list);
  parent.children.push_back(std::move(t));
  return ref;
}

base::NbtCompound& AppendListElementCompound(base::NbtList& list) noexcept {
  auto c = std::make_unique<base::NbtCompound>();
  auto& ref = *c;
  list.children.push_back(base::NbtPayload{std::move(c)});
  return ref;
}

std::optional<std::int64_t> IntOf(const base::NbtTag& tag) noexcept {
  if (tag.type == base::TagType::kInt) {
    if (const auto* v = std::get_if<std::int32_t>(&tag.payload)) return *v;
  } else if (tag.type == base::TagType::kShort) {
    if (const auto* v = std::get_if<std::int16_t>(&tag.payload)) return *v;
  } else if (tag.type == base::TagType::kByte) {
    if (const auto* v = std::get_if<std::int8_t>(&tag.payload)) return *v;
  }
  return std::nullopt;
}

std::optional<double> FloatOf(const base::NbtTag& tag) noexcept {
  if (tag.type == base::TagType::kDouble) {
    if (const auto* v = std::get_if<double>(&tag.payload)) return *v;
  } else if (tag.type == base::TagType::kFloat) {
    if (const auto* v = std::get_if<float>(&tag.payload)) {
      return static_cast<double>(*v);
    }
  }
  return std::nullopt;
}

std::optional<bool> BoolOf(const base::NbtTag& tag) noexcept {
  if (tag.type == base::TagType::kByte) {
    if (const auto* v = std::get_if<std::int8_t>(&tag.payload)) {
      return *v != 0;
    }
  }
  return std::nullopt;
}

base::NbtTag MakeLongTag(std::string_view name, std::int64_t value) noexcept {
  base::NbtTag t;
  t.type = base::TagType::kLong;
  t.name = name;
  t.payload = value;
  return t;
}

base::NbtTag MakeFloatTag(std::string_view name, float value) noexcept {
  base::NbtTag t;
  t.type = base::TagType::kFloat;
  t.name = name;
  t.payload = value;
  return t;
}

base::NbtTag MakeDoubleTag(std::string_view name, double value) noexcept {
  base::NbtTag t;
  t.type = base::TagType::kDouble;
  t.name = name;
  t.payload = value;
  return t;
}

base::NbtTag MakeIntArrayTag(std::string_view name,
                             const std::int32_t* values,
                             std::size_t n,
                             std::deque<std::string>& scratch) noexcept {
  // IntArray payloads are raw big-endian byte spans in the tree variant.
  // The serialized blob is stored in `scratch`, whose deque-backed
  // elements stay pointer-stable for the lifetime of the tree.
  std::string blob;
  blob.resize(n * 4);
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint32_t u = static_cast<std::uint32_t>(values[i]);
    blob[i * 4 + 0] = static_cast<char>((u >> 24) & 0xFFu);
    blob[i * 4 + 1] = static_cast<char>((u >> 16) & 0xFFu);
    blob[i * 4 + 2] = static_cast<char>((u >> 8) & 0xFFu);
    blob[i * 4 + 3] = static_cast<char>(u & 0xFFu);
  }
  scratch.push_back(std::move(blob));
  const std::string& stored = scratch.back();
  base::NbtTag t;
  t.type = base::TagType::kIntArray;
  t.name = name;
  t.payload = std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(stored.data()), stored.size());
  return t;
}

std::vector<std::int32_t> ReadIntArray(
    std::span<const std::byte> raw) noexcept {
  std::vector<std::int32_t> out;
  const std::size_t n = raw.size() / 4;
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const auto* p = reinterpret_cast<const unsigned char*>(raw.data()) + i * 4;
    const std::uint32_t u = (static_cast<std::uint32_t>(p[0]) << 24) |
                            (static_cast<std::uint32_t>(p[1]) << 16) |
                            (static_cast<std::uint32_t>(p[2]) << 8) |
                            static_cast<std::uint32_t>(p[3]);
    out.push_back(static_cast<std::int32_t>(u));
  }
  return out;
}

bool MakeBlockPosCompound(const base::NbtTag& src, base::NbtCompound& out) {
  if (src.type != base::TagType::kIntArray) return false;
  const auto* raw = std::get_if<std::span<const std::byte>>(&src.payload);
  if (raw == nullptr) return false;
  const std::vector<std::int32_t> ints = ReadIntArray(*raw);
  if (ints.size() != 3) return false;
  out.children.push_back(MakeIntTag("X", ints[0]));
  out.children.push_back(MakeIntTag("Y", ints[1]));
  out.children.push_back(MakeIntTag("Z", ints[2]));
  return true;
}

namespace {

int HexVal(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Digit counts per group of a canonical UUID (8-4-4-4-12).
constexpr std::size_t kUuidGroupLens[5] = {8, 4, 4, 4, 12};

}  // namespace

std::optional<std::array<std::int32_t, 4>> ParseUuidIntArray(
    std::string_view s) noexcept {
  // Layout check: 32 hex digits plus 4 hyphens.
  std::size_t hex = 0;
  std::size_t gi = 0;
  std::size_t group_digits = 0;
  for (const char c : s) {
    if (c == '-') {
      if (gi >= 5 || group_digits != kUuidGroupLens[gi]) return std::nullopt;
      ++gi;
      group_digits = 0;
      continue;
    }
    if (HexVal(c) < 0) return std::nullopt;
    ++group_digits;
    ++hex;
  }
  if (gi != 4 || group_digits != kUuidGroupLens[4] || hex != 32) {
    return std::nullopt;
  }
  // Parse 16 bytes big-endian into 4 int32 words.
  std::array<unsigned char, 16> bytes{};
  std::size_t bi = 0;
  for (const char c : s) {
    if (c == '-') continue;
    const int v = HexVal(c);
    if (bi % 2 == 0) {
      bytes[bi / 2] = static_cast<unsigned char>(v << 4);
    } else {
      bytes[bi / 2] = static_cast<unsigned char>(bytes[bi / 2] | v);
    }
    ++bi;
  }
  std::array<std::int32_t, 4> out{};
  for (std::size_t w = 0; w < 4; ++w) {
    const std::uint32_t u =
        (static_cast<std::uint32_t>(bytes[w * 4 + 0]) << 24) |
        (static_cast<std::uint32_t>(bytes[w * 4 + 1]) << 16) |
        (static_cast<std::uint32_t>(bytes[w * 4 + 2]) << 8) |
        static_cast<std::uint32_t>(bytes[w * 4 + 3]);
    out[w] = static_cast<std::int32_t>(u);
  }
  return out;
}

std::array<std::int32_t, 4> HashUuidIntArray(std::string_view s) noexcept {
  // Two 64-bit FNV-1a lanes produce 128 bits, then the RFC-4122 shape
  // (version nibble 3, variant 10) is forced so the value is a valid UUID.
  std::uint64_t hi = 1469598103934665603ull;
  std::uint64_t lo = 14695981039346656037ull;
  for (const char c : s) {
    const auto b = static_cast<unsigned char>(c);
    hi ^= b;
    hi *= 1099511628211ull;
    lo ^= static_cast<std::uint64_t>(b) << 8;
    lo *= 1099511628211ull;
  }
  // bytes 0..7 = hi (big-endian), bytes 8..15 = lo.
  std::array<unsigned char, 16> bytes{};
  for (int i = 0; i < 8; ++i) {
    bytes[i] = static_cast<unsigned char>((hi >> (8 * (7 - i))) & 0xFFu);
    bytes[8 + i] = static_cast<unsigned char>((lo >> (8 * (7 - i))) & 0xFFu);
  }
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0Fu) | 0x30u);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3Fu) | 0x80u);
  std::array<std::int32_t, 4> out{};
  for (std::size_t w = 0; w < 4; ++w) {
    const std::uint32_t u =
        (static_cast<std::uint32_t>(bytes[w * 4 + 0]) << 24) |
        (static_cast<std::uint32_t>(bytes[w * 4 + 1]) << 16) |
        (static_cast<std::uint32_t>(bytes[w * 4 + 2]) << 8) |
        static_cast<std::uint32_t>(bytes[w * 4 + 3]);
    out[w] = static_cast<std::int32_t>(u);
  }
  return out;
}

}  // namespace fschema::remap::internal