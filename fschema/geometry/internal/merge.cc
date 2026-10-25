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

#include "fschema/geometry/internal/merge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "fschema/base/hash.h"
#include "fschema/geometry/internal/resolver.h"
#include "fschema/geometry/types.h"

namespace fschema::geometry::internal {

namespace {

// Normal direction for a cull face.
constexpr std::array<float, 3> NormalForCullFace(std::uint8_t cf) {
  switch (static_cast<FaceDir>(cf)) {
    case FaceDir::kPosX:
      return {1, 0, 0};
    case FaceDir::kNegX:
      return {-1, 0, 0};
    case FaceDir::kPosY:
      return {0, 1, 0};
    case FaceDir::kNegY:
      return {0, -1, 0};
    case FaceDir::kPosZ:
      return {0, 0, 1};
    case FaceDir::kNegZ:
      return {0, 0, -1};
    default:
      return {0, 0, 1};
  }
}

// Axis the cull face is normal to (0=X, 1=Y, 2=Z).
constexpr int NormalAxis(std::uint8_t cf) {
  switch (static_cast<FaceDir>(cf)) {
    case FaceDir::kPosX:
    case FaceDir::kNegX:
      return 0;
    case FaceDir::kPosY:
    case FaceDir::kNegY:
      return 1;
    case FaceDir::kPosZ:
    case FaceDir::kNegZ:
      return 2;
    default:
      return 0;
  }
}

// The two in-plane axes, as (height, width) in the 2D merge grid.
constexpr std::pair<int, int> InPlaneAxes(std::uint8_t cf) {
  switch (static_cast<FaceDir>(cf)) {
    case FaceDir::kPosX:
    case FaceDir::kNegX:
      return {1, 2};  // Y, Z
    case FaceDir::kPosY:
    case FaceDir::kNegY:
      return {2, 0};  // Z, X
    case FaceDir::kPosZ:
    case FaceDir::kNegZ:
      return {0, 1};  // X, Y
    default:
      return {1, 2};
  }
}

// Group key for merging: faces with the same key are coplanar and can
// potentially be merged.
struct GroupKey {
  std::uint8_t cull_face;
  std::int32_t plane_pos;  // coordinate along normal axis
  std::uint32_t slot_index;
  std::uint8_t tint_slot;
  bool is_transparent;

