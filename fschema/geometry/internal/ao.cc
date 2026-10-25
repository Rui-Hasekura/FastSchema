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

#include "fschema/geometry/internal/ao.h"

#include <array>
#include <cstdint>

#include "fschema/filters/pos.h"
#include "fschema/filters/view.h"
#include "fschema/geometry/internal/cull.h"
#include "fschema/geometry/internal/resolver.h"
#include "fschema/ir/types.h"

namespace fschema::geometry::internal {

namespace {

// Corner coordinate -> in-plane side: <0.5 is the min side, else the max.
[[nodiscard]] constexpr int AxisSign(float v) noexcept {
  return v < 0.5f ? -1 : 1;
}

[[nodiscard]] bool IsOpaqueInBounds(const BlockGroupTable& table,
                                    const ir::BoundingBox& bounds,
                                    filters::LocalPos p) noexcept {
  if (p.x < 0 || p.y < 0 || p.z < 0) return false;
  if (p.x >= bounds.size[0] || p.y >= bounds.size[1] || p.z >= bounds.size[2])
    return false;
  const std::size_t linear = filters::LinearIndex(p, bounds);
  if (linear >= table.bytes.size()) return false;
  return table.IsOpaque(linear);
}

}  // namespace

std::uint8_t ComputeVertexAO(const BlockGroupTable& table,
                             const ir::BoundingBox& bounds,
                             filters::LocalPos block_pos,
                             std::uint8_t cull_face,
                             const std::array<float, 3>& vertex_local,
                             float ao_strength) noexcept {
  if (cull_face == 0) return 255;

  std::array<int, 3> normal{};
  switch (static_cast<FaceDir>(cull_face)) {
    case FaceDir::kPosX:
      normal = {1, 0, 0};
      break;
    case FaceDir::kNegX:
      normal = {-1, 0, 0};
      break;
    case FaceDir::kPosY:
      normal = {0, 1, 0};
      break;
    case FaceDir::kNegY:
      normal = {0, -1, 0};
      break;
    case FaceDir::kPosZ:
      normal = {0, 0, 1};
      break;
    case FaceDir::kNegZ:
      normal = {0, 0, -1};
      break;
    default:
      return 255;
  }

  std::array<int, 3> side1{}, side2{};
  switch (static_cast<FaceDir>(cull_face)) {
    case FaceDir::kPosX:
    case FaceDir::kNegX:  // in-plane: Y, Z
      side1 = {0, AxisSign(vertex_local[1]), 0};
      side2 = {0, 0, AxisSign(vertex_local[2])};
      break;
    case FaceDir::kPosY:
    case FaceDir::kNegY:  // in-plane: X, Z
      side1 = {AxisSign(vertex_local[0]), 0, 0};
      side2 = {0, 0, AxisSign(vertex_local[2])};
      break;
    case FaceDir::kPosZ:
    case FaceDir::kNegZ:  // in-plane: X, Y
      side1 = {AxisSign(vertex_local[0]), 0, 0};
      side2 = {0, AxisSign(vertex_local[1]), 0};
      break;
  }

  const auto Add = [](filters::LocalPos p, const std::array<int, 3>& d) {
    return filters::LocalPos{p.x + d[0], p.y + d[1], p.z + d[2]};
  };

  const filters::LocalPos n_side1 = Add(Add(block_pos, normal), side1);
  const filters::LocalPos n_side2 = Add(Add(block_pos, normal), side2);
  const filters::LocalPos n_corner =
      Add(Add(Add(block_pos, normal), side1), side2);

  const bool s1 = IsOpaqueInBounds(table, bounds, n_side1);
  const bool s2 = IsOpaqueInBounds(table, bounds, n_side2);
  const bool c = IsOpaqueInBounds(table, bounds, n_corner);

  int level;
  if (s1 && s2)
    level = 3;
  else if (s1 || s2)
    level = 2;
  else if (c)
    level = 1;
  else
    level = 0;

  if (ao_strength <= 0.0f) return 255;
  if (ao_strength >= 1.0f) {
    return static_cast<std::uint8_t>(255 - level * 85);
  }
  const float brightness = 255.0f - level * (255.0f * ao_strength / 3.0f);
  return static_cast<std::uint8_t>(brightness < 0.0f ? 0.0f : brightness);
}

}  // namespace fschema::geometry::internal
