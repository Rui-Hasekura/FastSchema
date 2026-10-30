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

// Group key packed into one 64-bit word:
//   plane_pos (32) | slot (20) | cull_face (3) | tint (3) | transparent (1)
// A flat u64 key hashes without re-reading the record bytes; the old
// struct key paid an xxhash pass over an 11-byte buffer per face.
// Slot indices beyond 2^20 cannot occur (the atlas is bounded by
// max_extent^2 / texture pixels), and kNoSlot faces never merge.
[[nodiscard]] std::uint64_t PackGroupKey(std::uint8_t cull_face,
                                         std::int32_t plane_pos,
                                         std::uint32_t slot_index,
                                         std::uint8_t tint_slot,
                                         bool is_transparent) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(plane_pos))
          << 32) |
         (static_cast<std::uint64_t>(slot_index & 0xFFFFFu) << 12) |
         (static_cast<std::uint64_t>(cull_face & 0x7u) << 9) |
         (static_cast<std::uint64_t>(tint_slot & 0x7u) << 6) |
         (is_transparent ? std::uint64_t{1} : std::uint64_t{0});
}

// World-space corners and model uvs of one collected face, re-derived
// from the palette model. Non-fluid faces translate the model's
// block-local corners by the record's world block position; their uvs
// are the model's own (fluid faces, the only per-block-morphed ones,
// never take the merge path).
struct FaceGeom {
  std::array<std::array<float, 3>, 4> corners;
  std::array<std::array<float, 2>, 4> uvs;
};

[[nodiscard]] FaceGeom FaceGeometryOf(
    const CollectedFace& f,
    const std::vector<ResolvedModel>& models) {
  const ModelFace& mf = models[f.palette_index].faces[f.face_index];
  FaceGeom g;
  const float bx = static_cast<float>(f.block_pos.x);
  const float by = static_cast<float>(f.block_pos.y);
  const float bz = static_cast<float>(f.block_pos.z);
  for (int i = 0; i < 4; ++i) {
    g.corners[i] = {bx + mf.corners[i][0], by + mf.corners[i][1],
                    bz + mf.corners[i][2]};
    g.uvs[i] = mf.uvs[i];
  }
  return g;
}

// Outward normal for collected-face geometry: axis normal for
// directed faces, corner-winding normal for rotated-element faces
// (cull_face 0).
[[nodiscard]] std::array<float, 3> FaceGeomNormal(
    std::uint8_t cull_face, const FaceGeom& g) {
  if (cull_face >= 1 && cull_face <= 6) {
    return NormalForCullFace(cull_face);
  }
  const auto& c = g.corners;
  const float ax = c[2][0] - c[0][0], ay = c[2][1] - c[0][1],
             az = c[2][2] - c[0][2];
  const float bx = c[1][0] - c[0][0], by = c[1][1] - c[0][1],
             bz = c[1][2] - c[0][2];
  float nx = ay * bz - az * by;
  float ny = az * bx - ax * bz;
  float nz = ax * by - ay * bx;
  const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len < 1e-9f) return {0.0f, 1.0f, 0.0f};
  return {nx / len, ny / len, nz / len};
}

// Emits one face as its own quad: its carried uvs (model-side
// convention, mapped per texture tile into the atlas slot), its own
// ao and tint, with the winding fix. Used by the direct path and by
// the merge fallback for rects whose faces disagree on the uv layout
// (mixed blockstate rotations in one group).
void EmitSingleFaceGeom(const std::array<std::array<float, 3>, 4>& corners,
                        const std::array<std::array<float, 2>, 4>& uvs,
                        const std::array<std::uint8_t, 4>& ao,
                        std::uint32_t slot_index,
                        std::uint8_t tint_slot,
                        const std::array<float, 3>& normal,
                        const TextureAtlas& atlas,
                        MeshAppender& out) {
  const TextureAtlas::Slot& slot = atlas.slots[slot_index];
  const float tile_u =
      (slot.u1 - slot.u0) / static_cast<float>(slot.repeats_x);
  const float tile_v =
      (slot.v1 - slot.v0) / static_cast<float>(slot.repeats_y);

  // Atlas-mapped uvs per corner; the tile mapping cannot be expressed
  // as a flat array because each axis scales differently, so the eight
  // floats are materialized per face.
  float uv8[8];
  for (int i = 0; i < 4; ++i) {
    uv8[i * 2 + 0] = slot.u0 + tile_u * uvs[i][0];
    uv8[i * 2 + 1] = slot.v0 + tile_v * uvs[i][1];
  }
  out.AppendQuad(&corners[0][0], normal.data(), uv8, ao.data(), tint_slot);
}

