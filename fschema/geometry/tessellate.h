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

#ifndef FSCHEMA_GEOMETRY_TESSELLATE_H_
#define FSCHEMA_GEOMETRY_TESSELLATE_H_

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "fschema/base/error.h"
#include "fschema/geometry/resource_pack.h"
#include "fschema/geometry/types.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"

namespace fschema::geometry {

struct TessellationOptions {
  // Drop faces shared between two opaque neighbors.
  bool cull_interior_faces = true;
  // Merge coplanar adjacent faces.
  bool merge_coplanar_faces = false;
  // Per-vertex ambient occlusion.
  bool ambient_occlusion = true;
  float ao_strength = 0.4f;  // 0..1

  std::uint32_t atlas_max_extent = 4096;
  std::uint32_t atlas_padding = 1;

  // Parallelism: chunk edge length for TBB. 0 = single-threaded.
  std::uint32_t chunk_edge = 16;

  // Include air blocks. Air without a model produces no geometry, so
  // this only affects packs that give air a non-empty model.
  bool include_air = false;
};

// Tessellate a single ir::Region.
[[nodiscard]] ParseResult<TessellationResult> TessellateRegion(
    const ir::Region& region,
    memory::Arena& arena,
    const ResourcePack& pack,
    TessellationOptions opts = {});

// Tessellate all regions of a Schematic as one combined volume.
[[nodiscard]] ParseResult<TessellationResult> TessellateSchematic(
    const ir::Schematic& schematic,
    const ResourcePack& pack,
    TessellationOptions opts = {});

}  // namespace fschema::geometry

#endif  // FSCHEMA_GEOMETRY_TESSELLATE_H_
