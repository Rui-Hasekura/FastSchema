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

#ifndef FSCHEMA_GEOMETRY_BLOCK_PROPS_H_
#define FSCHEMA_GEOMETRY_BLOCK_PROPS_H_

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "fschema/ir/types.h"

namespace fschema::geometry {

// Decoded block-state properties, e.g. {"facing":"north","half":"bottom"}.
// Built from ir::BlockState::raw_properties according to prop_encoding.
class BlockProps {
 public:
  BlockProps() = default;
  explicit BlockProps(const ir::BlockState& state);

  BlockProps(BlockProps&&) noexcept = default;
  BlockProps& operator=(BlockProps&&) noexcept = default;
  BlockProps(const BlockProps&) = default;
  BlockProps& operator=(const BlockProps&) = default;

  [[nodiscard]] std::optional<std::string_view> Get(
      std::string_view key) const noexcept;
  [[nodiscard]] bool Empty() const noexcept { return entries_.empty(); }
  [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }

  // Order-independent hash: equal (k,v) sets hash equally regardless of
  // insertion order. Used as the L2 cache key in the model resolver.
  [[nodiscard]] std::size_t StableHash() const noexcept;

 private:
  absl::flat_hash_map<std::string_view, std::string_view> entries_;
  void DecodeFromString(std::string_view src);
  void DecodeFromNbt(std::span<const std::byte> body);
};

}  // namespace fschema::geometry

#endif  // FSCHEMA_GEOMETRY_BLOCK_PROPS_H_
