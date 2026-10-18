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

#ifndef FSCHEMA_REMAP_ITEM_H_
#define FSCHEMA_REMAP_ITEM_H_

#include <deque>
#include <string>
#include <string_view>

#include "fschema/base/nbt/tree.h"

namespace fschema::remap::internal {

class RemapContext;

// Converts one item compound from its component form to the target-era
// tag form. No-op when the item is already in tag form.
void DowngradeItemCompound(RemapContext& ctx,
                           base::NbtCompound& item,
                           std::deque<std::string>& scratch,
                           std::string_view path);

}  // namespace fschema::remap::internal

#endif  // FSCHEMA_REMAP_ITEM_H_