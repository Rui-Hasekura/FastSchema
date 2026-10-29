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

// Face-direction shared setup: unit normal, the two in-plane axes
// ((axis1, axis2) mirror ComputeVertexAO's per-direction side picks)
// and their linear-index strides in the YZX block layout.
struct FaceDirSetup {
  std::array<int, 3> normal{};
  int axis1 = 0;  // first in-plane axis (side1 direction)
  int axis2 = 0;  // second in-plane axis (side2 direction)
  std::int64_t stride1 = 0;
  std::int64_t stride2 = 0;
  bool ok = false;
};

[[nodiscard]] FaceDirSetup SetupFaceDir(
    std::uint8_t cull_face, const ir::BoundingBox& bounds) noexcept {
  FaceDirSetup s;
  // YZX layout strides: x = 1, z = size_x, y = size_x * size_z.
  const std::int64_t sx = bounds.size[0];
  const std::int64_t strides[3] = {1, sx * bounds.size[2], sx};
  switch (static_cast<FaceDir>(cull_face)) {
    case FaceDir::kPosX:
      s.normal = {1, 0, 0};
      s.axis1 = 1;
      s.axis2 = 2;
      break;
    case FaceDir::kNegX:
      s.normal = {-1, 0, 0};
      s.axis1 = 1;
      s.axis2 = 2;
      break;
    case FaceDir::kPosY:
      s.normal = {0, 1, 0};
      s.axis1 = 0;
      s.axis2 = 2;
      break;
    case FaceDir::kNegY:
      s.normal = {0, -1, 0};
      s.axis1 = 0;
      s.axis2 = 2;
      break;
    case FaceDir::kPosZ:
      s.normal = {0, 0, 1};
      s.axis1 = 0;
      s.axis2 = 1;
      break;
    case FaceDir::kNegZ:
      s.normal = {0, 0, -1};
      s.axis1 = 0;
      s.axis2 = 1;
      break;
    default:
      return s;  // ok stays false
  }
  s.stride1 = strides[s.axis1];
  s.stride2 = strides[s.axis2];
  s.ok = true;
  return s;
}

// Brightness for an AO occlusion level (0..3), mirroring the legacy
// strength curve.
[[nodiscard]] std::uint8_t AoLevelBrightness(int level,
                                             float ao_strength) noexcept {
  if (ao_strength <= 0.0f) return 255;
  if (ao_strength >= 1.0f) {
    return static_cast<std::uint8_t>(255 - level * 85);
  }
  const float brightness = 255.0f - level * (255.0f * ao_strength / 3.0f);
  return static_cast<std::uint8_t>(brightness < 0.0f ? 0.0f : brightness);
}

}  // namespace

std::uint8_t ComputeVertexAO(const BlockGroupTable& table,
                             const ir::BoundingBox& bounds,
                             filters::LocalPos block_pos,
                             std::uint8_t cull_face,
                             const std::array<float, 3>& vertex_local,
                             float ao_strength) noexcept {
  // AO disabled entirely: skip the neighbor probes, every vertex is
  // fully bright. (The probes used to run even with ao_strength == 0.)
  if (cull_face == 0 || ao_strength <= 0.0f) return 255;

  const FaceDirSetup s = SetupFaceDir(cull_face, bounds);
  if (!s.ok) return 255;

  const auto Add = [](filters::LocalPos p, const std::array<int, 3>& d) {
    return filters::LocalPos{p.x + d[0], p.y + d[1], p.z + d[2]};
  };

  std::array<int, 3> side1{};
  std::array<int, 3> side2{};
  side1[s.axis1] = AxisSign(vertex_local[s.axis1]);
  side2[s.axis2] = AxisSign(vertex_local[s.axis2]);

  const filters::LocalPos n_side1 = Add(Add(block_pos, s.normal), side1);
  const filters::LocalPos n_side2 = Add(Add(block_pos, s.normal), side2);
  const filters::LocalPos n_corner =
      Add(Add(Add(block_pos, s.normal), side1), side2);

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

  return AoLevelBrightness(level, ao_strength);
}

void ComputeFaceAO(const BlockGroupTable& table,
                   const ir::BoundingBox& bounds,
                   filters::LocalPos block_pos,
                   std::uint8_t cull_face,
                   const std::array<std::array<float, 3>, 4>& corners,
                   float ao_strength,
                   std::array<std::uint8_t, 4>& out_ao) noexcept {
  out_ao.fill(255);
  if (cull_face == 0 || ao_strength <= 0.0f) return;

  const FaceDirSetup s = SetupFaceDir(cull_face, bounds);
  if (!s.ok) return;

  // Face-adjacent cell. When it is out of bounds every side/corner
  // probe is too (they share the normal-axis coordinate), so all four
  // vertices stay at 255.
  const std::array<int, 3> base_c{block_pos.x + s.normal[0],
                                   block_pos.y + s.normal[1],
                                   block_pos.z + s.normal[2]};
  if (base_c[0] < 0 || base_c[0] >= bounds.size[0] || base_c[1] < 0 ||
      base_c[1] >= bounds.size[1] || base_c[2] < 0 ||
      base_c[2] >= bounds.size[2]) {
    return;
  }
  const std::int64_t base_linear = static_cast<std::int64_t>(
      filters::LinearIndex({base_c[0], base_c[1], base_c[2]}, bounds));
  if (static_cast<std::size_t>(base_linear) >= table.bytes.size()) {
    return;
  }
  // In-bounds ranges of the two in-plane coordinates; probes outside
  // read as air, exactly like IsOpaqueInBounds.
  const std::int32_t hi1 = bounds.size[s.axis1];
  const std::int32_t hi2 = bounds.size[s.axis2];
  const std::uint8_t* bytes = table.bytes.data();
  const std::size_t table_size = table.bytes.size();

  auto opaque_at = [&](std::int64_t linear) -> bool {
    return linear >= 0 && static_cast<std::size_t>(linear) < table_size &&
           bytes[static_cast<std::size_t>(linear)] == 1;
  };

  for (int i = 0; i < 4; ++i) {
    const int sa1 = AxisSign(corners[i][s.axis1]);
    const int sa2 = AxisSign(corners[i][s.axis2]);
    const std::int32_t c1 = base_c[s.axis1] + sa1;
    const std::int32_t c2 = base_c[s.axis2] + sa2;
    const bool s1 = c1 >= 0 && c1 < hi1 &&
                    opaque_at(base_linear + sa1 * s.stride1);
    const bool s2 = c2 >= 0 && c2 < hi2 &&
                    opaque_at(base_linear + sa2 * s.stride2);
    int level;
    if (s1 && s2) {
      level = 3;
    } else if (s1 || s2) {
      level = 2;
    } else {
      const bool c = c1 >= 0 && c1 < hi1 && c2 >= 0 && c2 < hi2 &&
                     opaque_at(base_linear + sa1 * s.stride1 +
                               sa2 * s.stride2);
      level = c ? 1 : 0;
    }
    out_ao[i] = AoLevelBrightness(level, ao_strength);
  }
}

}  // namespace fschema::geometry::internal