// The affine uv map of one face: how its in-plane world coordinates
// project onto its carried (model-side) uvs, in tile units:
// uv = A * (a1, a2) + b. The slopes A are shared by every face of
// the same block state on the same plane -- a blockstate rotation
// re-orients the texture with the geometry and the layout travels
// per corner -- while b differs per 1x1 tile. ok=false for
// degenerate faces (zero in-plane extent or corners not on the
// bbox, i.e. not an axis-aligned quad).
struct FaceUvMap {
  float au = 0, bu = 0, cu = 0;
  float av = 0, bv = 0, cv = 0;
  bool ok = false;
};

[[nodiscard]] FaceUvMap FitFaceUvMap(const FaceGeom& g, int axis1, int axis2) {
  FaceUvMap m;
  const auto& c = g.corners;
  float p1min = c[0][axis1], p1max = p1min;
  float p2min = c[0][axis2], p2max = p2min;
  for (const auto& corner : c) {
    p1min = std::min(p1min, corner[axis1]);
    p1max = std::max(p1max, corner[axis1]);
    p2min = std::min(p2min, corner[axis2]);
    p2max = std::max(p2max, corner[axis2]);
  }
  const float d1 = p1max - p1min, d2 = p2max - p2min;
  if (d1 < 1e-4f || d2 < 1e-4f) return m;
  int i00 = -1, i01 = -1, i10 = -1, i11 = -1;
  for (int k = 0; k < 4; ++k) {
    const bool hi1 = c[k][axis1] > p1min + d1 * 0.5f;
    const bool hi2 = c[k][axis2] > p2min + d2 * 0.5f;
    int& slot = !hi1 && !hi2 ? i00 : !hi1 && hi2 ? i01
                             : hi1 && !hi2 ? i10 : i11;
    if (slot != -1) return m;  // ambiguous: corners not on the bbox
    slot = k;
  }
  if (i00 < 0 || i01 < 0 || i10 < 0 || i11 < 0) return m;
  m.au = (g.uvs[i10][0] - g.uvs[i00][0]) / d1;
  m.bu = (g.uvs[i01][0] - g.uvs[i00][0]) / d2;
  m.cu = g.uvs[i00][0] - m.au * p1min - m.bu * p2min;
  m.av = (g.uvs[i10][1] - g.uvs[i00][1]) / d1;
  m.bv = (g.uvs[i01][1] - g.uvs[i00][1]) / d2;
  m.cv = g.uvs[i00][1] - m.av * p1min - m.bv * p2min;
  m.ok = true;
  return m;
}

// Whether a face exactly covers its block cell on the merge grid:
// both in-plane extents span the full 1x1 cell of its block. The
// occupancy grid and rect reconstruction in MergeGroup assume unit
// faces, so sub-cell model faces (repeater torch fins, chest
// latches, slab sides, ...) must not take that path -- they would be
// rebuilt as full-cell quads and same-cell coplanar siblings would
// overwrite each other in the single-pointer grid cell. Faces of
// rotated elements (cull_face 0) have no grid direction either.
[[nodiscard]] bool IsUnitCellFace(std::uint8_t cull_face,
                                  const FaceGeom& g) {
  if (cull_face < 1 || cull_face > 6) return false;
  const auto [a1, a2] = InPlaneAxes(cull_face);
  float lo1 = g.corners[0][a1], hi1 = lo1;
  float lo2 = g.corners[0][a2], hi2 = lo2;
  for (const auto& c : g.corners) {
    lo1 = std::min(lo1, c[a1]);
    hi1 = std::max(hi1, c[a1]);
    lo2 = std::min(lo2, c[a2]);
    hi2 = std::max(hi2, c[a2]);
  }
  constexpr float kEps = 1e-4f;
  return hi1 - lo1 >= 1.0f - 2 * kEps && hi2 - lo2 >= 1.0f - 2 * kEps;
}

