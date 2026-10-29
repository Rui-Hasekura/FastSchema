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

#ifndef FSCHEMA_GEOMETRY_INTERNAL_RESOLVER_H_
#define FSCHEMA_GEOMETRY_INTERNAL_RESOLVER_H_

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "fschema/base/error.h"
#include "fschema/geometry/block_props.h"
#include "fschema/geometry/types.h"

namespace fschema::geometry::internal {

// Strip "minecraft:" namespace prefix.
[[nodiscard]] std::string_view StripNamespace(std::string_view ref);

// "minecraft:block/stone"  -> "assets/minecraft/textures/block/stone.png"
// "block/stone"             -> "assets/minecraft/textures/block/stone.png"
[[nodiscard]] std::string TextureRefToRelPath(std::string_view ref);

// "minecraft:stone"  -> "assets/minecraft/blockstates/stone.json"
[[nodiscard]] std::string BlockNameToBlockstateRelPath(
    std::string_view block_name);

// "minecraft:block/cube_all" -> "assets/minecraft/models/block/cube_all.json"
// "block/cube_all"            -> "assets/minecraft/models/block/cube_all.json"
[[nodiscard]] std::string ModelRefToRelPath(std::string_view model_ref);

// Reject path traversal (absolute paths, drive letters, ".." segments).
[[nodiscard]] bool IsPathSafe(std::string_view relative);

// Read file bytes into a string. Returns kModelMissingTexture if the
// file does not exist or cannot be opened.
[[nodiscard]] ParseResult<std::string> ReadFileBytes(
    const std::filesystem::path& path);

// Parse a variant key like "facing=north,half=bottom" and check that
// every (k,v) pair is present in `props` with the same value.
// BlockProps may have additional entries not covered by the variant key.
[[nodiscard]] bool VariantKeyMatches(std::string_view key,
                                     const BlockProps& props);

// Cull face indices (used by ModelFace::SetCullFace):
//   0 = none, 1 = +X, 2 = -X, 3 = +Y, 4 = -Y, 5 = +Z, 6 = -Z
enum class FaceDir : std::uint8_t {
  kPosZ = 5,  // "south"
  kNegZ = 6,  // "north"
  kPosX = 1,  // "east"
  kNegX = 2,  // "west"
  kPosY = 3,  // "up"
  kNegY = 4,  // "down"
};

// Standard cube face corner generation. Minecraft's cube is in [0, 16]
// block-local space; we emit [0, 1] (block-local, normalized).
//
// For each of 6 face directions, returns 4 corners in CCW order when
// viewed from outside the cube. Normal follows from the face direction.
struct CubeFaceGeometry {
  std::array<std::array<float, 3>, 4> corners;
  std::array<float, 3> normal;
  std::uint8_t cull_face;  // 1..6 (axis face)
};

[[nodiscard]] FaceDir ParseFaceDir(std::string_view name);

[[nodiscard]] CubeFaceGeometry BuildCubeFace(FaceDir dir,
                                             const std::array<float, 3>& from,
                                             const std::array<float, 3>& to);

// Vanilla per-face texture axes, MODEL side (FaceBakery.defaultFaceUV):
// u points image-left -> image-right, v image-top -> image-bottom as
// the face is seen from outside -- side faces stand the image upright
// and unmirrored (north/east mirror their axis via the 1 - coord
// terms of the default uv formulas), up is a north-up map (u = +X,
// v = +Z) and down reads with south up (u = +X, v = -Z). Face uv
// (0, 0) samples the texture's top-left texel (atlas Finalize keeps
// PNG row order; emit maps face uv into the slot linearly). Element
// faces, merged quads and uvlock follow this basis.
void ModelFaceAxes(FaceDir dir, float u[3], float v[3]) noexcept;

// The fluid renderer's own axes (net.minecraft FluidRenderer's corner
// uv assignment), used by synthesized fluid faces: side faces stand
// the image upright but MIRROR it (u runs opposite to ModelFaceAxes
// on all four sides) and down uses v = +Z like up. Vanilla's two
// systems genuinely differ; fluids must match FluidRenderer, models
// must match FaceBakery.
void FluidFaceAxes(FaceDir dir, float u[3], float v[3]) noexcept;

// The entity ModelPart box-UV axes (entity/chest/*.png and the other
// entity sheets): the four side walls unroll onto the sheet as a
// connected ring (west, north, east, south), so consecutive strips
// share a box edge and EVERY side face's u axis runs opposite to
// ModelFaceAxes -- the strip's low-u edge is the box's WEST side on
// north, its EAST side on south, its SOUTH end on west and its NORTH
// end on east; v stays upright. Up and down keep the block axes
// (up: u = +X, v = +Z; down: u = +X, v = -Z -- the down strip sits
// directly above the north strip in the box net and folds back along
// the north wall's top edge). The double-chest sheets are where the
// distinction becomes visible: vanilla leaves the seam-side strip
// transparent and drops the dark frame on the seam-side edge of every
// other strip, so side faces mapped on the block axes render the
// frame across the junction of a large chest's two halves.
void EntityFaceAxes(FaceDir dir, float u[3], float v[3]) noexcept;

// Assigns a [u0, v0, u1, v1] rect onto a face's four corners (in
// BuildCubeFace corner order) following ModelFaceAxes: each corner's
// uv is picked by projecting its position onto the direction's image
// axes and normalizing over the face's own extent -- how vanilla maps
// an element face's uv rect onto the quad.
[[nodiscard]] std::array<std::array<float, 2>, 4> CubeFaceUvRect(
    FaceDir dir,
    const std::array<std::array<float, 3>, 4>& corners,
    const std::array<float, 4>& rect) noexcept;

// Same rect assignment on the fluid axes (FluidFaceAxes), for
// synthesized fluid faces.
[[nodiscard]] std::array<std::array<float, 2>, 4> FluidCubeFaceUvRect(
    FaceDir dir,
    const std::array<std::array<float, 3>, 4>& corners,
    const std::array<float, 4>& rect) noexcept;

// Same rect assignment on the entity box-UV axes (EntityFaceAxes),
// for synthesized entity-model faces (the chest family).
[[nodiscard]] std::array<std::array<float, 2>, 4> EntityBoxFaceUvRect(
    FaceDir dir,
    const std::array<std::array<float, 3>, 4>& corners,
    const std::array<float, 4>& rect) noexcept;

}  // namespace fschema::geometry::internal

#endif  // FSCHEMA_GEOMETRY_INTERNAL_RESOLVER_H_
