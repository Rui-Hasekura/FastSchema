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

#ifndef FSCHEMA_IR_MANIPULATE_H_
#define FSCHEMA_IR_MANIPULATE_H_

#include <string_view>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"

namespace fschema::ir {

// Extracts the specified region and discards others (zero-copy).
[[nodiscard]] ParseResult<void> ExtractRegion(Schema& ir,
                                              std::string_view region_name);

// Merges a list of regions into a single new region.
// Performs palette merging, index remapping, and air padding.
[[nodiscard]] ParseResult<Region> MergeRegions(
    const std::vector<Region>& regions,
    std::string_view fill_block_name,
    memory::Arena& arena);

}  // namespace fschema::ir

#endif  // FSCHEMA_IR_MANIPULATE_H_