// Greedy merge for one group of coplanar faces.
void MergeGroup(const std::vector<const CollectedFace*>& group_faces,
                const std::vector<ResolvedModel>& palette_models,
                const TextureAtlas& atlas,
                MeshAppender& out) {
  if (group_faces.empty()) return;

  const std::uint8_t cf = palette_models[group_faces[0]->palette_index]
                             .faces[group_faces[0]->face_index]
                             .CullFace();
  const auto [axis1, axis2] = InPlaneAxes(cf);
  const auto normal = NormalForCullFace(cf);

  // Merge rectangles are capped at the slot's tile capacity: a merged
  // quad addresses its slot in tile units (uv up to the rect extent),
  // so the rect must fit inside the tiled copies the atlas packed for
  // this texture. repeats 1 caps at 1x1 -- the merged quad then equals
  // the single face it replaces.
  const TextureAtlas::Slot& slot0 =
      atlas.slots[group_faces[0]->slot_index];
  const int cap_w = std::max(1, static_cast<int>(slot0.repeats_x));
  const int cap_h = std::max(1, static_cast<int>(slot0.repeats_y));

  // Find grid bounds in the in-plane axes.
  std::int32_t min_a1 = std::numeric_limits<std::int32_t>::max();
  std::int32_t max_a1 = std::numeric_limits<std::int32_t>::min();
  std::int32_t min_a2 = std::numeric_limits<std::int32_t>::max();
  std::int32_t max_a2 = std::numeric_limits<std::int32_t>::min();
  for (const CollectedFace* f : group_faces) {
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

  // Occupancy grid: cell -> face (nullptr = empty). One pointer per
  // cell: a second coplanar same-direction unit face of the same cell
  // (duplicate model elements) would silently drop the first, so the
  // extra is emitted verbatim instead.
  std::vector<const CollectedFace*> grid(
      static_cast<std::size_t>(grid_w) * grid_h, nullptr);
  for (const CollectedFace* f : group_faces) {
    const int a1 = (&f->block_pos.x)[axis1] - min_a1;
    const int a2 = (&f->block_pos.x)[axis2] - min_a2;
    auto& cell = grid[static_cast<std::size_t>(a1) * grid_w + a2];
    if (cell != nullptr) {
      const FaceGeom g = FaceGeometryOf(*f, palette_models);
      const ModelFace& mf = palette_models[f->palette_index].faces[f->face_index];
      EmitSingleFaceGeom(g.corners, g.uvs, f->ao, f->slot_index,
                         mf.TintSlot(), FaceGeomNormal(mf.CullFace(), g),
                         atlas, out);
      continue;
    }
    cell = f;
  }

  std::vector<std::uint8_t> merged(static_cast<std::size_t>(grid_w) * grid_h,
                                   0);

  for (int row = 0; row < grid_h; ++row) {
    for (int col = 0; col < grid_w; ++col) {
      const std::size_t idx = static_cast<std::size_t>(row) * grid_w + col;
      if (merged[idx] || !grid[idx]) continue;

      // Grow right.
      int rect_w = 1;
      while (col + rect_w < grid_w && rect_w < cap_w) {
        const std::size_t next =
            static_cast<std::size_t>(row) * grid_w + (col + rect_w);
        if (merged[next] || !grid[next]) break;
        ++rect_w;
      }

      // Grow down.
      int rect_h = 1;
      bool can_grow = true;
      while (row + rect_h < grid_h && can_grow && rect_h < cap_h) {
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
          merged[static_cast<std::size_t>(row + y) * grid_w +
                 (col + x)] = 1;
        }
      }

      // Corner blocks for AO.
      const CollectedFace* bl =
          grid[static_cast<std::size_t>(row) * grid_w + col];
      const CollectedFace* br =
          grid[static_cast<std::size_t>(row) * grid_w + (col + rect_w - 1)];
      const CollectedFace* tr =
          grid[static_cast<std::size_t>(row + rect_h - 1) * grid_w +
               (col + rect_w - 1)];
      const CollectedFace* tl =
          grid[static_cast<std::size_t>(row + rect_h - 1) * grid_w + col];

      // A 1x1 rectangle merges nothing. Emit it exactly as the direct
      // path would -- its own corners, uv layout and AO -- skipping
      // the affine map fit, the integer rebase and the AO corner
      // matching that the multi-cell reconstruction below needs. This
      // is the dominant case in sparse scenes.
      const FaceGeom bl_geom = FaceGeometryOf(*bl, palette_models);
      if (rect_w == 1 && rect_h == 1) {
        const ModelFace& mf =
            palette_models[bl->palette_index].faces[bl->face_index];
        EmitSingleFaceGeom(bl_geom.corners, bl_geom.uvs, bl->ao,
                           bl->slot_index, mf.TintSlot(), normal, atlas, out);
        continue;
      }

      // Reconstruct quad corners from the rect geometry. Assumes unit
      // cube faces; corner order matches BuildCubeFace's winding.
      const int na = NormalAxis(cf);
      const float na_coord = bl_geom.corners[0][na];
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

      // ---- Carried UV layout ----
      // The merged quad must keep the source faces' uv convention:
      // a blockstate rotation re-orients the texture with the
      // geometry (vanilla never re-lays it by the final world
      // direction). Fit the affine map from the representative face
      // bl and extend it over the rect; every 1x1 cell then shows
      // exactly one texture tile, like the unmerged faces it
      // replaces. The old projection onto ModelFaceAxes of the FINAL
      // cull face silently rotated merged textures of rotated models
      // (a lying barrel's side read upright instead of along the
      // barrel axis).
      const FaceUvMap rep = FitFaceUvMap(bl_geom, axis1, axis2);
      bool layout_ok = rep.ok;
      for (int y = 0; y < rect_h && layout_ok; ++y) {
        for (int x = 0; x < rect_w && layout_ok; ++x) {
          const CollectedFace* f = grid[static_cast<std::size_t>(row + y) *
                                            grid_w +
                                        (col + x)];
          if (f == bl) continue;
          const FaceUvMap fm =
              FitFaceUvMap(FaceGeometryOf(*f, palette_models), axis1, axis2);
          layout_ok = fm.ok && std::abs(fm.au - rep.au) < 1e-4f &&
                      std::abs(fm.bu - rep.bu) < 1e-4f &&
                      std::abs(fm.av - rep.av) < 1e-4f &&
                      std::abs(fm.bv - rep.bv) < 1e-4f;
        }
      }
      if (!layout_ok) {
        // A group can mix block states whose different rotations
        // disagree on the layout; one affine quad cannot represent
        // both, so emit the rect's faces individually, each with its
        // own carried uvs.
        for (int y = 0; y < rect_h; ++y) {
          for (int x = 0; x < rect_w; ++x) {
            const CollectedFace* f =
                grid[static_cast<std::size_t>(row + y) * grid_w + (col + x)];
            const FaceGeom g = FaceGeometryOf(*f, palette_models);
            const ModelFace& mf =
                palette_models[f->palette_index].faces[f->face_index];
            EmitSingleFaceGeom(g.corners, g.uvs, f->ao, f->slot_index,
                               mf.TintSlot(), normal, atlas, out);
          }
        }
        continue;
      }
      std::array<std::array<float, 2>, 4> uvs{};
      {
        float raw[4][2];
        for (int i = 0; i < 4; ++i) {
          raw[i][0] =
              rep.au * corners[i][axis1] + rep.bu * corners[i][axis2] + rep.cu;
          raw[i][1] =
              rep.av * corners[i][axis1] + rep.bv * corners[i][axis2] + rep.cv;
        }
        float u_min = raw[0][0], v_min = raw[0][1];
        for (int i = 1; i < 4; ++i) {
          u_min = std::min(u_min, raw[i][0]);
          v_min = std::min(v_min, raw[i][1]);
        }
        // Integer rebase: anchor the quad at the lower-left tile of
        // the slot. Whole-tile shifts leave every cell's window
        // integer-aligned (the same texture copy), so the per-block
        // image is unchanged; this just guarantees the quad's uv
        // stays within [0, repeats] of the slot instead of running
        // off it (and, pre-rebase, possibly negative).
        const float du = std::floor(u_min);
        const float dv = std::floor(v_min);
        for (int i = 0; i < 4; ++i) {
          uvs[i] = {raw[i][0] - du, raw[i][1] - dv};
        }
      }

      // AO source per corner, in the same order.
      const CollectedFace* src[4] = {
          bl, mode_a ? br : tl, tr, mode_a ? tl : br};
      std::array<std::uint8_t, 4> ao{};
      {
        const FaceGeom src_geom[4] = {FaceGeometryOf(*src[0], palette_models),
                                      FaceGeometryOf(*src[1], palette_models),
                                      FaceGeometryOf(*src[2], palette_models),
                                      FaceGeometryOf(*src[3], palette_models)};
        for (int i = 0; i < 4; ++i) {
          ao[i] = 255;
          for (int k = 0; k < 4; ++k) {
            if (std::abs(src_geom[i].corners[k][axis1] - corners[i][axis1]) <
                    0.001f &&
                std::abs(src_geom[i].corners[k][axis2] - corners[i][axis2]) <
                    0.001f) {
              ao[i] = src[i]->ao[k];
              break;
            }
          }
        }
      }

      const TextureAtlas::Slot& slot = atlas.slots[bl->slot_index];
      const float tile_u =
          (slot.u1 - slot.u0) / static_cast<float>(slot.repeats_x);
      const float tile_v =
          (slot.v1 - slot.v0) / static_cast<float>(slot.repeats_y);
      float uv8[8];
      for (int i = 0; i < 4; ++i) {
        uv8[i * 2 + 0] = slot.u0 + tile_u * uvs[i][0];
        uv8[i * 2 + 1] = slot.v0 + tile_v * uvs[i][1];
      }
      out.AppendQuad(&corners[0][0], normal.data(), uv8, ao.data(),
                     palette_models[bl->palette_index]
                         .faces[bl->face_index]
                         .TintSlot());
    }
  }
}

}  // namespace