  bool operator==(const GroupKey&) const = default;
};

struct GroupKeyHash {
  std::size_t operator()(const GroupKey& k) const noexcept {
    std::array<std::uint8_t, 11> buf{};
    buf[0] = k.cull_face;
    std::memcpy(buf.data() + 1, &k.plane_pos, 4);
    std::memcpy(buf.data() + 5, &k.slot_index, 4);
    buf[9] = k.tint_slot;
    buf[10] = k.is_transparent ? std::uint8_t{1} : std::uint8_t{0};
    return static_cast<std::size_t>(
        base::Hash64WithSeed(buf.data(), buf.size()));
  }
};

// Greedy merge for one group of coplanar faces.
void MergeGroup(const std::vector<const VisibleFaceInfo*>& group_faces,
                const TextureAtlas& atlas,
                Mesh& out_mesh) {
  if (group_faces.empty()) return;

  const std::uint8_t cf = group_faces[0]->cull_face;
  const auto [axis1, axis2] = InPlaneAxes(cf);
  const auto normal = NormalForCullFace(cf);

  // Find grid bounds in the in-plane axes.
  std::int32_t min_a1 = std::numeric_limits<std::int32_t>::max();
  std::int32_t max_a1 = std::numeric_limits<std::int32_t>::min();
  std::int32_t min_a2 = std::numeric_limits<std::int32_t>::max();
  std::int32_t max_a2 = std::numeric_limits<std::int32_t>::min();
  for (const auto* f : group_faces) {
    const int a1 = (&f->block_pos.x)[axis1];
    const int a2 = (&f->block_pos.x)[axis2];
    min_a1 = std::min(min_a1, static_cast<std::int32_t>(a1));
    max_a1 = std::max(max_a1, static_cast<std::int32_t>(a1));
    min_a2 = std::min(min_a2, static_cast<std::int32_t>(a2));
    max_a2 = std::max(max_a2, static_cast<std::int32_t>(a2));
  }

  const int grid_w = max_a2 - min_a2 + 1;
  const int grid_h = max_a1 - min_a1 + 1;
  if (grid_w <= 0 || grid_h <= 0) return;

  // Occupancy grid: cell -> face (nullptr = empty).
  std::vector<const VisibleFaceInfo*> grid(
      static_cast<std::size_t>(grid_w) * grid_h, nullptr);
  for (const auto* f : group_faces) {
    const int a1 = (&f->block_pos.x)[axis1] - min_a1;
    const int a2 = (&f->block_pos.x)[axis2] - min_a2;
    grid[static_cast<std::size_t>(a1) * grid_w + a2] = f;
  }

  std::vector<std::uint8_t> merged(static_cast<std::size_t>(grid_w) * grid_h,
                                   0);

  for (int row = 0; row < grid_h; ++row) {
    for (int col = 0; col < grid_w; ++col) {
      const std::size_t idx = static_cast<std::size_t>(row) * grid_w + col;
      if (merged[idx] || !grid[idx]) continue;

      // Grow right.
      int rect_w = 1;
      while (col + rect_w < grid_w) {
        const std::size_t next =
            static_cast<std::size_t>(row) * grid_w + (col + rect_w);
        if (merged[next] || !grid[next]) break;
        ++rect_w;
      }

      // Grow down.
      int rect_h = 1;
      bool can_grow = true;
      while (row + rect_h < grid_h && can_grow) {
        for (int x = 0; x < rect_w; ++x) {
          const std::size_t next =
              static_cast<std::size_t>(row + rect_h) * grid_w + (col + x);
          if (merged[next] || !grid[next]) {
            can_grow = false;
            break;
          }
        }
        if (can_grow) ++rect_h;
      }

      // Mark the rectangle's cells as merged.
      for (int y = 0; y < rect_h; ++y) {
        for (int x = 0; x < rect_w; ++x) {
          merged[static_cast<std::size_t>(row + y) * grid_w + (col + x)] = 1;
        }
      }

      // Corner blocks for AO.
      const VisibleFaceInfo* bl =
          grid[static_cast<std::size_t>(row) * grid_w + col];
      const VisibleFaceInfo* br =
          grid[static_cast<std::size_t>(row) * grid_w + (col + rect_w - 1)];
      const VisibleFaceInfo* tr =
          grid[static_cast<std::size_t>(row + rect_h - 1) * grid_w +
               (col + rect_w - 1)];
      const VisibleFaceInfo* tl =
          grid[static_cast<std::size_t>(row + rect_h - 1) * grid_w + col];

      // Reconstruct quad corners from the rect geometry. Assumes unit
      // cube faces; corner order matches BuildCubeFace's winding.
      const int na = NormalAxis(cf);
      const float na_coord = bl->world_corners[0][na];
      const float a1_min = static_cast<float>(min_a1 + row);
      const float a1_max = static_cast<float>(min_a1 + row + rect_h);
      const float a2_min = static_cast<float>(min_a2 + col);
      const float a2_max = static_cast<float>(min_a2 + col + rect_w);

      // Positive faces (+X/+Y/+Z, odd cull_face) wind (min,min)
      // (min,max) (max,max) (max,min); negative faces wind (min,min)
      // (max,min) (max,max) (min,max).
      const bool mode_a = (cf % 2 == 1);
      std::array<std::array<float, 3>, 4> corners{};
      auto SetCorner = [&](int i, float a1, float a2) {
        corners[i][na] = na_coord;
        corners[i][axis1] = a1;
        corners[i][axis2] = a2;
      };
      if (mode_a) {
        SetCorner(0, a1_min, a2_min);
        SetCorner(1, a1_min, a2_max);
        SetCorner(2, a1_max, a2_max);
        SetCorner(3, a1_max, a2_min);
      } else {
        SetCorner(0, a1_min, a2_min);
        SetCorner(1, a1_max, a2_min);
        SetCorner(2, a1_max, a2_max);
        SetCorner(3, a1_min, a2_max);
      }

      // UVs follow the merged rect's world position projected onto the
      // direction's model image axes (ModelFaceAxes, the same basis
      // CubeFaceUvRect assigns to element faces), in block units: the
      // texture tiles once per block exactly like the unmerged faces
      // it replaces.
      float ub[3], vb[3];
      ModelFaceAxes(static_cast<FaceDir>(cf), ub, vb);
      float su[4], sv[4];
      for (int i = 0; i < 4; ++i) {
        su[i] = corners[i][0] * ub[0] + corners[i][1] * ub[1] +
                corners[i][2] * ub[2];
        sv[i] = corners[i][0] * vb[0] + corners[i][1] * vb[1] +
                corners[i][2] * vb[2];
      }
      float su_min = su[0], sv_min = sv[0];
      for (int i = 1; i < 4; ++i) {
        su_min = std::min(su_min, su[i]);
        sv_min = std::min(sv_min, sv[i]);
      }
      std::array<std::array<float, 2>, 4> uvs{};
      for (int i = 0; i < 4; ++i) {
        uvs[i] = {su[i] - su_min, sv[i] - sv_min};
      }

      // AO source per corner, in the same order.
      const VisibleFaceInfo* src[4] = {
          bl, mode_a ? br : tl, tr, mode_a ? tl : br};
      std::array<std::uint8_t, 4> ao{};
      for (int i = 0; i < 4; ++i) {
        ao[i] = 255;
        for (int k = 0; k < 4; ++k) {
          if (std::abs(src[i]->world_corners[k][axis1] - corners[i][axis1]) <
                  0.001f &&
              std::abs(src[i]->world_corners[k][axis2] - corners[i][axis2]) <
                  0.001f) {
            ao[i] = src[i]->ao[k];
            break;
          }
        }
      }

      const std::uint32_t base =
          static_cast<std::uint32_t>(out_mesh.positions.size() / 3);
      const TextureAtlas::Slot& slot = atlas.slots[bl->slot_index];

      for (int i = 0; i < 4; ++i) {
        out_mesh.positions.push_back(corners[i][0]);
        out_mesh.positions.push_back(corners[i][1]);
        out_mesh.positions.push_back(corners[i][2]);
        out_mesh.normals.push_back(normal[0]);
        out_mesh.normals.push_back(normal[1]);
        out_mesh.normals.push_back(normal[2]);
        const float u = slot.u0 + (slot.u1 - slot.u0) * uvs[i][0];
        const float v = slot.v0 + (slot.v1 - slot.v0) * uvs[i][1];
        out_mesh.uvs.push_back(u);
        out_mesh.uvs.push_back(v);
        out_mesh.ao.push_back(ao[i]);
        out_mesh.tints.push_back(bl->tint_slot);
      }
      out_mesh.indices.push_back(base + 0);
      out_mesh.indices.push_back(base + 1);
      out_mesh.indices.push_back(base + 2);
      out_mesh.indices.push_back(base + 0);
      out_mesh.indices.push_back(base + 2);
      out_mesh.indices.push_back(base + 3);
    }
  }
}

}  // namespace

void EmitFacesDirectly(const std::vector<VisibleFaceInfo>& faces,
                       const TextureAtlas& atlas,
                       Mesh& out_mesh) {
  for (const auto& f : faces) {
    const auto normal = NormalForCullFace(f.cull_face);
    const TextureAtlas::Slot& slot = atlas.slots[f.slot_index];
    const std::uint32_t base =
        static_cast<std::uint32_t>(out_mesh.positions.size() / 3);
    for (int i = 0; i < 4; ++i) {
      out_mesh.positions.push_back(f.world_corners[i][0]);
      out_mesh.positions.push_back(f.world_corners[i][1]);
      out_mesh.positions.push_back(f.world_corners[i][2]);
      out_mesh.normals.push_back(normal[0]);
      out_mesh.normals.push_back(normal[1]);
      out_mesh.normals.push_back(normal[2]);
      const float u = slot.u0 + (slot.u1 - slot.u0) * f.uvs[i][0];
      const float v = slot.v0 + (slot.v1 - slot.v0) * f.uvs[i][1];
      out_mesh.uvs.push_back(u);
      out_mesh.uvs.push_back(v);
      out_mesh.ao.push_back(f.ao[i]);
      out_mesh.tints.push_back(f.tint_slot);
    }
    out_mesh.indices.push_back(base + 0);
    out_mesh.indices.push_back(base + 1);
    out_mesh.indices.push_back(base + 2);
    out_mesh.indices.push_back(base + 0);
    out_mesh.indices.push_back(base + 2);
    out_mesh.indices.push_back(base + 3);
  }
}

void MergeAndEmitFaces(const std::vector<VisibleFaceInfo>& faces,
                       const TextureAtlas& atlas,
                       Mesh& out_mesh) {
  if (faces.empty()) return;

  absl::flat_hash_map<GroupKey,
                      std::vector<const VisibleFaceInfo*>,
                      GroupKeyHash>
      groups;
  for (const auto& f : faces) {
    const int na = NormalAxis(f.cull_face);
    const std::int32_t plane_pos = (&f.block_pos.x)[na];
    GroupKey key{
        f.cull_face, plane_pos, f.slot_index, f.tint_slot, f.is_transparent};
    groups[key].push_back(&f);
  }

  for (const auto& [key, group_faces] : groups) {
    MergeGroup(group_faces, atlas, out_mesh);
  }
}

}  // namespace fschema::geometry::internal
