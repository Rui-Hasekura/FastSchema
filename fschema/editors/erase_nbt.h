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

#ifndef FSCHEMA_EDITORS_ERASE_NBT_H_
#define FSCHEMA_EDITORS_ERASE_NBT_H_

#include <cstring>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "fschema/base/error.h"
#include "fschema/base/nbt/writer.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/pos.h"
#include "fschema/filters/view.h"
#include "fschema/ir/internal/codec_utils.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"

namespace fschema::editors {

/// Removes specific top-level fields from the NBT data of
/// selected BlockEntities matching the target IDs.
///
/// Filters matching block entities by local bounds and re-serializes their NBT
/// into the arena.
template <filters::Filter F>
[[nodiscard]] ParseResult<void> EraseNbt(
    filters::BasicView<F> v,
    std::span<const std::string_view> target_ids,
    std::span<const std::string_view> fields_to_erase,
    memory::Arena& arena) {
  ir::Region& r = v.region();

  absl::flat_hash_set<std::string_view> id_set(target_ids.begin(),
                                               target_ids.end());

  const std::int32_t sx = r.bounds.size[0];
  const std::int32_t sy = r.bounds.size[1];
  const std::int32_t sz = r.bounds.size[2];

  for (auto& be : r.block_entities) {
    const bool id_matches = be.id.empty() || id_set.contains(be.id);
    if (!id_matches) continue;

    filters::LocalPos lp;
    lp.x = be.block_position[0] - r.bounds.origin[0];
    lp.y = be.block_position[1] - r.bounds.origin[1];
    lp.z = be.block_position[2] - r.bounds.origin[2];

    if (lp.x < 0 || lp.x >= sx || lp.y < 0 || lp.y >= sy || lp.z < 0 ||
        lp.z >= sz) {
      continue;
    }

    std::uint64_t linear_idx = filters::LinearIndex(lp, r.bounds);
    std::uint16_t pal = r.block_indices[linear_idx];

    if (!v.filter()(lp, pal)) {
      continue;
    }

    // Re-serialize the NBT without the specified fields.
    // FilterAndWriteFields iterates the compound body and
    // writes everything except the fields listed in fields_to_erase.
    base::NbtWriter writer;
    auto res =
        ir::internal::FilterAndWriteFields(be.raw_nbt, fields_to_erase, writer);
    if (!res) return std::unexpected(res.error());

    // Append End tag to close the Compound body
    writer.WriteEndTag();

    std::vector<std::byte> new_payload = std::move(writer).Finalize();

    void* mem = arena.Allocate(new_payload.size());
    std::memcpy(mem, new_payload.data(), new_payload.size());
    be.raw_nbt = std::span<const std::byte>(static_cast<const std::byte*>(mem),
                                            new_payload.size());
  }

  // It's no need to call MarkEdited().
  return {};
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_ERASE_NBT_H_