// Axis normals indexed by cull_face (1..6): the emit hot path pays a
// table load instead of a switch per face.
constexpr std::array<std::array<float, 3>, 7> kCullFaceNormals = {{
    {{0.0f, 0.0f, 0.0f}},
    {{1.0f, 0.0f, 0.0f}},   // +X
    {{-1.0f, 0.0f, 0.0f}},  // -X
    {{0.0f, 1.0f, 0.0f}},   // +Y
    {{0.0f, -1.0f, 0.0f}},  // -Y
    {{0.0f, 0.0f, 1.0f}},   // +Z
    {{0.0f, 0.0f, -1.0f}},  // -Z
}};

void EmitFaceDirectly(const VisibleFaceInfo& face,
                      const TextureAtlas& atlas,
                      MeshAppender& out) {
  const TextureAtlas::Slot& slot = atlas.slots[face.slot_index];
  const float tile_u =
      (slot.u1 - slot.u0) / static_cast<float>(slot.repeats_x);
  const float tile_v =
      (slot.v1 - slot.v0) / static_cast<float>(slot.repeats_y);
  float uv8[8];
  for (int i = 0; i < 4; ++i) {
    uv8[i * 2 + 0] = slot.u0 + tile_u * face.uvs[i][0];
    uv8[i * 2 + 1] = slot.v0 + tile_v * face.uvs[i][1];
  }
  // Directed faces (the overwhelming majority) read their normal from
  // the table; only rotated-element faces (cull_face 0) derive it
  // from the corner winding.
  std::array<float, 3> winding{};
  const float* normal3;
  if (face.cull_face >= 1 && face.cull_face <= 6) {
    normal3 = kCullFaceNormals[face.cull_face].data();
  } else {
    winding = FaceRecordNormal(face);
    normal3 = winding.data();
  }
  out.AppendQuad(&face.world_corners[0][0], normal3, uv8, face.ao.data(),
                 face.tint_slot);
}

