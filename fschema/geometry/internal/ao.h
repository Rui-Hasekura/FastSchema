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

#ifndef FSCHEMA_GEOMETRY_INTERNAL_AO_H_
#define FSCHEMA_GEOMETRY_INTERNAL_AO_H_

#include <array>
#include <cstdint>

#include "fschema/filters/pos.h"
#include "fschema/geometry/internal/cull.h"
#include "fschema/ir/types.h"

namespace fschema::geometry::internal {

// Minecraft-style smooth-lighting AO for one vertex of a cube face.
// Returns the vertex brightness in [0, 255] (255 = unoccluded).
// `vertex_local` is the vertex position in block-local [0, 1] space;
// `cull_face` uses the FaceDir values. AO is disabled when the face has
// no cull_face (non-axis-aligned models get no AO). Only opaque blocks
// (group 1) occlude; transparent blocks cast no shade, as in vanilla.
[[nodiscard]] std::uint8_t ComputeVertexAO(
    const BlockGroupTable& table,
    const ir::BoundingBox& bounds,
    filters::LocalPos block_pos,
    std::uint8_t cull_face,
    const std::array<float, 3>& vertex_local,
    float ao_strength) noexcept;

// All four vertex AO bytes of one face in a single pass: shares the
// face-direction setup (normal axis, in-plane axes, face-adjacent cell
// linear index) across the corners instead of paying the per-vertex
// switch/bounds dance four times. Same semantics as calling
// ComputeVertexAO per corner; `corners` are the face's four
// block-local vertex positions.
void ComputeFaceAO(const BlockGroupTable& table,
                   const ir::BoundingBox& bounds,
                   filters::LocalPos block_pos,
                   std::uint8_t cull_face,
                   const std::array<std::array<float, 3>, 4>& corners,
                   float ao_strength,
                   std::array<std::uint8_t, 4>& out_ao) noexcept;

}  // namespace fschema::geometry::internal

#endif  // FSCHEMA_GEOMETRY_INTERNAL_AO_H_
