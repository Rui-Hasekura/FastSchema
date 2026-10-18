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

#ifndef FSCHEMA_REMAP_COMPONENT_EXTRA_H_
#define FSCHEMA_REMAP_COMPONENT_EXTRA_H_

#include <cstdint>
#include <deque>
#include <string_view>

#include "fschema/base/nbt/tree.h"
#include "fschema/remap/internal/rule.h"

namespace fschema::remap::internal {

class RemapContext;

// Builds a target-era `SkullOwner` compound from a `{name, id, properties}`
// profile body and appends it to `out_parent` under `field_name`. Returns
// false when the input carries no `name`, no parseable `id`, and no
// `properties`; the caller then leaves the source field alone.
[[nodiscard]] bool WriteSkullOwner(RemapContext& ctx,
                                   const base::NbtTag& src,
                                   base::NbtCompound& out_parent,
                                   std::string_view field_name,
                                   std::deque<std::string>& scratch,
                                   std::uint16_t rule_id,
                                   std::string_view path);

// Applies one extra component -> tag rule (kinds not handled by the main
// ApplyComponent switch). Returns true when the component was fully
// handled; false when the caller should treat it as unmapped.
[[nodiscard]] bool ApplyComponentExtra(RemapContext& ctx,
                                       const ComponentRule& rule,
                                       base::NbtTag& comp,
                                       base::NbtCompound& tag,
                                       std::deque<std::string>& scratch,
                                       std::uint8_t& hide_bits,
                                       std::string_view path);

}  // namespace fschema::remap::internal

#endif  // FSCHEMA_REMAP_COMPONENT_EXTRA_H_