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

#ifndef FSCHEMA_FILTERS_NBT_FILTER_H_
#define FSCHEMA_FILTERS_NBT_FILTER_H_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/base/nbt/reader.h"
#include "fschema/base/nbt/skip.h"
#include "fschema/base/nbt/tag.h"
#include "fschema/filters/pos.h"
#include "fschema/ir/types.h"

namespace fschema::filters {

// NBT Query DSL

enum class NbtPathNodeType : std::uint8_t {
  kKey,    // Navigate into a Compound by key
  kIndex,  // Navigate into a List by index
};

struct NbtPathNode {
  NbtPathNodeType type;
  std::string_view key;   // Used if type == kKey
  std::size_t index = 0;  // Used if type == kIndex
};

enum class NbtOp : std::uint8_t {
  kExists,    // Path exists
  kEqString,  // Target is a String equal to str_val
  kEqInt,     // Target is an integer equal to int_val
  kGtInt,     // Target is an integer greater than int_val
  kLtInt,     // Target is an integer less than int_val
};

struct NbtQuery {
  std::vector<NbtPathNode> path;
  NbtOp op = NbtOp::kExists;
  std::string_view str_val;
  std::int64_t int_val = 0;
};

/// Filters blocks whose BlockEntity NBT payload satisfies an `NbtQuery`.
class NbtFilter {
 public:
  NbtFilter(const ir::Region& r, const NbtQuery& query) {
    matched_positions_.reserve(r.block_entities.size());
    for (const auto& be : r.block_entities) {
      if (EvalQuery(be.raw_nbt, query)) {
        LocalPos p;
        p.x = be.block_position[0] - r.bounds.origin[0];
        p.y = be.block_position[1] - r.bounds.origin[1];
        p.z = be.block_position[2] - r.bounds.origin[2];
        matched_positions_.insert(p);
      }
    }
  }

  [[nodiscard]] bool operator()(LocalPos p,
                                std::uint16_t /*palette_idx=*/) const noexcept {
    return matched_positions_.contains(p);
  }

 private:
  // Navigates to a Compound child by key,
  // leaving `reader` at the child's payload.
  [[nodiscard]] static bool NavigateToChild(base::ByteReader& reader,
                                            std::string_view key,
                                            base::TagType& out_type) {
    for (;;) {
      std::string_view name;
      auto tag = reader.ReadCompoundEntryHeaderView(name);
      if (!tag || *tag == base::TagType::End) return false;
      if (name == key) {
        out_type = *tag;
        return true;
      }
      if (!base::SkipPayload(reader, *tag)) return false;
    }
  }

  // Navigates to a List element by index,
  // leaving `reader` at the element's payload.
  [[nodiscard]] static bool NavigateToIndex(base::ByteReader& reader,
                                            std::size_t index,
                                            base::TagType& out_type) {
    auto header = reader.ReadListHeader();
    if (!header) return false;
    auto [elem_type, count] = *header;
    out_type = elem_type;
    if (index >= count) return false;
    for (std::size_t i = 0; i < index; ++i) {
      if (!base::SkipPayload(reader, elem_type)) return false;
    }
    return true;
  }

  // Evaluates query constraints against raw NBT payload.
  [[nodiscard]] static bool EvalQuery(std::span<const std::byte> nbt,
                                      const NbtQuery& query) {
    if (nbt.empty()) return false;
    base::ByteReader reader(nbt, base::DecodeLimits{});
    base::TagType current_type = base::TagType::Compound;

    for (const auto& node : query.path) {
      if (node.type == NbtPathNodeType::kKey) {
        if (current_type != base::TagType::Compound) return false;
        if (!NavigateToChild(reader, node.key, current_type)) return false;
      } else if (node.type == NbtPathNodeType::kIndex) {
        if (current_type != base::TagType::List) return false;
        if (!NavigateToIndex(reader, node.index, current_type)) return false;
      }
    }

    switch (query.op) {
      case NbtOp::kExists:
        return true;
      case NbtOp::kEqString:
        if (current_type != base::TagType::String) return false;
        if (auto v = reader.ReadStringView()) return *v == query.str_val;
        return false;
      case NbtOp::kEqInt:
      case NbtOp::kGtInt:
      case NbtOp::kLtInt: {
        std::int64_t val = 0;
        bool success = false;
        if (current_type == base::TagType::Byte) {
          if (auto v = reader.Read<std::int8_t>()) val = *v, success = true;
        } else if (current_type == base::TagType::Short) {
          if (auto v = reader.Read<std::int16_t>()) val = *v, success = true;
        } else if (current_type == base::TagType::Int) {
          if (auto v = reader.Read<std::int32_t>()) val = *v, success = true;
        } else if (current_type == base::TagType::Long) {
          if (auto v = reader.Read<std::int64_t>()) val = *v, success = true;
        }
        if (!success) return false;
        if (query.op == NbtOp::kEqInt) return val == query.int_val;
        if (query.op == NbtOp::kGtInt) return val > query.int_val;
        if (query.op == NbtOp::kLtInt) return val < query.int_val;
        return false;
      }
    }
    return false;
  }

  absl::flat_hash_set<LocalPos, LocalPosHash> matched_positions_;
};

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_NBT_FILTER_H_