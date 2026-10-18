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

#include "fschema/remap/internal/palette.h"

#include "fschema/remap/internal/context.h"
#include "fschema/remap/internal/rule.h"

namespace fschema::remap::internal {

// Renames block ids in `region.palette` using the active id-rename window.
// Renames are zero-allocation span re-points into the static table storage.
// Unknown ids are kept verbatim so the target file still names the original
// block; the caller surfaces them through the diagnostic stream.
ParseResult<void> RemapRegionPalette(RemapContext& ctx,
                                     ir::Region& region,
                                     std::string_view path) {
  if (!ctx.IdRenameFamilyActive()) return {};

  for (std::size_t i = 0; i < region.palette.size(); ++i) {
    ir::BlockState& bs = region.palette[i];
    const IdRenameRule* rule =
        FindIdRename(bs.name, ctx.tgt_dv(), ctx.src_dv());
    if (rule == nullptr) continue;
    bs.name = rule->older_id;
    ++ctx.stats().counters.blocks_renamed;
    ctx.Report(Diag::kBlockIdRenamed, rule->rule_id, path);
  }
  return {};
}

}  // namespace fschema::remap::internal