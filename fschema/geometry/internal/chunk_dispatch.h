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

#ifndef FSCHEMA_GEOMETRY_INTERNAL_CHUNK_DISPATCH_H_
#define FSCHEMA_GEOMETRY_INTERNAL_CHUNK_DISPATCH_H_

#include <cstdint>
#include <vector>

#include "fschema/filters/pos.h"
#include "fschema/ir/types.h"

namespace fschema::geometry::internal {

// Origins of chunk_edge-aligned cubes covering `bounds`, in YZX order to
// match the block_indices memory layout. The last cube along each axis
// may extend past the bounds; callers clip. chunk_edge == 0 yields none.
std::vector<filters::LocalPos> ComputeChunkOrigins(
    const ir::BoundingBox& bounds,
    std::uint32_t chunk_edge);

}  // namespace fschema::geometry::internal

#endif  // FSCHEMA_GEOMETRY_INTERNAL_CHUNK_DISPATCH_H_
