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

#include "fschema/ir/materialize.h"

#include "fschema/litematic/internal/block_states.h"
#include "fschema/schem/internal/block_data.h"

namespace fschema::ir {

ParseResult<void> EnsureMaterialized(const Region& reg, memory::Arena& arena) {
  if (reg.is_materialized) return {};

  std::uint64_t volume = static_cast<std::uint64_t>(reg.bounds.size[0]) *
                         static_cast<std::uint64_t>(reg.bounds.size[1]) *
                         static_cast<std::uint64_t>(reg.bounds.size[2]);

  if (reg.lazy_source.encoding == BlockDataEncoding::kLitematicaLongArray) {
    auto res =
        litematic::internal::UnpackIndicesFused(reg.lazy_source.raw_bytes,
                                                reg.lazy_source.bits_per_block,
                                                volume,
                                                reg.lazy_source.palette_size);
    if (!res) return std::unexpected(res.error());
    reg.block_indices = std::move(*res);
  } else if (reg.lazy_source.encoding == BlockDataEncoding::kSpongeVarint) {
    auto res = schem::internal::DecodeVarintArray(
        reg.lazy_source.raw_bytes, volume, reg.lazy_source.palette_size, arena);
    if (!res) return std::unexpected(res.error());
    reg.block_indices = std::move(*res);
  } else {
    // Empty region or unknown format, allocate default space
    reg.block_indices.reallocate_uninitialized(static_cast<std::size_t>(volume),
                                           &arena);
  }

  reg.is_materialized = true;
  return {};
}

}  // namespace fschema::ir