std::array<float, 3> FaceRecordNormal(const VisibleFaceInfo& face) {
  if (face.cull_face >= 1 && face.cull_face <= 6) {
    return kCullFaceNormals[face.cull_face];
  }
  const auto& c = face.world_corners;
  const float ax = c[2][0] - c[0][0], ay = c[2][1] - c[0][1],
             az = c[2][2] - c[0][2];
  const float bx = c[1][0] - c[0][0], by = c[1][1] - c[0][1],
             bz = c[1][2] - c[0][2];
  float nx = ay * bz - az * by;
  float ny = az * bx - ax * bz;
  float nz = ax * by - ay * bx;
  const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len < 1e-9f) return {0.0f, 1.0f, 0.0f};
  return {nx / len, ny / len, nz / len};
}

void EmitFacesDirectly(const std::vector<VisibleFaceInfo>& faces,
                       const TextureAtlas& atlas,
                       Mesh& out_mesh) {
  MeshAppender out = MeshAppender::Attach(out_mesh);
  for (const VisibleFaceInfo& f : faces) {
    EmitFaceDirectly(f, atlas, out);
  }
  out.Finish();
}

void MergeAndEmitFaces(const std::vector<CollectedFace>& faces,
                       const std::vector<ResolvedModel>& palette_models,
                       const TextureAtlas& atlas,
                       Mesh& out_mesh) {
  if (faces.empty()) return;

  MeshAppender out_app = MeshAppender::Attach(out_mesh);
  absl::flat_hash_map<std::uint64_t, std::vector<const CollectedFace*>>
      groups;
  groups.reserve(faces.size() / 8 + 1);
  for (const CollectedFace& f : faces) {
    const ModelFace& mf = palette_models[f.palette_index].faces[f.face_index];
    const FaceGeom g = FaceGeometryOf(f, palette_models);
    if (!IsUnitCellFace(mf.CullFace(), g)) {
      EmitSingleFaceGeom(g.corners, g.uvs, f.ao, f.slot_index, mf.TintSlot(),
                         FaceGeomNormal(mf.CullFace(), g), atlas, out_app);
      continue;
    }
    const int na = NormalAxis(mf.CullFace());
    // Group by the face's ACTUAL plane, quantized to 1/256 blocks:
    // coplanar faces share a group, and faces at different heights in
    // one block column stay apart. The previous block-index key put a
    // half-slab's top (y+0.5) and a neighbouring full block's top
    // (y+1.0) into the same group, mangling both.
    const std::int32_t plane_pos = static_cast<std::int32_t>(
        std::lround(g.corners[0][na] * 256.0f));
    const std::uint64_t key =
        PackGroupKey(mf.CullFace(), plane_pos, f.slot_index, mf.TintSlot(),
                     mf.IsTransparent());
    groups[key].push_back(&f);
  }

  for (const auto& [key, group_faces] : groups) {
    MergeGroup(group_faces, palette_models, atlas, out_app);
  }
  out_app.Finish();
}

}  // namespace fschema::geometry::internal
