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

#ifndef FSCHEMA_GEOMETRY_INTERNAL_MERGE_H_
#define FSCHEMA_GEOMETRY_INTERNAL_MERGE_H_

#include <array>
#include <cstdint>
#include <vector>

#include "fschema/filters/pos.h"
#include "fschema/geometry/types.h"

namespace fschema::geometry::internal {

// A visible face after cull, with all info needed to emit it directly
// or as part of a merged rectangle.
struct VisibleFaceInfo {
  filters::LocalPos block_pos;
  std::uint8_t cull_face;    // 1..6
  std::uint32_t slot_index;  // atlas slot
  std::uint8_t tint_slot;
  bool is_transparent;
  // Synthesized fluid faces bypass greedy merge: their corners are not
  // unit-cube aligned, and MergeGroup rebuilds quads from block-grid
  // bounds, which would flatten slopes and raise sub-block tops.
  bool is_fluid = false;
  // World-space corner positions (block_pos + ModelFace::corners[i]).
  std::array<std::array<float, 3>, 4> world_corners{};
  // Model-local UVs (from ModelFace::uvs[i]).
  std::array<std::array<float, 2>, 4> uvs{};
  // Per-vertex AO, filled when the face is collected.
  std::array<std::uint8_t, 4> ao{};
};

// Emits faces unmerged: one quad (4 verts + 6 indices) per face. The
// fallback path when opts.merge_coplanar_faces is false.
void EmitFacesDirectly(const std::vector<VisibleFaceInfo>& faces,
                       const TextureAtlas& atlas,
                       Mesh& out_mesh);

// Greedy merge + emit. Groups faces by (cull_face, plane_pos, slot, tint,
// transparency), builds a 2D occupancy grid per group, grows rectangles
// greedily, and emits one quad per merged rectangle -- roughly 5-10x fewer
// triangles on cube-heavy scenes.
void MergeAndEmitFaces(const std::vector<VisibleFaceInfo>& faces,
                       const TextureAtlas& atlas,
                       Mesh& out_mesh);

}  // namespace fschema::geometry::internal

#endif  // FSCHEMA_GEOMETRY_INTERNAL_MERGE_H_
