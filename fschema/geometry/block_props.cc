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

#include "fschema/geometry/block_props.h"

#include <cstdint>

#include "fschema/base/hash.h"
#include "fschema/base/nbt/reader.h"
#include "fschema/base/nbt/tag.h"

namespace fschema::geometry {

BlockProps::BlockProps(const ir::BlockState& state) {
  switch (state.prop_encoding) {
    case ir::PropertyEncoding::kNone:
      return;
    case ir::PropertyEncoding::kString:
      DecodeFromString(std::string_view(
          reinterpret_cast<const char*>(state.raw_properties.data()),
          state.raw_properties.size()));
      return;
    case ir::PropertyEncoding::kNbt:
      DecodeFromNbt(state.raw_properties);
      return;
  }
}

std::optional<std::string_view> BlockProps::Get(
    std::string_view key) const noexcept {
  auto it = entries_.find(key);
  if (it == entries_.end()) return std::nullopt;
  return it->second;
}

std::size_t BlockProps::StableHash() const noexcept {
  // Order-independent hash: XOR of per-entry XXH3 hashes.
  std::size_t acc = 0;
  for (const auto& [k, v] : entries_) {
    std::uint64_t h = base::Hash64WithSeed(k.data(), k.size());
    h = base::Hash64WithSeed(v.data(), v.size(), h);
    const std::size_t shifted = (static_cast<std::size_t>(h) << 1) |
                                (static_cast<std::size_t>(h) >> 63);
    acc ^= shifted;
  }
  return acc;
}

void BlockProps::DecodeFromString(std::string_view src) {
  // Format: "k1=v1,k2=v2,..." with no trailing comma. Empty -> empty map.
  if (src.empty()) return;
  std::size_t pair_start = 0;
  while (pair_start <= src.size()) {
    const std::size_t comma = src.find(',', pair_start);
    std::string_view pair =
        src.substr(pair_start,
                   comma == std::string_view::npos ? std::string_view::npos
                                                   : comma - pair_start);
    if (!pair.empty()) {
      const std::size_t eq = pair.find('=');
      if (eq != std::string_view::npos) {
        entries_.emplace(pair.substr(0, eq), pair.substr(eq + 1));
      }
    }
    if (comma == std::string_view::npos) break;
    pair_start = comma + 1;
  }
}

void BlockProps::DecodeFromNbt(std::span<const std::byte> body) {
  // Litematica block properties: NBT compound with all-string children.
  base::DecodeLimits limits{};
  base::ByteReader reader(body, limits);

  for (;;) {
    std::string_view name;
    auto header = reader.ReadCompoundEntryHeaderView(name);
    if (!header) {
      entries_.clear();
      return;
    }
    if (*header == base::TagType::kEnd) break;

    if (*header != base::TagType::kString) {
      // Non-string payloads are not produced by any supported source.
      entries_.clear();
      return;
    }
    auto value = reader.ReadStringView();
    if (!value) {
      entries_.clear();
      return;
    }
    entries_.emplace(name, *value);
  }
}

}  // namespace fschema::geometry
