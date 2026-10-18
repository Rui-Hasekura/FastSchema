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

#ifndef FSCHEMA_REMAP_BLOCK_ENTITY_H_
#define FSCHEMA_REMAP_BLOCK_ENTITY_H_

#include <cstdint>
#include <deque>
#include <string_view>

#include "fschema/base/error.h"
#include "fschema/base/nbt/tree.h"
#include "fschema/ir/types.h"

namespace fschema::remap::internal {

class RemapContext;

// Applies the active rule families to a single block entity root.
[[nodiscard]] ParseResult<void> RemapBlockEntity(RemapContext& ctx,
                                                 ir::BlockEntity& be,
                                                 std::string_view path);

// Downgrades one block entity compound in place to the target DataVersion.
[[nodiscard]] ParseResult<void> DowngradeBlockEntityCompound(
    RemapContext& ctx,
    base::NbtCompound& block_entity,
    std::deque<std::string>& scratch,
    std::string_view path);

// Helpers exposed because they are also used from item.cc.
void WriteBannerPatternsListInto(RemapContext& ctx,
                                 base::NbtList& dst,
                                 const base::NbtList& src,
                                 std::uint16_t rule_id,
                                 std::string_view path);

void WriteBeesListInto(RemapContext& ctx,
                       base::NbtList& dst,
                       base::NbtList& src,
                       std::uint16_t rule_id,
                       std::deque<std::string>& scratch,
                       std::string_view path);

}  // namespace fschema::remap::internal

#endif  // FSCHEMA_REMAP_BLOCK_ENTITY_H_