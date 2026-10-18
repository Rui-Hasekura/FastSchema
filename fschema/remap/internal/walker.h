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

#ifndef FSCHEMA_REMAP_WALKER_H_
#define FSCHEMA_REMAP_WALKER_H_

#include <deque>
#include <string>
#include <string_view>

#include "fschema/base/nbt/tree.h"

namespace fschema::remap::internal {

class RemapContext;

// Walks an NBT compound recursively and applies the active families.
void WalkCompoundDeep(RemapContext& ctx,
                      base::NbtCompound& c,
                      std::deque<std::string>& scratch,
                      std::string_view path,
                      int depth,
                      bool entity_root);

}  // namespace fschema::remap::internal

#endif  // FSCHEMA_REMAP_WALKER_H_