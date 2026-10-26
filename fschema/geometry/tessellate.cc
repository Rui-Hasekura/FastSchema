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

#include "fschema/geometry/tessellate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "fast_float/fast_float.h"
#include "fschema/base/error.h"
#include "fschema/geometry/atlas.h"
#include "fschema/geometry/block_volume.h"
#include "fschema/geometry/internal/ao.h"
#include "fschema/geometry/internal/chunk_dispatch.h"
#include "fschema/geometry/internal/cull.h"
#include "fschema/geometry/internal/merge.h"
#include "fschema/geometry/internal/resolver.h"
#include "fschema/geometry/resource_pack.h"
#include "fschema/geometry/types.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"
#include "tbb/blocked_range.h"
#include "tbb/enumerable_thread_specific.h"
#include "tbb/parallel_for.h"

namespace fschema::geometry {

namespace {

[[nodiscard]] constexpr filters::LocalPos NeighborInCullDir(
    filters::LocalPos p,
    std::uint8_t cull_face) noexcept {
  switch (static_cast<internal::FaceDir>(cull_face)) {
    case internal::FaceDir::kPosX:
      return {p.x + 1, p.y, p.z};
    case internal::FaceDir::kNegX:
      return {p.x - 1, p.y, p.z};
    case internal::FaceDir::kPosY:
      return {p.x, p.y + 1, p.z};
    case internal::FaceDir::kNegY:
      return {p.x, p.y - 1, p.z};
    case internal::FaceDir::kPosZ:
      return {p.x, p.y, p.z + 1};
    case internal::FaceDir::kNegZ:
      return {p.x, p.y, p.z - 1};
    default:
      return p;
  }
}

// The cull/AO group of the neighbor in the cull direction: 0 for air
// and out-of-bounds, 1 for opaque, 2+ for a transparent group.
[[nodiscard]] std::uint8_t NeighborGroup(const internal::BlockGroupTable& table,
                                         const ir::BoundingBox& bounds,
                                         filters::LocalPos p,
                                         std::uint8_t cull_face) noexcept {
  if (cull_face == 0) return 0;
  const filters::LocalPos nb = NeighborInCullDir(p, cull_face);
  if (nb.x < 0 || nb.x >= bounds.size[0] || nb.y < 0 ||
      nb.y >= bounds.size[1] || nb.z < 0 || nb.z >= bounds.size[2]) {
    return 0;
  }
  return table.bytes[filters::LinearIndex(nb, bounds)];
}

// The opposite FaceDir byte (kPosX <-> kNegX etc.); cull_face 0 and any
// non-direction value pass through unchanged. Fluid back faces flip
// their cull face so the exported normal matches the visible side.
[[nodiscard]] constexpr std::uint8_t OppositeCullFace(
    std::uint8_t cull_face) noexcept {
  switch (static_cast<internal::FaceDir>(cull_face)) {
    case internal::FaceDir::kPosX:
      return static_cast<std::uint8_t>(internal::FaceDir::kNegX);
    case internal::FaceDir::kNegX:
      return static_cast<std::uint8_t>(internal::FaceDir::kPosX);
    case internal::FaceDir::kPosY:
      return static_cast<std::uint8_t>(internal::FaceDir::kNegY);
    case internal::FaceDir::kNegY:
      return static_cast<std::uint8_t>(internal::FaceDir::kPosY);
    case internal::FaceDir::kPosZ:
      return static_cast<std::uint8_t>(internal::FaceDir::kNegZ);
    case internal::FaceDir::kNegZ:
      return static_cast<std::uint8_t>(internal::FaceDir::kPosZ);
    default:
      return cull_face;
  }
}

// The leaves family (every *_leaves block plus azalea and
// flowering_azalea, whose class is AzaleaLeavesBlock even though the
// name lacks the suffix). Name-classified like the overlay classifier.
[[nodiscard]] bool IsLeavesBlock(std::string_view block_name) noexcept {
  const std::string_view n = internal::StripNamespace(block_name);
  return n == "azalea" || n == "flowering_azalea" || n.ends_with("_leaves");
}

// Cull/AO group per palette entry: 0 = air, 1 = opaque, 2+ = ids shared
// by same-name transparent blocks (water culls against water, glass
// against glass, but neither culls other blocks' faces). Leaves share
// the single non-self-culling group kNonCullingGroupBase: fancy-graphics
// leaves hide neither neighbors' faces nor each other's (vanilla
// LeavesBlock overrides no skipRendering and is not an occluder), so
// they must not consume a self-culling id.
[[nodiscard]] std::vector<std::uint8_t> BuildPaletteGroups(
    const ir::Region& region,
    const std::vector<ResolvedModel>& palette_models) {
  std::vector<std::uint8_t> groups(region.palette.size(), 1);
  absl::flat_hash_map<std::string_view, std::uint8_t> transparent_ids;
  std::uint8_t next_id = 2;
  for (std::size_t i = 0; i < region.palette.size(); ++i) {
    if (region.IsAir(static_cast<std::uint16_t>(i))) {
      groups[i] = 0;
    } else if (i < palette_models.size() && palette_models[i].is_transparent) {
      if (IsLeavesBlock(region.palette[i].name)) {
        groups[i] = internal::kNonCullingGroupBase;
        continue;
      }
      auto [it, inserted] =
          transparent_ids.emplace(region.palette[i].name, next_id);
      if (inserted && next_id < 255) ++next_id;
      groups[i] = it->second;
    }
  }
  return groups;
}

// Fluid slopes (vanilla FluidRenderer port).

[[nodiscard]] constexpr bool InBounds(const ir::BoundingBox& b,
                                      filters::LocalPos p) noexcept {
  return p.x >= 0 && p.x < b.size[0] && p.y >= 0 && p.y < b.size[1] &&
         p.z >= 0 && p.z < b.size[2];
}

// Palette-level fluid info from the block name and `level` property:
// kind (1 = water, 2 = lava) and vanilla fluid amount (source = 8,
// flowing L = 8 - L, falling L = 16 - L; FlowingFluid#getLegacyLevel).
struct FluidPaletteInfo {
  std::uint8_t kind = 0;
  std::uint8_t amount = 0;
  // Only blocks whose faces came from SynthesizeFluidFaces are
  // morphable; a pack-provided water model keeps its own geometry.
  bool morphable = false;
};

[[nodiscard]] std::uint8_t FluidKindForName(std::string_view name) {
  const std::string_view n = internal::StripNamespace(name);
  if (n == "water" || n == "flowing_water") return 1;
  if (n == "lava" || n == "flowing_lava") return 2;
  return 0;
}

[[nodiscard]] std::uint8_t FluidAmountFromProps(const BlockProps& props) {
  std::int64_t level = 0;  // missing/unparsable -> source
  if (auto lv = props.Get("level"); lv.has_value() && !lv->empty()) {
    const char* first = lv->data();
    const char* last = first + lv->size();
    fast_float::from_chars(first, last, level);
  }
  if (level < 0) level = 0;
  if (level > 15) level = 15;
  const int amount =
      level >= 8 ? 16 - static_cast<int>(level) : 8 - static_cast<int>(level);
  return static_cast<std::uint8_t>(amount);
}

// Vanilla FluidRenderer: a fluid side touching a HalfTransparentBlock
// (glass, tinted/stained glass, ice, frosted ice, honey) or a
// LeavesBlock swaps its sprite to the overlay texture. Name-classified
// like the render-layer classifier; glass panes and slime are never
// overlay-eligible in vanilla.
[[nodiscard]] bool BlockGetsFluidOverlay(std::string_view name) noexcept {
  const std::string_view n = internal::StripNamespace(name);
  if (n == "glass" || n == "tinted_glass" || n == "ice" || n == "frosted_ice" ||
      n == "honey_block" || n == "azalea" || n == "flowering_azalea") {
    return true;
  }
  return n.ends_with("_stained_glass") || n.ends_with("_leaves");
}

// Overlay context shared by one tessellation pass (read-only after
// build): the fixed overlay texture path -- empty when the pack ships
// none; lava never gets one -- plus a per-palette eligibility byte for
// the neighbor check (vanilla swaps the sprite per SIDE, so the flag
// is consulted per face direction, not per block).
struct FluidOverlayInfo {
  std::string_view texture;  // "" = pack ships no water_overlay
  std::span<const std::uint8_t> palette_eligible;  // per palette entry
};

[[nodiscard]] std::vector<FluidPaletteInfo> BuildPaletteFluid(
    const ir::Region& region,
    const std::vector<ResolvedModel>& palette_models) {
  std::vector<FluidPaletteInfo> fluid(region.palette.size());
  for (std::size_t i = 0; i < region.palette.size(); ++i) {
    const std::uint8_t kind = FluidKindForName(region.palette[i].name);
    if (kind == 0 || i >= palette_models.size()) continue;
    bool morphable = false;
    for (const ModelFace& f : palette_models[i].faces) {
      if (f.IsFluidFace()) {
        morphable = true;
        break;
      }
    }
    if (!morphable) continue;
    fluid[i].kind = kind;
    fluid[i].amount = FluidAmountFromProps(BlockProps(region.palette[i]));
    fluid[i].morphable = true;
  }
  return fluid;
}

// Per-cell fluid byte: 0xFF = none, else (kind << 4) | amount. Same
// shape as the group table (one byte per block, LinearIndex order);
// read-only after build, shared with the TBB workers.
[[nodiscard]] std::vector<std::uint8_t> BuildFluidTable(
    const ir::Region& region,
    const std::vector<FluidPaletteInfo>& palette_fluid) {
  std::vector<std::uint8_t> table(region.block_indices.size(), 0xFF);
  for (std::size_t i = 0; i < region.block_indices.size(); ++i) {
    const std::uint16_t pal = region.block_indices[i];
    if (pal >= palette_fluid.size()) continue;
    const FluidPaletteInfo& f = palette_fluid[pal];
    if (!f.morphable) continue;
    table[i] = static_cast<std::uint8_t>((f.kind << 4) | f.amount);
  }
  return table;
}

// Vanilla FluidRenderer#getHeight: same-fluid cell -> 1.0 when the
// same fluid continues above, else amount/9; a different fluid
// behaves like air (fluid blocks carry no collision -> 0.0F); any
// other non-fluid cell WITH collision (isSolid: opaque blocks and
// glass/ice/stained glass/panes/slime/honey alike) returns the -1.0
// sentinel and drops out of the averaging; only air and out-of-
// bounds cells contribute 0.0 (a source pool stays flat right up
// against a glass wall, matching vanilla).
[[nodiscard]] float FluidCellHeight(
    const std::vector<std::uint8_t>& fluid_table,
    const internal::BlockGroupTable& group_table,
    const ir::BoundingBox& bounds,
    filters::LocalPos p,
    std::uint8_t kind) noexcept {
  if (!InBounds(bounds, p)) return 0.0f;  // out of bounds: like air
  const std::size_t linear = filters::LinearIndex(p, bounds);
  const std::uint8_t cell = fluid_table[linear];
  if (cell != 0xFF) {
    if ((cell >> 4) == kind) {
      const filters::LocalPos above{p.x, p.y + 1, p.z};
      bool above_same = false;
      if (InBounds(bounds, above)) {
        const std::uint8_t up =
            fluid_table[filters::LinearIndex(above, bounds)];
        above_same = up != 0xFF && (up >> 4) == kind;
      }
      return above_same ? 1.0f : static_cast<float>(cell & 0x0F) / 9.0f;
    }
    return 0.0f;  // a different fluid never blocks the average
  }
  return group_table.bytes[linear] == 0 ? 0.0f : -1.0f;
}

// Vanilla FluidState#shouldRenderBackwardUpFace: the fluid up face
// needs a back copy visible from below unless every cell of the 3x3
// neighborhood at the layer above continues the same fluid or is
// solid-render; out-of-bounds acts like air, as in every other probe
// in this port. The full scan is kept even though the simplified cull
// model lets the center cell alone decide, so the semantics stay
// exact if culling ever gains vanilla's occluder nuance.
[[nodiscard]] bool ShouldRenderBackwardUpFace(
    const std::vector<std::uint8_t>& fluid_table,
    const internal::BlockGroupTable& group_table,
    const ir::BoundingBox& bounds,
    filters::LocalPos p,
    std::uint8_t kind) noexcept {
  for (int dz = -1; dz <= 1; ++dz) {
    for (int dx = -1; dx <= 1; ++dx) {
      const filters::LocalPos q{p.x + dx, p.y + 1, p.z + dz};
      if (!InBounds(bounds, q)) return true;  // out of bounds: like air
      const std::size_t linear = filters::LinearIndex(q, bounds);
      const std::uint8_t cell = fluid_table[linear];
      if (cell != 0xFF) {
        if ((cell >> 4) != kind) return true;  // a different fluid
        continue;                              // same fluid continues
      }
      if (group_table.bytes[linear] != 1) return true;  // not solid-render
    }
  }
  return false;
}

// Vanilla FluidRenderer#addWeightedHeight: heights >= 0.8 (a source
// or a full falling column, amount >= 8) carry 10x weight so flat
// source pools stay flat; the -1.0 solid sentinel is excluded.
void AddWeightedHeight(float& sum, float& weight, float h) noexcept {
  if (h >= 0.8f) {
    sum += h * 10.0f;
    weight += 10.0f;
  } else if (h >= 0.0f) {
    sum += h;
    weight += 1.0f;
  }
}

// Vanilla FluidRenderer#calculateAverageHeight.
[[nodiscard]] float CornerHeight(float h_self,
                                 float h_a,
                                 float h_b,
                                 float h_diag) noexcept {
  if (h_a >= 1.0f || h_b >= 1.0f) return 1.0f;
  float sum = 0.0f;
  float weight = 0.0f;
  if (h_a > 0.0f || h_b > 0.0f) {
    if (h_diag >= 1.0f) return 1.0f;
    AddWeightedHeight(sum, weight, h_diag);
  }
  AddWeightedHeight(sum, weight, h_self);
  AddWeightedHeight(sum, weight, h_a);
  AddWeightedHeight(sum, weight, h_b);
  if (weight <= 0.0f) return h_self;  // defensive; self is always > 0
  return sum / weight;
}

// The four corner heights in BuildCubeFace up-face order:
// {NW (x0,z0), NE (x1,z0), SE (x1,z1), SW (x0,z1)}.
struct FluidCornerHeights {
  float nw = 0.0f, ne = 0.0f, se = 0.0f, sw = 0.0f;
};

[[nodiscard]] FluidCornerHeights FluidCornersAt(
    const std::vector<std::uint8_t>& fluid_table,
    const internal::BlockGroupTable& group_table,
    const ir::BoundingBox& bounds,
    filters::LocalPos p,
    std::uint8_t kind) noexcept {
  const float h_self =
      FluidCellHeight(fluid_table, group_table, bounds, p, kind);
  if (h_self >= 1.0f) {
    return FluidCornerHeights{1.0f, 1.0f, 1.0f, 1.0f};
  }
  const float h_n = FluidCellHeight(
      fluid_table, group_table, bounds, {p.x, p.y, p.z - 1}, kind);
  const float h_s = FluidCellHeight(
      fluid_table, group_table, bounds, {p.x, p.y, p.z + 1}, kind);
  const float h_w = FluidCellHeight(
      fluid_table, group_table, bounds, {p.x - 1, p.y, p.z}, kind);
  const float h_e = FluidCellHeight(
      fluid_table, group_table, bounds, {p.x + 1, p.y, p.z}, kind);
  const float h_nw = FluidCellHeight(
      fluid_table, group_table, bounds, {p.x - 1, p.y, p.z - 1}, kind);
  const float h_ne = FluidCellHeight(
      fluid_table, group_table, bounds, {p.x + 1, p.y, p.z - 1}, kind);
  const float h_se = FluidCellHeight(
      fluid_table, group_table, bounds, {p.x + 1, p.y, p.z + 1}, kind);
  const float h_sw = FluidCellHeight(
      fluid_table, group_table, bounds, {p.x - 1, p.y, p.z + 1}, kind);
  return FluidCornerHeights{/*nw=*/CornerHeight(h_self, h_n, h_w, h_nw),
                            /*ne=*/CornerHeight(h_self, h_n, h_e, h_ne),
                            /*se=*/CornerHeight(h_self, h_s, h_e, h_se),
                            /*sw=*/CornerHeight(h_self, h_s, h_w, h_sw)};
}

// Vanilla FlowingFluid#getFlow, horizontal part: the flow direction
// accumulates own-height differences against horizontal neighbors. A
// different fluid never contributes (affectsFlow); a fluidless
// neighbor contributes only when it does not block motion and the
// same fluid continues below it (a hole pulls with extra weight,
// distance = own - (below - 8/9)). The FALLING branch only adds a y
// component the renderer ignores, so it is omitted; out-of-bounds
// cells act like air with air below (no contribution).
[[nodiscard]] std::pair<float, float> FluidFlowAt(
    const std::vector<std::uint8_t>& fluid_table,
    const internal::BlockGroupTable& group_table,
    const ir::BoundingBox& bounds,
    filters::LocalPos p,
    std::uint8_t kind) noexcept {
  float fx = 0.0f;
  float fz = 0.0f;
  const std::uint8_t own = fluid_table[filters::LinearIndex(p, bounds)];
  const float own_h = static_cast<float>(own & 0x0F) / 9.0f;
  const std::int32_t dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (const auto& d : dirs) {
    const filters::LocalPos q{p.x + d[0], p.y, p.z + d[1]};
    if (!InBounds(bounds, q)) continue;
    const std::uint8_t cell = fluid_table[filters::LinearIndex(q, bounds)];
    float distance = 0.0f;
    if (cell != 0xFF) {
      if ((cell >> 4) != kind) continue;  // different fluid: no effect
      const float nh = static_cast<float>(cell & 0x0F) / 9.0f;
      if (nh > 0.0f) distance = own_h - nh;
    } else if (group_table.bytes[filters::LinearIndex(q, bounds)] == 0) {
      // Fluidless, motion-free neighbor: a hole (same fluid below)
      // pulls toward it (FlowingFluid#getFlow).
      const filters::LocalPos below{q.x, q.y - 1, q.z};
      if (InBounds(bounds, below)) {
        const std::uint8_t b = fluid_table[filters::LinearIndex(below, bounds)];
        if (b != 0xFF && (b >> 4) == kind) {
          const float bh = static_cast<float>(b & 0x0F) / 9.0f;
          if (bh > 0.0f) distance = own_h - (bh - 8.0f / 9.0f);
        }
      }
    }
    if (distance != 0.0f) {
      fx += static_cast<float>(d[0]) * distance;
      fz += static_cast<float>(d[1]) * distance;
    }
  }
  return {fx, fz};
}

// Everything the per-block fluid morph needs: corner heights, the
// flow texture (empty when the pack ships none) and the still-vs-
// flowing decision for the top face. `flowing` is only ever true
// when `flow_texture` is non-empty (rotation targets that texture).
struct FluidMorph {
  FluidCornerHeights corners;
  std::string_view flow_texture;
  std::string_view overlay_texture;       // water only; "" = pack has none
  std::array<float, 2> flow{0.0f, 0.0f};  // (x, z) direction
  bool flowing = false;
};

// Flow texture of a synthesized fluid model, derived from the model
// itself: sides carry the flow texture when the pack ships it
// (SynthesizeFluidFaces), and the down face always keeps the still
// texture. Equal paths mean the pack ships no flow texture, so the
// top face never swaps and sides keep the full uv rect.
[[nodiscard]] std::string_view FluidFlowTextureOf(
    const ResolvedModel& model) noexcept {
  std::string_view side;
  std::string_view down;
  for (const ModelFace& f : model.faces) {
    if (!f.IsFluidFace() || f.texture_path.empty()) continue;
    const auto dir = static_cast<internal::FaceDir>(f.CullFace());
    if (dir == internal::FaceDir::kNegY) {
      down = f.texture_path;
    } else if (dir != internal::FaceDir::kPosY && side.empty()) {
      side = f.texture_path;
    }
  }
  return side == down ? std::string_view{} : side;
}

[[nodiscard]] FluidMorph BuildFluidMorph(
    const ResolvedModel& model,
    const FluidPaletteInfo& fluid,
    const std::vector<std::uint8_t>& fluid_table,
    const internal::BlockGroupTable& group_table,
    const ir::BoundingBox& bounds,
    filters::LocalPos p,
    std::string_view overlay_texture) noexcept {
  FluidMorph m;
  if (!fluid.morphable) return m;
  m.corners = FluidCornersAt(fluid_table, group_table, bounds, p, fluid.kind);
  // Only water's fluid model carries an overlay material in vanilla.
  m.overlay_texture = fluid.kind == 1 ? overlay_texture : std::string_view{};
  m.flow_texture = FluidFlowTextureOf(model);
  if (m.flow_texture.empty()) return m;
  const auto [fx, fz] =
      FluidFlowAt(fluid_table, group_table, bounds, p, fluid.kind);
  m.flow = {fx, fz};
  m.flowing = fx != 0.0f || fz != 0.0f;
  return m;
}

// Morphs a synthesized fluid cuboid face in place (vanilla
// FluidRenderer#tesselate): the up face takes the four corner
// heights, side faces slope their top edge along them, the down face
// stays at y = 0. With a flow texture, sides sample vanilla's flow
// window (u 0..1/2, v (1-h)/2 at the surface and 1/2 at the block
// bottom) and a flowing top swaps to the flow texture with the
// sampling window rotated toward the flow direction. Corner-index
// maps follow BuildCubeFace's winding per direction.
void MorphFluidFace(ModelFace& face, const FluidMorph& fm, bool overlay_side) {
  const FluidCornerHeights& h = fm.corners;
  switch (static_cast<internal::FaceDir>(face.CullFace())) {
    case internal::FaceDir::kPosY:
      face.corners[0][1] = h.nw;
      face.corners[1][1] = h.ne;
      face.corners[2][1] = h.se;
      face.corners[3][1] = h.sw;
      if (fm.flowing) {
        // Window centered at (1/2, 1/2) with radius 1/4, rotated by
        // atan2(flow.z, flow.x) - pi/2.
        const float angle = std::atan2(fm.flow[1], fm.flow[0]) -
                            std::numbers::pi_v<float> * 0.5f;
        const float s = std::sin(angle) * 0.25f;
        const float c = std::cos(angle) * 0.25f;
        face.uvs = {{
            {{0.5f - c - s, 0.5f - c + s}},  // NW
            {{0.5f + c - s, 0.5f - c - s}},  // NE
            {{0.5f + c + s, 0.5f + c - s}},  // SE
            {{0.5f - c + s, 0.5f + c + s}},  // SW
        }};
        face.texture_path.assign(fm.flow_texture);
      }
      break;
    case internal::FaceDir::kNegZ:  // north: top edge = NW, NE
      face.corners[3][1] = h.nw;
      face.corners[2][1] = h.ne;
      if (overlay_side || !fm.flow_texture.empty()) {
        face.uvs = {{
            {{0.0f, 0.5f}},                  // bottom, x0 (NW)
            {{0.5f, 0.5f}},                  // bottom, x1 (NE)
            {{0.5f, (1.0f - h.ne) * 0.5f}},  // top, NE
            {{0.0f, (1.0f - h.nw) * 0.5f}},  // top, NW
        }};
      }
      if (overlay_side) {
        face.texture_path.assign(fm.overlay_texture);
      }
      break;
    case internal::FaceDir::kPosZ:  // south: top edge = SW, SE
      face.corners[1][1] = h.sw;
      face.corners[2][1] = h.se;
      if (overlay_side || !fm.flow_texture.empty()) {
        face.uvs = {{
            {{0.5f, 0.5f}},                  // bottom, x0 (SW)
            {{0.5f, (1.0f - h.sw) * 0.5f}},  // top, SW
            {{0.0f, (1.0f - h.se) * 0.5f}},  // top, SE
            {{0.0f, 0.5f}},                  // bottom, x1 (SE)
        }};
      }
      if (overlay_side) {
        face.texture_path.assign(fm.overlay_texture);
      }
      break;
    case internal::FaceDir::kNegX:  // west: top edge = NW, SW
      face.corners[1][1] = h.nw;
      face.corners[2][1] = h.sw;
      if (overlay_side || !fm.flow_texture.empty()) {
        face.uvs = {{
            {{0.5f, 0.5f}},                  // bottom, z0 (NW)
            {{0.5f, (1.0f - h.nw) * 0.5f}},  // top, NW
            {{0.0f, (1.0f - h.sw) * 0.5f}},  // top, SW
            {{0.0f, 0.5f}},                  // bottom, z1 (SW)
        }};
      }
      if (overlay_side) {
        face.texture_path.assign(fm.overlay_texture);
      }
      break;
    case internal::FaceDir::kPosX:  // east: top edge = NE, SE
      face.corners[2][1] = h.se;
      face.corners[3][1] = h.ne;
      if (overlay_side || !fm.flow_texture.empty()) {
        face.uvs = {{
            {{0.0f, 0.5f}},                  // bottom, z0 (NE)
            {{0.5f, 0.5f}},                  // bottom, z1 (SE)
            {{0.5f, (1.0f - h.se) * 0.5f}},  // top, SE
            {{0.0f, (1.0f - h.ne) * 0.5f}},  // top, NE
        }};
      }
      if (overlay_side) {
        face.texture_path.assign(fm.overlay_texture);
      }
      break;
    case internal::FaceDir::kNegY:
    default:
      break;  // bottom stays at y = 0, full still-texture rect
  }
}

// Builds one visible-face record (world-space corners, UVs, per-vertex
// AO). `local_pos` indexes the group table for AO; `world_pos` is the
// corner base (the two differ in TessellateSchematic, where regions
// carry world origins). Returned so callers can push derived copies
// (fluid back faces re-emit the same record reversed).
[[nodiscard]] internal::VisibleFaceInfo BuildFaceRecord(
    const ModelFace& face,
    filters::LocalPos local_pos,
    filters::LocalPos world_pos,
    std::uint32_t slot_index,
    const internal::BlockGroupTable& group_table,
    const ir::BoundingBox& bounds,
    float ao_strength) {
  internal::VisibleFaceInfo info;
  info.block_pos = world_pos;
  info.cull_face = face.CullFace();
  info.slot_index = slot_index;
  info.tint_slot = face.TintSlot();
  info.is_transparent = face.IsTransparent();
  info.is_fluid = face.IsFluidFace();

  for (int i = 0; i < 4; ++i) {
    info.world_corners[i] = {
        static_cast<float>(world_pos.x) + face.corners[i][0],
        static_cast<float>(world_pos.y) + face.corners[i][1],
        static_cast<float>(world_pos.z) + face.corners[i][2]};
    info.uvs[i] = face.uvs[i];
    info.ao[i] = internal::ComputeVertexAO(group_table,
                                           bounds,
                                           local_pos,
                                           face.CullFace(),
                                           face.corners[i],
                                           ao_strength);
  }
  return info;
}

// Vanilla FluidRenderer#addFace: a back face re-emits the same
// four vertices in reverse order (3, 2, 1, 0) with identical uvs,
// color, and light, flipping the winding so the quad is visible from
// the other side. The reversed copy keeps the front's AO and tint per
// vertex; its cull face flips so the exported normal matches the
// visible orientation (FastSchema normals are per-face, unlike
// vanilla's hardcoded up for every fluid vertex).
void CollectFluidBackFace(
    const internal::VisibleFaceInfo& front,
    std::vector<internal::VisibleFaceInfo>& out_opaque,
    std::vector<internal::VisibleFaceInfo>& out_transparent) {
  internal::VisibleFaceInfo back = front;
  for (int i = 0; i < 4; ++i) {
    back.world_corners[i] = front.world_corners[3 - i];
    back.uvs[i] = front.uvs[3 - i];
    back.ao[i] = front.ao[3 - i];
  }
  back.cull_face = OppositeCullFace(front.cull_face);
  if (back.is_transparent)
    out_transparent.push_back(std::move(back));
  else
    out_opaque.push_back(std::move(back));
}

void ProcessBlock(
    filters::LocalPos p,
    const std::vector<ResolvedModel>& palette_models,
    const ir::Region& region,
    const std::vector<std::uint8_t>& palette_groups,
    const std::vector<FluidPaletteInfo>& palette_fluid,
    const FluidOverlayInfo& overlay,
    const std::vector<std::uint8_t>& fluid_table,
    const internal::BlockGroupTable& group_table,
    const absl::flat_hash_map<std::string, std::uint32_t>& texture_to_slot,
    const TessellationOptions& opts,
    std::uint8_t pos_x_group,
    std::uint8_t neg_x_group,
    std::vector<internal::VisibleFaceInfo>& out_opaque,
    std::vector<internal::VisibleFaceInfo>& out_transparent) {
  const auto pal_idx =
      region.block_indices[filters::LinearIndex(p, region.bounds)];
  if (region.IsAir(pal_idx)) return;

  const ResolvedModel& model = palette_models[pal_idx];
  const std::uint8_t cur_group = palette_groups[pal_idx];
  const float ao_strength = opts.ambient_occlusion ? opts.ao_strength : 0.0f;

  // Synthesized fluid faces are morphed per block: their corner
  // heights and flow direction depend on fluid neighbors, which the
  // per-state model cache cannot express (vanilla
  // FluidRenderer#tesselate).
  const FluidPaletteInfo& fluid = palette_fluid[pal_idx];
  const FluidMorph fluid_m = BuildFluidMorph(model,
                                             fluid,
                                             fluid_table,
                                             group_table,
                                             region.bounds,
                                             p,
                                             overlay.texture);
  ModelFace morphed;
  for (const ModelFace& face : model.faces) {
    const ModelFace* fp = &face;
    // Overlay flag is consulted per horizontal side (vanilla swaps the
    // sprite per face direction); up/down faces never overlay. Hoisted
    // out of the morph branch because the back-face decision below
    // needs it too.
    bool overlay_side = false;
    if (fluid.morphable && face.IsFluidFace()) {
      morphed = face;
      if (!fluid_m.overlay_texture.empty()) {
        const std::uint8_t dir = face.CullFace();
        if (dir != static_cast<std::uint8_t>(internal::FaceDir::kPosY) &&
            dir != static_cast<std::uint8_t>(internal::FaceDir::kNegY)) {
          const filters::LocalPos nb = NeighborInCullDir(p, dir);
          if (InBounds(region.bounds, nb)) {
            overlay_side =
                overlay
                    .palette_eligible[region.block_indices[filters::LinearIndex(
                        nb, region.bounds)]];
          }
        }
      }
      MorphFluidFace(morphed, fluid_m, overlay_side);
      fp = &morphed;
    }
    if (opts.cull_interior_faces && fp->CullFace() != 0) {
      std::uint8_t nb_group;
      switch (static_cast<internal::FaceDir>(fp->CullFace())) {
        case internal::FaceDir::kPosX:
          nb_group = pos_x_group;
          break;
        case internal::FaceDir::kNegX:
          nb_group = neg_x_group;
          break;
        default:
          nb_group =
              NeighborGroup(group_table, region.bounds, p, fp->CullFace());
      }
      if (internal::NeighborCulls(nb_group, cur_group)) continue;
    }
    if (fp->texture_path.empty()) continue;
    auto slot_it = texture_to_slot.find(fp->texture_path);
    if (slot_it == texture_to_slot.end()) continue;

    internal::VisibleFaceInfo info = BuildFaceRecord(
        *fp, p, p, slot_it->second, group_table, region.bounds, ao_strength);
    if (info.is_transparent) {
      out_transparent.push_back(info);
    } else {
      out_opaque.push_back(info);
    }
    // Vanilla FluidRenderer#addFace doubles every emitted fluid face
    // with a reversed-winding copy except the down face and overlay
    // sides: sides pass addBackFace = !isOverlay, the up face only
    // while shouldRenderBackwardUpFace holds.
    if (fluid.morphable && face.IsFluidFace()) {
      const auto dir = static_cast<internal::FaceDir>(fp->CullFace());
      bool back = false;
      if (dir == internal::FaceDir::kPosY) {
        back = ShouldRenderBackwardUpFace(
            fluid_table, group_table, region.bounds, p, fluid.kind);
      } else if (dir != internal::FaceDir::kNegY) {
        back = !overlay_side;
      }
      if (back) CollectFluidBackFace(info, out_opaque, out_transparent);
    }
  }
}

void ProcessRow(
    filters::LocalPos x_start_pos,
    std::int32_t y,
    std::int32_t z,
    std::int32_t x_count,
    const std::vector<ResolvedModel>& palette_models,
    const ir::Region& region,
    const std::vector<std::uint8_t>& palette_groups,
    const std::vector<FluidPaletteInfo>& palette_fluid,
    const FluidOverlayInfo& overlay,
    const std::vector<std::uint8_t>& fluid_table,
    const internal::BlockGroupTable& group_table,
    const absl::flat_hash_map<std::string, std::uint32_t>& texture_to_slot,
    const TessellationOptions& opts,
    std::vector<internal::VisibleFaceInfo>& out_opaque,
    std::vector<internal::VisibleFaceInfo>& out_transparent) {
  if (x_count <= 0) return;

  if (!opts.cull_interior_faces) {
    for (std::int32_t i = 0; i < x_count; ++i) {
      ProcessBlock({x_start_pos.x + i, y, z},
                   palette_models,
                   region,
                   palette_groups,
                   palette_fluid,
                   overlay,
                   fluid_table,
                   group_table,
                   texture_to_slot,
                   opts,
                   0,
                   0,
                   out_opaque,
                   out_transparent);
    }
    return;
  }

  const std::size_t base_linear =
      filters::LinearIndex({x_start_pos.x, y, z}, region.bounds);
  const std::size_t row = static_cast<std::size_t>(x_count);
  const std::uint32_t region_row =
      static_cast<std::uint32_t>(region.bounds.size[0]);

  std::vector<std::uint8_t> pos_x_group(row);
  std::vector<std::uint8_t> neg_x_group(row);
  internal::ComputePosXNeighborGroupBatch(
      group_table, base_linear, row, region_row, pos_x_group.data());
  internal::ComputeNegXNeighborGroupBatch(
      group_table, base_linear, row, region_row, neg_x_group.data());

  for (std::int32_t i = 0; i < x_count; ++i) {
    ProcessBlock({x_start_pos.x + i, y, z},
                 palette_models,
                 region,
                 palette_groups,
                 palette_fluid,
                 overlay,
                 fluid_table,
                 group_table,
                 texture_to_slot,
                 opts,
                 pos_x_group[static_cast<std::size_t>(i)],
                 neg_x_group[static_cast<std::size_t>(i)],
                 out_opaque,
                 out_transparent);
  }
}

void AppendMesh(Mesh& dst, const Mesh& src) {
  const std::uint32_t base = static_cast<std::uint32_t>(dst.VertexCount());
  dst.positions.insert(
      dst.positions.end(), src.positions.begin(), src.positions.end());
  dst.normals.insert(dst.normals.end(), src.normals.begin(), src.normals.end());
  dst.uvs.insert(dst.uvs.end(), src.uvs.begin(), src.uvs.end());
  dst.ao.insert(dst.ao.end(), src.ao.begin(), src.ao.end());
  dst.tints.insert(dst.tints.end(), src.tints.begin(), src.tints.end());
  dst.indices.reserve(dst.indices.size() + src.indices.size());
  for (auto idx : src.indices) {
    dst.indices.push_back(idx + base);
  }
}

// Emit collected faces into the target mesh. Dispatches to direct emit
// or greedy merge based on opts.merge_coplanar_faces.
void EmitCollectedFaces(
    std::vector<internal::VisibleFaceInfo>& opaque_faces,
    std::vector<internal::VisibleFaceInfo>& transparent_faces,
    const TextureAtlas& atlas,
    const TessellationOptions& opts,
    Mesh& out_opaque,
    Mesh& out_transparent) {
  if (opts.merge_coplanar_faces) {
    // Fluid faces carry sub-block/sloped corners; MergeGroup rebuilds
    // quads from block-grid bounds and would flatten them. Splice them
    // out and emit them directly, per output mesh.
    std::vector<internal::VisibleFaceInfo> opaque_fluid;
    std::vector<internal::VisibleFaceInfo> transparent_fluid;
    const auto splice_fluid =
        [](std::vector<internal::VisibleFaceInfo>& v,
           std::vector<internal::VisibleFaceInfo>& out_fluid) {
          std::vector<internal::VisibleFaceInfo> keep;
          keep.reserve(v.size());
          for (auto& f : v) {
            if (f.is_fluid)
              out_fluid.push_back(std::move(f));
            else
              keep.push_back(std::move(f));
          }
          v = std::move(keep);
        };
    splice_fluid(opaque_faces, opaque_fluid);
    splice_fluid(transparent_faces, transparent_fluid);
    internal::MergeAndEmitFaces(opaque_faces, atlas, out_opaque);
    internal::MergeAndEmitFaces(transparent_faces, atlas, out_transparent);
    internal::EmitFacesDirectly(opaque_fluid, atlas, out_opaque);
    internal::EmitFacesDirectly(transparent_fluid, atlas, out_transparent);
  } else {
    internal::EmitFacesDirectly(opaque_faces, atlas, out_opaque);
    internal::EmitFacesDirectly(transparent_faces, atlas, out_transparent);
  }
  // Clear to free memory before next chunk.
  opaque_faces.clear();
  transparent_faces.clear();
}

}  // namespace

ParseResult<TessellationResult> TessellateRegion(const ir::Region& region,
                                                 memory::Arena& arena,
                                                 const ResourcePack& pack,
                                                 TessellationOptions opts) {
  RegionVolume vol(region, arena);
  auto ready = vol.EnsureReady();
  if (!ready) return std::unexpected(ready.error());

  std::vector<ResolvedModel> palette_models(region.palette.size());
  absl::flat_hash_set<std::string> all_texture_paths;
  for (std::size_t i = 0; i < region.palette.size(); ++i) {
    const auto& state = region.palette[i];
    const bool is_air = region.IsAir(static_cast<std::uint16_t>(i));
    if (is_air && !opts.include_air) continue;
    const BlockProps props(state);
    auto m = pack.Resolve(state.name, props);
    if (!m) {
      // Air without a model in the pack contributes no geometry.
      if (is_air) {
        palette_models[i] = ResolvedModel{};
        continue;
      }
      return std::unexpected(m.error());
    }
    palette_models[i] = *m;
    for (const ModelFace& face : m->faces) {
      if (!face.texture_path.empty()) {
        all_texture_paths.insert(face.texture_path);
      }
    }
  }

  // Fluid overlay context (vanilla FluidRenderer): the fixed
  // water_overlay path, probed only when the palette holds water by
  // name; the probe doubles as the availability check and feeds the
  // shared texture set so the atlas reserves a slot. Pack texture
  // successes are cached; misses re-stat once per call.
  std::vector<std::uint8_t> palette_overlay(region.palette.size(), 0);
  bool any_water = false;
  for (std::size_t i = 0; i < region.palette.size(); ++i) {
    if (BlockGetsFluidOverlay(region.palette[i].name)) {
      palette_overlay[i] = 1;
    }
    if (FluidKindForName(region.palette[i].name) == 1) any_water = true;
  }
  std::string_view overlay_texture;
  if (any_water &&
      pack.LoadTexturePixels("minecraft:block/water_overlay").has_value()) {
    overlay_texture = "minecraft:block/water_overlay";
    all_texture_paths.emplace("minecraft:block/water_overlay");
  }
  const FluidOverlayInfo overlay{overlay_texture, palette_overlay};
  TextureAtlasBuilder atlas_builder(opts.atlas_max_extent, opts.atlas_padding);
  absl::flat_hash_map<std::string, std::uint32_t> texture_to_slot;
  texture_to_slot.reserve(all_texture_paths.size());
  for (const std::string& path : all_texture_paths) {
    auto tex = pack.LoadTexturePixels(path);
    if (!tex) return std::unexpected(tex.error());
    auto slot = atlas_builder.Insert(path, tex->rgba, tex->width, tex->height);
    if (!slot) return std::unexpected(slot.error());
    texture_to_slot.emplace(path, *slot);
  }
  TextureAtlas atlas = std::move(atlas_builder).Finalize();

  const std::vector<std::uint8_t> palette_groups =
      BuildPaletteGroups(region, palette_models);
  internal::BlockGroupTable group_table;
  group_table.BuildFrom(region, palette_groups);

  // Fluid slope inputs. Skip the per-cell table entirely when the
  // palette holds no morphable fluid (stone-only regions pay nothing).
  const std::vector<FluidPaletteInfo> palette_fluid =
      BuildPaletteFluid(region, palette_models);
  bool any_fluid = false;
  for (const FluidPaletteInfo& f : palette_fluid) {
    if (f.morphable) {
      any_fluid = true;
      break;
    }
  }
  const std::vector<std::uint8_t> fluid_table =
      any_fluid ? BuildFluidTable(region, palette_fluid)
                : std::vector<std::uint8_t>{};

  TessellationResult result;
  result.atlas = atlas;

  const ir::BoundingBox& b = region.bounds;
  const std::vector<filters::LocalPos> chunk_origins =
      internal::ComputeChunkOrigins(b, opts.chunk_edge);
  const bool use_parallel = opts.chunk_edge > 0 && chunk_origins.size() > 1;

  if (!use_parallel) {
    // Scalar path: collect all faces, then emit.
    std::vector<internal::VisibleFaceInfo> all_opaque;
    std::vector<internal::VisibleFaceInfo> all_transparent;
    for (std::int32_t y = 0; y < b.size[1]; ++y) {
      for (std::int32_t z = 0; z < b.size[2]; ++z) {
        ProcessRow({0, y, z},
                   y,
                   z,
                   b.size[0],
                   palette_models,
                   region,
                   palette_groups,
                   palette_fluid,
                   overlay,
                   fluid_table,
                   group_table,
                   texture_to_slot,
                   opts,
                   all_opaque,
                   all_transparent);
      }
    }
    EmitCollectedFaces(all_opaque,
                       all_transparent,
                       result.atlas,
                       opts,
                       result.opaque_mesh,
                       result.transparent_mesh);
  } else {
    // Parallel path: per-chunk collect, then emit.
    tbb::enumerable_thread_specific<Mesh> tls_opaque;
    tbb::enumerable_thread_specific<Mesh> tls_transparent;

    tbb::parallel_for(
        tbb::blocked_range<std::size_t>(0, chunk_origins.size()),
        [&](const tbb::blocked_range<std::size_t>& r) {
          Mesh& local_opaque = tls_opaque.local();
          Mesh& local_transparent = tls_transparent.local();
          for (std::size_t ci = r.begin(); ci != r.end(); ++ci) {
            const filters::LocalPos origin = chunk_origins[ci];
            const std::int32_t x_end =
                std::min(origin.x + static_cast<std::int32_t>(opts.chunk_edge),
                         b.size[0]);
            const std::int32_t y_end =
                std::min(origin.y + static_cast<std::int32_t>(opts.chunk_edge),
                         b.size[1]);
            const std::int32_t z_end =
                std::min(origin.z + static_cast<std::int32_t>(opts.chunk_edge),
                         b.size[2]);

            std::vector<internal::VisibleFaceInfo> chunk_opaque;
            std::vector<internal::VisibleFaceInfo> chunk_transparent;
            for (std::int32_t y = origin.y; y < y_end; ++y) {
              for (std::int32_t z = origin.z; z < z_end; ++z) {
                ProcessRow(origin,
                           y,
                           z,
                           x_end - origin.x,
                           palette_models,
                           region,
                           palette_groups,
                           palette_fluid,
                           overlay,
                           fluid_table,
                           group_table,
                           texture_to_slot,
                           opts,
                           chunk_opaque,
                           chunk_transparent);
              }
            }
            EmitCollectedFaces(chunk_opaque,
                               chunk_transparent,
                               result.atlas,
                               opts,
                               local_opaque,
                               local_transparent);
          }
        });

    for (const Mesh& m : tls_opaque) {
      AppendMesh(result.opaque_mesh, m);
    }
    for (const Mesh& m : tls_transparent) {
      AppendMesh(result.transparent_mesh, m);
    }
  }

  return result;
}

ParseResult<TessellationResult> TessellateSchematic(
    const ir::Schematic& schematic,
    const ResourcePack& pack,
    TessellationOptions opts) {
  // Resolve every region's palette and collect the union of texture
  // paths.
  std::vector<std::vector<ResolvedModel>> per_region_models;
  per_region_models.reserve(schematic.regions.size());

  absl::flat_hash_set<std::string> all_texture_paths;
  std::vector<std::vector<std::uint8_t>> per_region_overlay;
  per_region_overlay.reserve(schematic.regions.size());
  bool any_water = false;
  for (const ir::Region& region : schematic.regions) {
    // Materialize before anything below reads region.block_indices:
    // freshly decoded litematic/schem regions are lazy (block_indices
    // stays empty until EnsureMaterialized runs), and the per-region
    // group/fluid tables size themselves from it -- building them
    // first would yield empty tables whose LinearIndex reads go out
    // of range. Hoisting the call here also fails fast on corrupt
    // packed data, before pack.Resolve and the atlas build.
    if (auto mat = ir::EnsureMaterialized(region, *schematic.arena);
        !mat) {
      return std::unexpected(mat.error());
    }
    // Build the air LUT; the palette loop below queries region.IsAir().
    region.EnsureAirLut();

    std::vector<ResolvedModel> models(region.palette.size());
    for (std::size_t i = 0; i < region.palette.size(); ++i) {
      const bool is_air = region.IsAir(static_cast<std::uint16_t>(i));
      if (is_air && !opts.include_air) continue;
      const BlockProps props(region.palette[i]);
      auto m = pack.Resolve(region.palette[i].name, props);
      if (!m) {
        if (is_air) {
          models[i] = ResolvedModel{};
          continue;
        }
        return std::unexpected(m.error());
      }
      models[i] = *m;
      for (const ModelFace& face : m->faces) {
        if (!face.texture_path.empty())
          all_texture_paths.insert(face.texture_path);
      }
    }
    std::vector<std::uint8_t> overlay_eligible(region.palette.size(), 0);
    for (std::size_t i = 0; i < region.palette.size(); ++i) {
      if (BlockGetsFluidOverlay(region.palette[i].name)) {
        overlay_eligible[i] = 1;
      }
      if (FluidKindForName(region.palette[i].name) == 1) any_water = true;
    }
    per_region_overlay.push_back(std::move(overlay_eligible));
    per_region_models.push_back(std::move(models));
  }

  std::string_view overlay_texture;
  if (any_water &&
      pack.LoadTexturePixels("minecraft:block/water_overlay").has_value()) {
    overlay_texture = "minecraft:block/water_overlay";
    all_texture_paths.emplace("minecraft:block/water_overlay");
  }
  // One shared atlas across all regions.
  TextureAtlasBuilder atlas_builder(opts.atlas_max_extent, opts.atlas_padding);
  absl::flat_hash_map<std::string, std::uint32_t> texture_to_slot;
  texture_to_slot.reserve(all_texture_paths.size());
  for (const std::string& path : all_texture_paths) {
    auto tex = pack.LoadTexturePixels(path);
    if (!tex) return std::unexpected(tex.error());
    auto slot = atlas_builder.Insert(path, tex->rgba, tex->width, tex->height);
    if (!slot) return std::unexpected(slot.error());
    texture_to_slot.emplace(path, *slot);
  }
  TextureAtlas atlas = std::move(atlas_builder).Finalize();

  // Per-region walk. Each region uses its own BlockGroupTable for AO
  // and cull; cross-region neighbor checks are not performed, so faces
  // at region boundaries stay visible.
  const float ao_strength = opts.ambient_occlusion ? opts.ao_strength : 0.0f;

  std::vector<internal::VisibleFaceInfo> all_opaque;
  std::vector<internal::VisibleFaceInfo> all_transparent;

  for (std::size_t ri = 0; ri < schematic.regions.size(); ++ri) {
    const ir::Region& region = schematic.regions[ri];
    const std::vector<ResolvedModel>& models = per_region_models[ri];

    // Build the group table for AO and cull (within this region only).
    const std::vector<std::uint8_t> palette_groups =
        BuildPaletteGroups(region, models);
    internal::BlockGroupTable group_table;
    group_table.BuildFrom(region, palette_groups);

    const std::vector<FluidPaletteInfo> palette_fluid =
        BuildPaletteFluid(region, models);
    bool any_fluid = false;
    for (const FluidPaletteInfo& f : palette_fluid) {
      if (f.morphable) {
        any_fluid = true;
        break;
      }
    }
    const std::vector<std::uint8_t> fluid_table =
        any_fluid ? BuildFluidTable(region, palette_fluid)
                  : std::vector<std::uint8_t>{};

    // block_indices was materialized in the first loop above; the
    // group/fluid tables built here are sized and filled from it.

    const ir::BoundingBox& b = region.bounds;
    const std::int32_t ox = b.origin[0];
    const std::int32_t oy = b.origin[1];
    const std::int32_t oz = b.origin[2];

    for (std::int32_t y = 0; y < b.size[1]; ++y) {
      for (std::int32_t z = 0; z < b.size[2]; ++z) {
        for (std::int32_t x = 0; x < b.size[0]; ++x) {
          const filters::LocalPos local_p{x, y, z};
          const auto pal_idx =
              region.block_indices[filters::LinearIndex(local_p, b)];
          if (!opts.include_air && region.IsAir(pal_idx)) continue;

          const ResolvedModel& model = models[pal_idx];
          // Fluid slope + flow-texture morph, mirroring ProcessBlock
          // (vanilla FluidRenderer#tesselate).
          const FluidPaletteInfo& fluid = palette_fluid[pal_idx];
          const FluidMorph fluid_m = BuildFluidMorph(model,
                                                     fluid,
                                                     fluid_table,
                                                     group_table,
                                                     b,
                                                     local_p,
                                                     overlay_texture);
          ModelFace morphed;
          for (const ModelFace& face : model.faces) {
            const ModelFace* fp = &face;
            // Per-side overlay flag, hoisted for the back-face
            // decision (mirrors ProcessBlock).
            bool overlay_side = false;
            if (fluid.morphable && face.IsFluidFace()) {
              morphed = face;
              if (!fluid_m.overlay_texture.empty()) {
                const std::uint8_t dir = face.CullFace();
                if (dir !=
                        static_cast<std::uint8_t>(internal::FaceDir::kPosY) &&
                    dir !=
                        static_cast<std::uint8_t>(internal::FaceDir::kNegY)) {
                  const filters::LocalPos nb = NeighborInCullDir(local_p, dir);
                  if (nb.x >= 0 && nb.x < b.size[0] && nb.y >= 0 &&
                      nb.y < b.size[1] && nb.z >= 0 && nb.z < b.size[2]) {
                    overlay_side = per_region_overlay
                        [ri][region.block_indices[filters::LinearIndex(nb, b)]];
                  }
                }
              }
              MorphFluidFace(morphed, fluid_m, overlay_side);
              fp = &morphed;
            }
            // Scalar cull (within this region only).
            if (opts.cull_interior_faces && fp->CullFace() != 0) {
              const filters::LocalPos nb =
                  NeighborInCullDir(local_p, fp->CullFace());
              std::uint8_t nb_group = 0;
              if (nb.x >= 0 && nb.x < b.size[0] && nb.y >= 0 &&
                  nb.y < b.size[1] && nb.z >= 0 && nb.z < b.size[2]) {
                nb_group = group_table.bytes[filters::LinearIndex(nb, b)];
              }
              if (internal::NeighborCulls(nb_group, palette_groups[pal_idx])) {
                continue;
              }
              // Out of region bounds: don't cull (might be another region
              // or air).
            }

            if (fp->texture_path.empty()) continue;
            auto slot_it = texture_to_slot.find(fp->texture_path);
            if (slot_it == texture_to_slot.end()) continue;

            // World-space position (local + bounds.origin); AO uses the
            // LOCAL position (for group_table lookup).
            const filters::LocalPos world_p{x + ox, y + oy, z + oz};

            internal::VisibleFaceInfo info = BuildFaceRecord(*fp,
                                                             local_p,
                                                             world_p,
                                                             slot_it->second,
                                                             group_table,
                                                             b,
                                                             ao_strength);
            if (info.is_transparent) {
              all_transparent.push_back(info);
            } else {
              all_opaque.push_back(info);
            }
            // Back faces, mirroring ProcessBlock: sides double unless
            // overlay, up face per shouldRenderBackwardUpFace, down
            // never.
            if (fluid.morphable && face.IsFluidFace()) {
              const auto dir = static_cast<internal::FaceDir>(fp->CullFace());
              bool back = false;
              if (dir == internal::FaceDir::kPosY) {
                back = ShouldRenderBackwardUpFace(
                    fluid_table, group_table, b, local_p, fluid.kind);
              } else if (dir != internal::FaceDir::kNegY) {
                back = !overlay_side;
              }
              if (back) {
                CollectFluidBackFace(info, all_opaque, all_transparent);
              }
            }
          }
        }
      }
    }
  }

  TessellationResult result;
  result.atlas = atlas;
  EmitCollectedFaces(all_opaque,
                     all_transparent,
                     result.atlas,
                     opts,
                     result.opaque_mesh,
                     result.transparent_mesh);

  return result;
}

}  // namespace fschema::geometry
