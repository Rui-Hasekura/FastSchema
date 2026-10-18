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

#ifndef FSCHEMA_REMAP_ENTITY_H_
#define FSCHEMA_REMAP_ENTITY_H_

#include <cstdint>
#include <deque>
#include <string_view>

#include "fschema/base/error.h"
#include "fschema/base/nbt/tree.h"
#include "fschema/ir/types.h"

namespace fschema::remap::internal {

class RemapContext;

// Applies the active rule families to a single entity root.
[[nodiscard]] ParseResult<void> RemapEntity(RemapContext& ctx,
                                            ir::Entity& ent,
                                            std::string_view path);

// Downgrades one entity compound in place to the target DataVersion.
[[nodiscard]] ParseResult<void> DowngradeEntityCompound(
    RemapContext& ctx,
    base::NbtCompound& entity,
    std::deque<std::string>& scratch,
    std::string_view path);

// Splits `<id>_boat` / `<id>_chest_boat` / `<id>_raft` / `<id>_chest_raft`
// into `Type:<id>` + `minecraft:boat` / `minecraft:chest_boat`, then applies
// the id-rename table to the resulting id.
[[nodiscard]] ParseResult<void> DowngradeEntityId(
    RemapContext& ctx,
    base::NbtCompound& root,
    std::deque<std::string>& scratch,
    std::string_view path);

// Renames entity root fields and rewrites block-position fields.
void DowngradeEntityFields(RemapContext& ctx,
                           base::NbtCompound& root,
                           std::deque<std::string>& scratch,
                           std::string_view path);

// Rewrites the entity structural fields into their target-era layout.
void DowngradeEntityStructure(RemapContext& ctx,
                              base::NbtCompound& root,
                              std::string_view path,
                              std::deque<std::string>& scratch);

// Rewrites the entity `Attributes` list into its target-era layout.
void DowngradeEntityAttributes(RemapContext& ctx,
                               base::NbtCompound& root,
                               std::string_view path,
                               std::deque<std::string>& scratch);

}  // namespace fschema::remap::internal

#endif  // FSCHEMA_REMAP_ENTITY_H_