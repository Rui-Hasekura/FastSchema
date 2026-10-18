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

#ifndef FSCHEMA_REMAP_PALETTE_H_
#define FSCHEMA_REMAP_PALETTE_H_

#include <string_view>

#include "fschema/base/error.h"
#include "fschema/ir/types.h"

namespace fschema::remap::internal {

class RemapContext;

// Renames block ids in `region.palette` according to the active id-rename
// window. Unknown ids are kept verbatim.
[[nodiscard]] ParseResult<void> RemapRegionPalette(RemapContext& ctx,
                                                   ir::Region& region,
                                                   std::string_view path);

}  // namespace fschema::remap::internal

#endif  // FSCHEMA_REMAP_PALETTE_H_