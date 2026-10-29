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
#include <cstring>
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
#include "oneapi/tbb/blocked_range.h"
#include "oneapi/tbb/enumerable_thread_specific.h"
#include "oneapi/tbb/parallel_for.h"

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

// Sentinel atlas slot: "no texture / texture not in the atlas".
// Faces resolve to this once, at palette-resolution time, instead of
// hashing their texture path per face in the walk.
constexpr std::uint32_t kNoSlot = 0xFFFFFFFFu;

// Per-palette-entry atlas slot tables, built once per tessellation
// after the atlas is finalized: one slot per model face (kNoSlot for
// empty/missing textures), the flow-texture slot per morphable fluid
// entry, and the shared water-overlay slot.
struct PaletteSlotTable {
  std::vector<std::vector<std::uint32_t>> face_slots;
  std::vector<std::uint32_t> flow_slots;
  std::uint32_t overlay_slot = kNoSlot;
};

// Read-only per-region walk state shared by every worker. (FluidPaletteInfo
// is defined with the fluid helpers below.)
struct FluidPaletteInfo;

struct WalkCtx {
  const ir::Region* region;
  const std::vector<ResolvedModel>* models;
  const std::uint8_t* palette_groups;  // per palette entry
  const FluidPaletteInfo* palette_fluid;  // per palette entry
  const PaletteSlotTable* slots;
  const std::uint8_t* overlay_eligible;  // per palette entry
  const std::vector<std::uint8_t>* fluid_table;  // empty when no fluid
  const internal::BlockGroupTable* group_table;
  float ao_strength;   // 0 when ambient occlusion is off
  bool cull;           // opts.cull_interior_faces
  bool include_air;
  std::array<std::int32_t, 3> world_offset{};  // table pos -> world pos
};

// Face destination: streaming emits straight into the target meshes
// (no intermediate face buffer); collecting buffers VisibleFaceInfo
// for the greedy merge.
template <bool kStream>
struct FaceSink;

template <>
struct FaceSink<false> {
  std::vector<internal::VisibleFaceInfo>* opaque;
  std::vector<internal::VisibleFaceInfo>* transparent;
  // Copy, not move: fluid back faces re-emit the front record right
  // after Put, so the record must stay intact (same front-then-back
  // order as the old direct-collect path).
  void Put(const internal::VisibleFaceInfo& f) {
    (f.is_transparent ? transparent : opaque)->push_back(f);
  }
};

template <>
struct FaceSink<true> {
  const TextureAtlas* atlas;
  Mesh* opaque;
  Mesh* transparent;
  void Put(const internal::VisibleFaceInfo& f) const {
    internal::EmitFaceDirectly(
        f, *atlas, f.is_transparent ? *transparent : *opaque);
  }
};

// Thread-reusable neighbor-group caches for one X row: groups[d][i]
// is the cull/AO group of the block at row offset i in FaceDir d
// (1..6); 0 for air/out-of-bounds. The ±X fill is the existing SIMD
// batch; ±Y/±Z rows are contiguous in the YZX layout and fall out of
// one memcpy each. Sizes persist across rows/chunks, so a worker pays
// the allocation once per thread.
struct RowNeighborBuffers {
  std::array<std::vector<std::uint8_t>, 7> groups;

  void Ensure(std::size_t row) {
    for (std::size_t d = 1; d < groups.size(); ++d) {
      std::vector<std::uint8_t>& g = groups[d];
      if (g.size() < row) {
        // Grow geometrically so scalar full-width rows stay cheap too.
        g.resize(std::max(row, g.size() + g.size() / 2 + 1));
      }
    }
  }
};

// Copies the neighbor group row at `base + offset` (contiguous in the
// YZX layout) into `out`, or zeroes it when the neighbor row is out of
// bounds (edges act like air).
void FillNeighborRow(const internal::BlockGroupTable& table,
                      std::vector<std::uint8_t>& out,
                      std::size_t base,
                      std::int32_t row,
                      std::int64_t offset,
                      bool in_bounds) {
  if (row <= 0) return;
  if (!in_bounds) {
    std::fill_n(out.data(), static_cast<std::size_t>(row), 0);
    return;
  }
  std::memcpy(out.data(), table.bytes.data() + base + offset,
              static_cast<std::size_t>(row));
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

// Whether a cull-face byte is a horizontal direction; the fluid
// overlay and back-face decisions below apply to sides only, never
// to the up or down face.
[[nodiscard]] constexpr bool IsHorizontalFace(
    std::uint8_t cull_face) noexcept {
  const auto dir = static_cast<internal::FaceDir>(cull_face);
  return dir != internal::FaceDir::kPosY && dir != internal::FaceDir::kNegY;
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
//
// Opaque entries occlude (and AO-shade) only when their resolved model
// is a FULL CUBE: vanilla hides a face only behind a shape that
// occludes that direction (canOcclude + full-shape occluder), and the
// one-byte group table cannot express partial per-direction coverage.
// Sub-block shapes -- chests with their particle-only models, redstone
// dust, repeaters, slabs, carpets, torches, ... -- keep every
// neighbor face visible. That over-draws the hidden sliver behind
// them but never eats a face that should be visible; the previous
// classify-everything-opaque behavior wrongly culled glass faces
// resting against chests and redstone dust.
[[nodiscard]] bool ModelOccludesAllDirections(const ResolvedModel& model) {
  bool covered[6] = {false, false, false, false, false, false};
  for (const ModelFace& f : model.faces) {
    const std::uint8_t cf = f.CullFace();
    if (cf < 1 || cf > 6) continue;
    const int axis = cf <= 2 ? 0 : (cf <= 4 ? 1 : 2);
    // Boundary plane: +axis faces sit at 1, -axis faces at 0.
    const float want = (cf == 1 || cf == 3 || cf == 5) ? 1.0f : 0.0f;
    bool at_plane = true;
    for (const auto& c : f.corners) {
      if (std::abs(c[axis] - want) > 1e-4f) {
        at_plane = false;
        break;
      }
    }
    if (!at_plane) continue;
    // Full coverage: both in-plane coordinates span [0,1].
    const int a1 = (axis + 1) % 3;
    const int a2 = (axis + 2) % 3;
    float lo1 = f.corners[0][a1], hi1 = f.corners[0][a1];
    float lo2 = f.corners[0][a2], hi2 = f.corners[0][a2];
    for (const auto& c : f.corners) {
      lo1 = std::min(lo1, c[a1]);
      hi1 = std::max(hi1, c[a1]);
      lo2 = std::min(lo2, c[a2]);
      hi2 = std::max(hi2, c[a2]);
    }
    if (lo1 <= 1e-4f && hi1 >= 1.0f - 1e-4f && lo2 <= 1e-4f &&
        hi2 >= 1.0f - 1e-4f) {
      covered[cf - 1] = true;
    }
  }
  for (bool c : covered) {
    if (!c) return false;
  }
  return true;
}

[[nodiscard]] std::vector<std::uint8_t> BuildPaletteGroups(
    const ir::Region& region,
    const std::vector<ResolvedModel>& palette_models,
    const std::vector<bool>& skipped) {
  std::vector<std::uint8_t> groups(region.palette.size(), 1);
  absl::flat_hash_map<std::string_view, std::uint8_t> transparent_ids;
  std::uint8_t next_id = 2;
  for (std::size_t i = 0; i < region.palette.size(); ++i) {
    // Air and kSkipAndWarn-skipped entries occlude nothing: their
    // neighbors keep every face, so an unresolvable block leaves a
    // hole instead of a black box.
    if (region.IsAir(static_cast<std::uint16_t>(i)) ||
        (i < skipped.size() && skipped[i])) {
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
    } else if (i >= palette_models.size() ||
               !ModelOccludesAllDirections(palette_models[i])) {
      // Non-full-cube opaque entry: occludes nothing (renders its own
      // faces, keeps every neighbor face, contributes no AO).
      groups[i] = 0;
    }
  }
  return groups;
}

// Records one resource skipped under kSkipAndWarn. Called only from
// the single-threaded palette/texture resolution phases, so the sink
// needs no locking.
void RecordWarning(const TessellationOptions& opts,
                   std::string_view resource,
                   const ParseError& error) {
  if (opts.warnings_sink == nullptr) return;
  opts.warnings_sink->push_back(
      TessellationWarning{std::string(resource), error});
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
// flow/overlay atlas slots (kNoSlot when the pack ships neither) and
// the still-vs-flowing decision for the top face. `flowing` is only
// ever true when `flow_slot != kNoSlot` (rotation targets that
// texture). Slot-driven: the hot loop never copies a texture path.
struct FluidMorph {
  FluidCornerHeights corners;
  std::uint32_t overlay_slot = kNoSlot;  // water only; kNoSlot = none
  std::uint32_t flow_slot = kNoSlot;     // kNoSlot = no flow texture
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

// Builds the per-palette-entry atlas slot tables once the atlas is
// finalized: one slot per model face, the flow-texture slot per
// morphable fluid entry and the shared water-overlay slot. Faces then
// emit by table lookup instead of hashing their texture path per
// block.
[[nodiscard]] PaletteSlotTable BuildPaletteSlots(
    const std::vector<ResolvedModel>& palette_models,
    const std::vector<FluidPaletteInfo>& palette_fluid,
    const absl::flat_hash_map<std::string, std::uint32_t>& texture_to_slot,
    bool overlay_available,
    std::string_view overlay_texture) {
  PaletteSlotTable t;
  t.face_slots.resize(palette_models.size());
  t.flow_slots.assign(palette_models.size(), kNoSlot);
  if (overlay_available) {
    auto it = texture_to_slot.find(std::string(overlay_texture));
    if (it != texture_to_slot.end()) t.overlay_slot = it->second;
  }
  for (std::size_t i = 0; i < palette_models.size(); ++i) {
    auto& slots = t.face_slots[i];
    slots.reserve(palette_models[i].faces.size());
    for (const ModelFace& face : palette_models[i].faces) {
      if (face.texture_path.empty()) {
        slots.push_back(kNoSlot);
        continue;
      }
      auto it = texture_to_slot.find(face.texture_path);
      slots.push_back(it == texture_to_slot.end() ? kNoSlot : it->second);
    }
    if (i < palette_fluid.size() && palette_fluid[i].morphable) {
      const std::string flow(FluidFlowTextureOf(palette_models[i]));
      if (!flow.empty()) {
        auto it = texture_to_slot.find(flow);
        if (it != texture_to_slot.end()) t.flow_slots[i] = it->second;
      }
    }
  }
  return t;
}

[[nodiscard]] FluidMorph BuildFluidMorph(
    const FluidPaletteInfo& fluid,
    const std::vector<std::uint8_t>& fluid_table,
    const internal::BlockGroupTable& group_table,
    const ir::BoundingBox& bounds,
    filters::LocalPos p,
    std::uint32_t overlay_slot,
    std::uint32_t flow_slot) noexcept {
  FluidMorph m;
  if (!fluid.morphable) return m;
  m.corners = FluidCornersAt(fluid_table, group_table, bounds, p, fluid.kind);
  // Only water's fluid model carries an overlay material in vanilla.
  m.overlay_slot = fluid.kind == 1 ? overlay_slot : kNoSlot;
  m.flow_slot = flow_slot;
  if (m.flow_slot == kNoSlot) return m;
  const auto [fx, fz] =
      FluidFlowAt(fluid_table, group_table, bounds, p, fluid.kind);
  m.flow = {fx, fz};
  m.flowing = fx != 0.0f || fz != 0.0f;
  return m;
}

// Morphs a synthesized fluid cuboid face in place (vanilla
// FluidRenderer#tesselate): the up face takes the four corner
// heights, side faces slope their top edge along them, the down face
// stays at y = 0. With a flow texture (fm.flow_slot), sides sample
// vanilla's flow window (u 0..1/2, v (1-h)/2 at the surface and 1/2 at
// the block bottom) and a flowing top swaps to the flow texture with
// the sampling window rotated toward the flow direction. The face's
// OWN texture never changes here -- the caller picks the atlas slot
// (still / flow / overlay) so this stays allocation-free.
// Corner-index maps follow BuildCubeFace's winding per direction.
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
      }
      break;
    case internal::FaceDir::kNegZ:  // north: top edge = NW, NE
      face.corners[3][1] = h.nw;
      face.corners[2][1] = h.ne;
      if (overlay_side || fm.flow_slot != kNoSlot) {
        face.uvs = {{
            {{0.0f, 0.5f}},                  // bottom, x0 (NW)
            {{0.5f, 0.5f}},                  // bottom, x1 (NE)
            {{0.5f, (1.0f - h.ne) * 0.5f}},  // top, NE
            {{0.0f, (1.0f - h.nw) * 0.5f}},  // top, NW
        }};
      }
      break;
    case internal::FaceDir::kPosZ:  // south: top edge = SW, SE
      face.corners[1][1] = h.sw;
      face.corners[2][1] = h.se;
      if (overlay_side || fm.flow_slot != kNoSlot) {
        face.uvs = {{
            {{0.5f, 0.5f}},                  // bottom, x0 (SW)
            {{0.5f, (1.0f - h.sw) * 0.5f}},  // top, SW
            {{0.0f, (1.0f - h.se) * 0.5f}},  // top, SE
            {{0.0f, 0.5f}},                  // bottom, x1 (SE)
        }};
      }
      break;
    case internal::FaceDir::kNegX:  // west: top edge = NW, SW
      face.corners[1][1] = h.nw;
      face.corners[2][1] = h.sw;
      if (overlay_side || fm.flow_slot != kNoSlot) {
        face.uvs = {{
            {{0.5f, 0.5f}},                  // bottom, z0 (NW)
            {{0.5f, (1.0f - h.nw) * 0.5f}},  // top, NW
            {{0.0f, (1.0f - h.sw) * 0.5f}},  // top, SW
            {{0.0f, 0.5f}},                  // bottom, z1 (SW)
        }};
      }
      break;
    case internal::FaceDir::kPosX:  // east: top edge = NE, SE
      face.corners[2][1] = h.se;
      face.corners[3][1] = h.ne;
      if (overlay_side || fm.flow_slot != kNoSlot) {
        face.uvs = {{
            {{0.0f, 0.5f}},                  // bottom, z0 (NE)
            {{0.5f, 0.5f}},                  // bottom, z1 (SE)
            {{0.5f, (1.0f - h.se) * 0.5f}},  // top, SE
            {{0.0f, (1.0f - h.ne) * 0.5f}},  // top, NE
        }};
      }
      break;
    case internal::FaceDir::kNegY:
    default:
      break;  // bottom stays at y = 0, full still-texture rect
  }
}

// Builds one visible-face record (world-space corners, UVs, per-vertex
// AO). `table_pos` indexes the neighbor tables (region-relative);
// `world_pos` is the corner base (world-space for the schematic walk,
// equal to table_pos for single regions). All four vertex AO bytes are
// computed in one shared-setup pass. Returned so callers can push
// derived copies (fluid back faces re-emit the same record reversed).
[[nodiscard]] internal::VisibleFaceInfo BuildFaceRecord(
    const ModelFace& face,
    filters::LocalPos table_pos,
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
  }
  internal::ComputeFaceAO(group_table,
                           bounds,
                           table_pos,
                           face.CullFace(),
                           face.corners,
                           ao_strength,
                           info.ao);
  return info;
}

// Vanilla FluidRenderer#addFace: a back face re-emits the same
// four vertices in reverse order (3, 2, 1, 0) with identical uvs,
// color, and light, flipping the winding so the quad is visible from
// the other side. The reversed copy keeps the front's AO and tint per
// vertex; its cull face flips so the exported normal matches the
// visible orientation (FastSchema normals are per-face, unlike
// vanilla's hardcoded up for every fluid vertex).
template <bool kStream>
void EmitFluidBackFace(const internal::VisibleFaceInfo& front,
                       FaceSink<kStream>& sink) {
  internal::VisibleFaceInfo back = front;
  for (int i = 0; i < 4; ++i) {
    back.world_corners[i] = front.world_corners[3 - i];
    back.uvs[i] = front.uvs[3 - i];
    back.ao[i] = front.ao[3 - i];
  }
  back.cull_face = OppositeCullFace(front.cull_face);
  sink.Put(std::move(back));
}

// Walks one block: culls faces against the row caches, resolves atlas
// slots through the per-palette table (no per-face hashing), morphs
// fluid faces, computes AO and hands the record to the sink. `row_i`
// is the block's offset within the current X row segment, indexing
// the neighbor rows in `rows` (only read when ctx.cull).
template <bool kStream>
void ProcessBlock(const WalkCtx& ctx,
                  filters::LocalPos p,
                  std::size_t row_i,
                  RowNeighborBuffers& rows,
                  FaceSink<kStream>& sink) {
  const ir::Region& region = *ctx.region;
  const std::uint16_t pal_idx = static_cast<std::uint16_t>(
      region.block_indices[filters::LinearIndex(p, region.bounds)]);
  if (!ctx.include_air && region.IsAir(pal_idx)) return;

  const ResolvedModel& model = (*ctx.models)[pal_idx];
  const std::uint8_t cur_group = ctx.palette_groups[pal_idx];
  const std::uint32_t* face_slots = ctx.slots->face_slots[pal_idx].data();

  // Synthesized fluid faces are morphed per block: their corner
  // heights and flow direction depend on fluid neighbors, which the
  // per-state model cache cannot express (vanilla
  // FluidRenderer#tesselate).
  const FluidPaletteInfo& fluid = ctx.palette_fluid[pal_idx];
  const FluidMorph fluid_m =
      BuildFluidMorph(fluid, *ctx.fluid_table, *ctx.group_table,
                      region.bounds, p, ctx.slots->overlay_slot,
                      ctx.slots->flow_slots[pal_idx]);
  ModelFace morphed;
  std::size_t fi = 0;
  for (const ModelFace& face : model.faces) {
    const std::uint32_t own_slot = face_slots[fi];
    ++fi;
    const ModelFace* fp = &face;
    // Overlay flag is consulted per horizontal side (vanilla swaps the
    // sprite per face direction); up/down faces never overlay. Hoisted
    // out of the morph branch because the back-face decision below
    // needs it too.
    bool overlay_side = false;
    if (fluid.morphable && face.IsFluidFace()) {
      morphed = face;
      if (fluid_m.overlay_slot != kNoSlot) {
        const std::uint8_t dir = face.CullFace();
        if (IsHorizontalFace(dir)) {
          const filters::LocalPos nb = NeighborInCullDir(p, dir);
          if (InBounds(region.bounds, nb)) {
            overlay_side = ctx.overlay_eligible[region.block_indices[
                filters::LinearIndex(nb, region.bounds)]];
          }
        }
      }
      MorphFluidFace(morphed, fluid_m, overlay_side);
      fp = &morphed;
    }
    if (ctx.cull && fp->CullFace() != 0 &&
        internal::NeighborCulls(rows.groups[fp->CullFace()][row_i],
                                cur_group)) {
      continue;
    }
    // Slot pick: overlay sides swap to the water-overlay sprite, a
    // flowing top swaps to the flow texture; every other face keeps
    // its own per-face slot (kNoSlot covers both the empty-path and
    // the not-in-atlas skip the hash lookup used to pay for).
    std::uint32_t slot = own_slot;
    if (overlay_side) {
      slot = fluid_m.overlay_slot;
    } else if (fluid.morphable && face.IsFluidFace() &&
               fp->CullFace() ==
                   static_cast<std::uint8_t>(internal::FaceDir::kPosY) &&
               fluid_m.flowing) {
      slot = fluid_m.flow_slot;
    }
    if (slot == kNoSlot) continue;

    const filters::LocalPos world_p{p.x + ctx.world_offset[0],
                                    p.y + ctx.world_offset[1],
                                    p.z + ctx.world_offset[2]};
    internal::VisibleFaceInfo info = BuildFaceRecord(*fp, p, world_p, slot,
                                                     *ctx.group_table,
                                                     region.bounds,
                                                     ctx.ao_strength);
    sink.Put(info);
    // Vanilla FluidRenderer#addFace doubles every emitted fluid face
    // with a reversed-winding copy except the down face and overlay
    // sides: sides pass addBackFace = !isOverlay, the up face only
    // while shouldRenderBackwardUpFace holds.
    if (fluid.morphable && face.IsFluidFace()) {
      const auto dir = static_cast<internal::FaceDir>(fp->CullFace());
      bool back = false;
      if (dir == internal::FaceDir::kPosY) {
        back = ShouldRenderBackwardUpFace(*ctx.fluid_table, *ctx.group_table,
                                          region.bounds, p, fluid.kind);
      } else if (dir != internal::FaceDir::kNegY) {
        back = !overlay_side;
      }
      if (back) EmitFluidBackFace(info, sink);
    }
  }
}

// Walks one X row segment. Fills the six neighbor-group rows first
// (two Highway-SIMD batches for +-X, one memcpy each for +-Y/+-Z,
// which are contiguous in the YZX table layout; out-of-bounds rows
// read as air), then visits the blocks. The buffers persist across
// rows and chunks, so a worker pays the allocation once per thread.
template <bool kStream>
void ProcessRow(const WalkCtx& ctx,
                filters::LocalPos x_start,
                std::int32_t y,
                std::int32_t z,
                std::int32_t x_count,
                RowNeighborBuffers& rows,
                FaceSink<kStream>& sink) {
  if (x_count <= 0) return;
  const ir::Region& region = *ctx.region;
  const ir::BoundingBox& b = region.bounds;
  const std::size_t row = static_cast<std::size_t>(x_count);

  if (ctx.cull) {
    rows.Ensure(row);
    const std::size_t base = filters::LinearIndex({x_start.x, y, z}, b);
    const std::uint32_t region_width = static_cast<std::uint32_t>(b.size[0]);
    internal::ComputePosXNeighborGroupBatch(
        *ctx.group_table, base, row, region_width,
        rows.groups[static_cast<std::size_t>(internal::FaceDir::kPosX)]
            .data());
    internal::ComputeNegXNeighborGroupBatch(
        *ctx.group_table, base, row, region_width,
        rows.groups[static_cast<std::size_t>(internal::FaceDir::kNegX)]
            .data());
    const std::int64_t sx = b.size[0];
    const std::int64_t sy = b.size[1];
    const std::int64_t sz = b.size[2];
    FillNeighborRow(*ctx.group_table,
                    rows.groups[static_cast<std::size_t>(
                        internal::FaceDir::kPosY)],
                    base, x_count, sx * sz, y + 1 < sy);
    FillNeighborRow(*ctx.group_table,
                    rows.groups[static_cast<std::size_t>(
                        internal::FaceDir::kNegY)],
                    base, x_count, -sx * sz, y - 1 >= 0);
    FillNeighborRow(*ctx.group_table,
                    rows.groups[static_cast<std::size_t>(
                        internal::FaceDir::kPosZ)],
                    base, x_count, sx, z + 1 < sz);
    FillNeighborRow(*ctx.group_table,
                    rows.groups[static_cast<std::size_t>(
                        internal::FaceDir::kNegZ)],
                    base, x_count, -sx, z - 1 >= 0);
  }

  for (std::int32_t i = 0; i < x_count; ++i) {
    ProcessBlock(ctx,
                 {x_start.x + i, y, z},
                 static_cast<std::size_t>(i),
                 rows,
                 sink);
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

// Folds the thread-local meshes into `out`: the exact final capacity is
// reserved up front (the fold never pays a growth reallocation of its
// own) and each source mesh is freed right after its bytes land, so
// the transient peak is the final mesh plus one worker's leftovers
// instead of everything twice over.
template <typename TlsMeshes>
void MergeTlsMeshes(TlsMeshes& tls, Mesh& out) {
  std::size_t verts = 0;
  std::size_t idxs = 0;
  for (const Mesh& m : tls) {
    verts += m.VertexCount();
    idxs += m.indices.size();
  }
  if (verts == 0 && idxs == 0) return;
  out.Reserve(out.VertexCount() + verts, out.indices.size() + idxs);
  for (Mesh& m : tls) {
    AppendMesh(out, m);
    m = Mesh{};
  }
}

// Builds the shared texture atlas and its path->slot map. When
// greedy merging is on, merged quads address their slot in tile
// units (a merged wall quad spans many texture tiles), so each slot
// carries tiled copies of its texture: try the preferred repeat
// count first and halve on atlas overflow, down to the compact
// one-tile slot. At repeats 1 the merge caps its rectangles at 1x1
// tile, so every quad stays inside its slot either way -- rendering
// stays correct, only the merge benefit shrinks.
[[nodiscard]] ParseResult<std::pair<
    TextureAtlas,
    absl::flat_hash_map<std::string, std::uint32_t>>>
BuildAtlasWithSlots(const ResourcePack& pack,
                    const absl::flat_hash_set<std::string>& paths,
                    const TessellationOptions& opts,
                    bool skip_unresolved,
                    const TessellationOptions& warn_opts) {
  constexpr std::uint32_t kPreferredRepeat = 8;
  const std::uint32_t preferred =
      opts.merge_coplanar_faces ? kPreferredRepeat : 1;
  static_assert(kPreferredRepeat > 0 &&
                    (kPreferredRepeat & (kPreferredRepeat - 1)) == 0,
                "repeat halving must terminate at 1");
  for (std::uint32_t repeats = preferred;; repeats >>= 1) {
    TextureAtlasBuilder builder(opts.atlas_max_extent, opts.atlas_padding);
    absl::flat_hash_map<std::string, std::uint32_t> to_slot;
    to_slot.reserve(paths.size());
    bool overflow = false;
    for (const std::string& path : paths) {
      auto tex = pack.LoadTexturePixels(path);
      if (!tex) {
        if (skip_unresolved) {
          RecordWarning(warn_opts, path, tex.error());
          continue;
        }
        return std::unexpected(tex.error());
      }
      auto slot = builder.Insert(path, tex->rgba, tex->width, tex->height,
                                 repeats, repeats);
      if (!slot) {
        if (slot.error().code == ParseError::Code::kAtlasOverflow &&
            repeats > 1) {
          overflow = true;
          break;
        }
        return std::unexpected(slot.error());
      }
      to_slot.emplace(path, *slot);
    }
    if (!overflow) {
      return std::make_pair(std::move(builder).Finalize(),
                            std::move(to_slot));
    }
    if (repeats <= 1) {
      // repeats 1 is the pre-tiled layout; its overflow is final.
      return std::unexpected(ParseError::At(
          ParseError::Code::kAtlasOverflow,
          "atlas overflow at repeat count 1", 0));
    }
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
    // out in place and emit them directly, per output mesh.
    std::vector<internal::VisibleFaceInfo> opaque_fluid;
    std::vector<internal::VisibleFaceInfo> transparent_fluid;
    internal::SpliceFluidFacesInPlace(opaque_faces, opaque_fluid);
    internal::SpliceFluidFacesInPlace(transparent_faces, transparent_fluid);
    internal::MergeAndEmitFaces(opaque_faces, atlas, out_opaque);
    internal::MergeAndEmitFaces(transparent_faces, atlas, out_transparent);
    internal::EmitFacesDirectly(opaque_fluid, atlas, out_opaque);
    internal::EmitFacesDirectly(transparent_fluid, atlas, out_transparent);
  } else {
    internal::EmitFacesDirectly(opaque_faces, atlas, out_opaque);
    internal::EmitFacesDirectly(transparent_faces, atlas, out_transparent);
  }
  // Drop the records (capacity stays for the next chunk's reuse).
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

  const bool skip_unresolved =
      opts.unresolved_resources == UnresolvedResourcePolicy::kSkipAndWarn;
  std::vector<ResolvedModel> palette_models(region.palette.size());
  std::vector<bool> skipped(region.palette.size(), false);
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
      if (skip_unresolved) {
        RecordWarning(opts, state.name, m.error());
        skipped[i] = true;
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
  auto atlas_and_slots =
      BuildAtlasWithSlots(pack, all_texture_paths, opts, skip_unresolved, opts);
  if (!atlas_and_slots) return std::unexpected(atlas_and_slots.error());
  absl::flat_hash_map<std::string, std::uint32_t>& texture_to_slot =
      atlas_and_slots->second;
  TessellationResult result;
  result.atlas = std::move(atlas_and_slots->first);

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

  // Per-palette atlas slots, resolved once: the walk emits by table
  // lookup instead of hashing each face's texture path per block.
  const PaletteSlotTable slots =
      BuildPaletteSlots(palette_models, palette_fluid, texture_to_slot,
                        !overlay_texture.empty(), overlay_texture);

  const std::vector<std::uint8_t> palette_groups =
      BuildPaletteGroups(region, palette_models, skipped);
  internal::BlockGroupTable group_table;
  group_table.BuildFrom(region, palette_groups);

  const WalkCtx ctx{&region,
                    &palette_models,
                    palette_groups.data(),
                    palette_fluid.data(),
                    &slots,
                    overlay.palette_eligible.data(),
                    &fluid_table,
                    &group_table,
                    opts.ambient_occlusion ? opts.ao_strength : 0.0f,
                    opts.cull_interior_faces,
                    /*include_air=*/false,  // air never meshes here
                    /*world_offset=*/{0, 0, 0}};

  const ir::BoundingBox& b = region.bounds;
  const std::vector<filters::LocalPos> chunk_origins =
      internal::ComputeChunkOrigins(b, opts.chunk_edge);
  const bool use_parallel = opts.chunk_edge > 0 && chunk_origins.size() > 1;

  if (!use_parallel) {
    RowNeighborBuffers rows;
    if (!opts.merge_coplanar_faces) {
      // Scalar streaming: faces go straight into the output meshes, no
      // intermediate record buffer at all.
      FaceSink<true> sink{&result.atlas, &result.opaque_mesh,
                          &result.transparent_mesh};
      for (std::int32_t y = 0; y < b.size[1]; ++y) {
        for (std::int32_t z = 0; z < b.size[2]; ++z) {
          ProcessRow(ctx, {0, y, z}, y, z, b.size[0], rows, sink);
        }
      }
    } else {
      // Scalar merge: merging needs the records, so collect the whole
      // region first (chunking would cap merged rectangles at chunk
      // boundaries).
      std::vector<internal::VisibleFaceInfo> all_opaque;
      std::vector<internal::VisibleFaceInfo> all_transparent;
      FaceSink<false> sink{&all_opaque, &all_transparent};
      for (std::int32_t y = 0; y < b.size[1]; ++y) {
        for (std::int32_t z = 0; z < b.size[2]; ++z) {
          ProcessRow(ctx, {0, y, z}, y, z, b.size[0], rows, sink);
        }
      }
      EmitCollectedFaces(all_opaque,
                         all_transparent,
                         result.atlas,
                         opts,
                         result.opaque_mesh,
                         result.transparent_mesh);
    }
  } else {
    // Parallel path. Non-merging workers stream straight into their
    // thread-local meshes; merging workers collect per chunk (records
    // bounded by one chunk) and merge into their thread-local meshes.
    tbb::enumerable_thread_specific<Mesh> tls_opaque;
    tbb::enumerable_thread_specific<Mesh> tls_transparent;
    tbb::enumerable_thread_specific<RowNeighborBuffers> tls_rows;

    tbb::parallel_for(
        tbb::blocked_range<std::size_t>(0, chunk_origins.size()),
        [&](const tbb::blocked_range<std::size_t>& r) {
          Mesh& local_opaque = tls_opaque.local();
          Mesh& local_transparent = tls_transparent.local();
          RowNeighborBuffers& rows = tls_rows.local();
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

            if (!opts.merge_coplanar_faces) {
              FaceSink<true> sink{&result.atlas, &local_opaque,
                                   &local_transparent};
              for (std::int32_t y = origin.y; y < y_end; ++y) {
                for (std::int32_t z = origin.z; z < z_end; ++z) {
                  ProcessRow(ctx, origin, y, z, x_end - origin.x, rows, sink);
                }
              }
            } else {
              std::vector<internal::VisibleFaceInfo> chunk_opaque;
              std::vector<internal::VisibleFaceInfo> chunk_transparent;
              FaceSink<false> sink{&chunk_opaque, &chunk_transparent};
              for (std::int32_t y = origin.y; y < y_end; ++y) {
                for (std::int32_t z = origin.z; z < z_end; ++z) {
                  ProcessRow(ctx, origin, y, z, x_end - origin.x, rows, sink);
                }
              }
              EmitCollectedFaces(chunk_opaque,
                                 chunk_transparent,
                                 result.atlas,
                                 opts,
                                 local_opaque,
                                 local_transparent);
            }
          }
        });

    MergeTlsMeshes(tls_opaque, result.opaque_mesh);
    MergeTlsMeshes(tls_transparent, result.transparent_mesh);
  }

  return result;
}

// ---------------------------------------------------------------------------
// Entity models: sheep.
//
// Schematics carry mobs in region.entities; the mesher ignores them
// entirely, so a sheep placed inside the build vanishes. Mesh the
// vanilla SheepModel (plus the SheepFurModel wool overlay) like any
// other geometry: boxes in Java's Y-down model space posed per part,
// wrapped by the root transform (RotX(pi), then translate (0.5,1.5,
// 0.5)) that flips to Y-up block space with the feet at y=0 and the
// model facing +Z, then rotated by the entity yaw about the anchor
// (bottom-center of the hitbox) so it faces (-sin(yaw), 0, cos(yaw))
// like vanilla's entity renderer. Box UVs follow the standard entity
// sheet layout (64x32 for sheep). Values mirror the SheepModel dump
// used by Schematic-Mesher (MC 1.21).
//
// Baby/sheared variants live in raw NBT and stay unsupported; the
// adult unsheared model covers the common schematic case.
// ---------------------------------------------------------------------------
struct SheepBoxSpec {
  // Box extents in 1/16 model units (Y-down), relative to the part
  // pivot; inflate expands about the box center like vanilla.
  float x0, y0, z0, x1, y1, z1;
  float inflate;               // 1/16 units
  int u_off, v_off;            // texture offset, texels
  float px, py, pz;            // pivot, 1/16 model units
  float rot_x;                 // pose rotation about X, radians
};

// Emits one entity box (six quads) into `out_mesh`. `anchor` is the
// entity position (feet, bottom-center of the hitbox) in mesh space;
// `yaw_deg` the entity yaw.
void EmitSheepBox(const SheepBoxSpec& box,
                  const TextureAtlas::Slot& slot,
                  double ax, double ay, double az,
                  float yaw_deg,
                  Mesh& out_mesh) {
  const float rad = yaw_deg * 3.14159265f / 180.0f;
  const float sy = std::sin(rad), cy = std::cos(rad);
  const float inf = box.inflate;
  const float bx0 = (box.x0 - inf) / 16.0f, bx1 = (box.x1 + inf) / 16.0f;
  const float by0 = (box.y0 - inf) / 16.0f, by1 = (box.y1 + inf) / 16.0f;
  const float bz0 = (box.z0 - inf) / 16.0f, bz1 = (box.z1 + inf) / 16.0f;

  // Model point -> world. Part rotation about the pivot, root wrapper
  // (Rx(pi) then translate), yaw about the cell-center axis, anchor at
  // the entity feet position.
  const float crx = std::cos(box.rot_x), srx = std::sin(box.rot_x);
  auto to_world = [&](float x, float y, float z) {
    if (box.rot_x != 0.0f) {
      const float ny = y * crx - z * srx;
      const float nz = y * srx + z * crx;
      y = ny;
      z = nz;
    }
    x += box.px / 16.0f;
    y += box.py / 16.0f;
    z += box.pz / 16.0f;
    // Root: Rx(pi) flips Y and Z, then translate to cell center.
    const float lx = x + 0.5f, ly = -y + 1.5f, lz = -z + 0.5f;
    // Yaw about (0.5, ., 0.5), then place the cell's bottom-center at
    // the anchor. Ry(-yaw) makes the +Z-facing model look toward
    // (-sin, 0, cos) of the entity yaw.
    const float dx = lx - 0.5f, dz = lz - 0.5f;
    const float rx = dx * cy - dz * sy;
    const float rz = dx * sy + dz * cy;
    return std::array<float, 3>{static_cast<float>(ax) + rx,
                                static_cast<float>(ay) + ly,
                                static_cast<float>(az) + rz};
  };

  const float w = box.x1 - box.x0, h = box.y1 - box.y0,
              d = box.z1 - box.z0;  // texels
  const float u = static_cast<float>(box.u_off);
  const float v = static_cast<float>(box.v_off);
  constexpr float kSheetW = 64.0f, kSheetH = 32.0f;

  struct FacePlan {
    internal::FaceDir dir;
    int corners[4];  // BuildCubeFace's order, CW from outside
  };
  // Corner orders mirror BuildCubeFace (bit-wise index x=1<<2, y=1<<1,
  // z=1<<0), so culling and winding match the block pipeline exactly.
  static constexpr std::array<FacePlan, 6> kPlans = {{
      {internal::FaceDir::kNegY, {0, 1, 5, 4}},
      {internal::FaceDir::kPosY, {2, 6, 7, 3}},
      {internal::FaceDir::kNegZ, {0, 4, 6, 2}},
      {internal::FaceDir::kPosZ, {1, 3, 7, 5}},
      {internal::FaceDir::kNegX, {0, 2, 3, 1}},
      {internal::FaceDir::kPosX, {4, 5, 7, 6}},
  }};

  for (const FacePlan& plan : kPlans) {
    float l, t, r, b;
    switch (plan.dir) {
      case internal::FaceDir::kNegY:
        l = u + d; t = v; r = l + w; b = v + d; break;
      case internal::FaceDir::kPosY:
        l = u + d + w; t = v; r = l + w; b = v + d; break;
      case internal::FaceDir::kNegZ:
        l = u + d; t = v + d; r = l + w; b = t + h; break;
      case internal::FaceDir::kPosZ:
        l = u + d + w + d; t = v + d; r = l + w; b = t + h; break;
      case internal::FaceDir::kNegX:
        l = u; t = v + d; r = l + d; b = t + h; break;
      default:
        l = u + d + w; t = v + d; r = l + d; b = t + h; break;
    }
    const float nl = l / kSheetW, nt = t / kSheetH, nr = r / kSheetW,
                nb = b / kSheetH;

    // Model-space corners first (pre-transform): UVs project onto the
    // face's image axes exactly like block faces (CubeFaceUvRect /
    // ModelFaceAxes), so upright orientation and per-face mirroring
    // match vanilla's FaceBakery convention by construction; the
    // world positions then carry them through the part/yaw transform.
    std::array<std::array<float, 3>, 4> model{};
    for (int i = 0; i < 4; ++i) {
      const int ci = plan.corners[i];
      model[i] = {(ci & 4) ? bx1 : bx0, (ci & 2) ? by1 : by0,
                  (ci & 1) ? bz1 : bz0};
    }
    const std::array<std::array<float, 2>, 4> uvs = internal::CubeFaceUvRect(
        plan.dir, model, std::array<float, 4>{nl, nt, nr, nb});
    std::array<std::array<float, 3>, 4> pos{};
    for (int i = 0; i < 4; ++i) {
      pos[i] = to_world(model[i][0], model[i][1], model[i][2]);
    }
    // Geometric normal: matches PushQuadIndices' flipped winding.
    const float ax1 = pos[2][0] - pos[0][0], ay1 = pos[2][1] - pos[0][1],
                az1 = pos[2][2] - pos[0][2];
    const float bx1v = pos[1][0] - pos[0][0], by1v = pos[1][1] - pos[0][1],
                bz1v = pos[1][2] - pos[0][2];
    float nx = ay1 * bz1v - az1 * by1v;
    float ny = az1 * bx1v - ax1 * bz1v;
    float nz = ax1 * by1v - ay1 * bx1v;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-9f) {
      nx /= len;
      ny /= len;
      nz /= len;
    } else {
      nx = 0;
      ny = 1;
      nz = 0;
    }

    const std::uint32_t base =
        static_cast<std::uint32_t>(out_mesh.positions.size() / 3);
    // Per-tile atlas mapping, exactly like EmitSingleFace: the face
    // uvs are fractions of the entity SHEET, and the slot carries
    // repeats_x/y tiled copies -- mapping over the full slot would
    // sample across tile boundaries.
    const float tile_u =
        (slot.u1 - slot.u0) / static_cast<float>(slot.repeats_x);
    const float tile_v =
        (slot.v1 - slot.v0) / static_cast<float>(slot.repeats_y);
    for (int i = 0; i < 4; ++i) {
      out_mesh.positions.push_back(pos[i][0]);
      out_mesh.positions.push_back(pos[i][1]);
      out_mesh.positions.push_back(pos[i][2]);
      out_mesh.normals.push_back(nx);
      out_mesh.normals.push_back(ny);
      out_mesh.normals.push_back(nz);
      out_mesh.uvs.push_back(slot.u0 + tile_u * uvs[i][0]);
      out_mesh.uvs.push_back(slot.v0 + tile_v * uvs[i][1]);
      out_mesh.ao.push_back(255);
      out_mesh.tints.push_back(0);
    }
    out_mesh.indices.push_back(base + 0);
    out_mesh.indices.push_back(base + 2);
    out_mesh.indices.push_back(base + 1);
    out_mesh.indices.push_back(base + 0);
    out_mesh.indices.push_back(base + 3);
    out_mesh.indices.push_back(base + 2);
  }
}

// Appends every sheep found in the schematic's regions.
void AppendSheep(const ir::Schematic& schematic,
                 const absl::flat_hash_map<std::string, std::uint32_t>&
                     texture_to_slot,
                 const TextureAtlas& atlas,
                 Mesh& out_mesh) {
  // SheepModel (base skin) + SheepFurModel (wool overlay).
  static const std::vector<SheepBoxSpec> kSkin = {
      // head 6x6x8 at pose (0,6,-8)
      {-3, -4, -6, 3, 2, 2, 0.0f, 0, 0, 0, 6, -8, 0.0f},
      // body 8x16x6 at pose (0,5,2), RotX(90)
      {-4, -10, -7, 4, 6, -1, 0.0f, 28, 8, 0, 5, 2, 1.5707964f},
      // legs 4x12x4, poses (+-3,12,+-7 / +-5)
      {-2, 0, -2, 2, 12, 2, 0.0f, 0, 16, -3, 12, 7, 0.0f},
      {-2, 0, -2, 2, 12, 2, 0.0f, 0, 16, 3, 12, 7, 0.0f},
      {-2, 0, -2, 2, 12, 2, 0.0f, 0, 16, -3, 12, -5, 0.0f},
      {-2, 0, -2, 2, 12, 2, 0.0f, 0, 16, 3, 12, -5, 0.0f},
  };
  static const std::vector<SheepBoxSpec> kWool = {
      // wool head 6x6x6 (not 6x6x8), inflate 0.6
      {-3, -4, -4, 3, 2, 2, 0.6f, 0, 0, 0, 6, -8, 0.0f},
      // wool body, inflate 1.75
      {-4, -10, -7, 4, 6, -1, 1.75f, 28, 8, 0, 5, 2, 1.5707964f},
      // wool legs 4x6x4, inflate 0.5
      {-2, 0, -2, 2, 6, 2, 0.5f, 0, 16, -3, 12, 7, 0.0f},
      {-2, 0, -2, 2, 6, 2, 0.5f, 0, 16, 3, 12, 7, 0.0f},
      {-2, 0, -2, 2, 6, 2, 0.5f, 0, 16, -3, 12, -5, 0.0f},
      {-2, 0, -2, 2, 6, 2, 0.5f, 0, 16, 3, 12, -5, 0.0f},
  };

  for (const ir::Region& region : schematic.regions) {
    for (const ir::Entity& ent : region.entities) {
      if (ent.id != "minecraft:sheep") continue;
      const float yaw = ent.rotation[0];
      for (const SheepBoxSpec& box : kSkin) {
        auto it = texture_to_slot.find("minecraft:entity/sheep/sheep");
        if (it != texture_to_slot.end()) {
          EmitSheepBox(box, atlas.slots[it->second], ent.position[0],
                       ent.position[1], ent.position[2], yaw, out_mesh);
        }
      }
      for (const SheepBoxSpec& box : kWool) {
        auto it = texture_to_slot.find("minecraft:entity/sheep/sheep_wool");
        if (it != texture_to_slot.end()) {
          EmitSheepBox(box, atlas.slots[it->second], ent.position[0],
                       ent.position[1], ent.position[2], yaw, out_mesh);
        }
      }
    }
  }
}


ParseResult<TessellationResult> TessellateSchematic(
    const ir::Schematic& schematic,
    const ResourcePack& pack,
    TessellationOptions opts) {
  // Resolve every region's palette and collect the union of texture
  // paths.
  std::vector<std::vector<ResolvedModel>> per_region_models;
  per_region_models.reserve(schematic.regions.size());
  std::vector<std::vector<bool>> per_region_skipped;
  per_region_skipped.reserve(schematic.regions.size());

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
    std::vector<bool> skipped(region.palette.size(), false);
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
        if (opts.unresolved_resources == UnresolvedResourcePolicy::kSkipAndWarn) {
          RecordWarning(opts, region.palette[i].name, m.error());
          skipped[i] = true;
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
    per_region_skipped.push_back(std::move(skipped));
    per_region_models.push_back(std::move(models));
  }

  std::string_view overlay_texture;
  if (any_water &&
      pack.LoadTexturePixels("minecraft:block/water_overlay").has_value()) {
    overlay_texture = "minecraft:block/water_overlay";
    all_texture_paths.emplace("minecraft:block/water_overlay");
  }
  // Entities (sheep): reserve atlas slots for their entity textures
  // when any region carries one, probing availability like the fluid
  // overlay above.
  bool any_sheep = false;
  for (const ir::Region& region : schematic.regions) {
    for (const ir::Entity& ent : region.entities) {
      if (ent.id == "minecraft:sheep") {
        any_sheep = true;
        break;
      }
    }
  }
  if (any_sheep) {
    if (pack.LoadTexturePixels("minecraft:entity/sheep/sheep").has_value()) {
      all_texture_paths.emplace("minecraft:entity/sheep/sheep");
    }
    if (pack.LoadTexturePixels("minecraft:entity/sheep/sheep_wool")
            .has_value()) {
      all_texture_paths.emplace("minecraft:entity/sheep/sheep_wool");
    }
  }
  // One shared atlas across all regions.
  auto atlas_and_slots = BuildAtlasWithSlots(
      pack, all_texture_paths, opts,
      /*skip_unresolved=*/false, opts);
  if (!atlas_and_slots) return std::unexpected(atlas_and_slots.error());
  absl::flat_hash_map<std::string, std::uint32_t>& texture_to_slot =
      atlas_and_slots->second;
  TextureAtlas atlas = std::move(atlas_and_slots->first);

  // Per-region walk. Each region uses its own BlockGroupTable for AO
  // and cull; cross-region neighbor checks are not performed, so faces
  // at region boundaries stay visible. Non-merging walks stream each
  // region's faces straight into the output meshes (no record buffer);
  // merging walks collect per region, emit, and reuse the buffers.
  TessellationResult result;
  result.atlas = atlas;

  std::vector<internal::VisibleFaceInfo> collect_opaque;
  std::vector<internal::VisibleFaceInfo> collect_transparent;
  RowNeighborBuffers rows;

  for (std::size_t ri = 0; ri < schematic.regions.size(); ++ri) {
    const ir::Region& region = schematic.regions[ri];
    const std::vector<ResolvedModel>& models = per_region_models[ri];

    // Build the group table for AO and cull (within this region only).
    const std::vector<std::uint8_t> palette_groups =
        BuildPaletteGroups(region, models, per_region_skipped[ri]);
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

    // Per-palette atlas slots for this region's palette, resolved once
    // (no per-face texture hashing in the walk).
    const PaletteSlotTable slots =
        BuildPaletteSlots(models, palette_fluid, texture_to_slot,
                          !overlay_texture.empty(), overlay_texture);

    const WalkCtx ctx{&region,
                       &models,
                       palette_groups.data(),
                       palette_fluid.data(),
                       &slots,
                       per_region_overlay[ri].data(),
                       &fluid_table,
                       &group_table,
                       opts.ambient_occlusion ? opts.ao_strength : 0.0f,
                       opts.cull_interior_faces,
                       opts.include_air,
                       /*world_offset=*/{region.bounds.origin[0],
                                          region.bounds.origin[1],
                                          region.bounds.origin[2]}};

    const ir::BoundingBox& b = region.bounds;
    if (!opts.merge_coplanar_faces) {
      FaceSink<true> sink{&result.atlas, &result.opaque_mesh,
                          &result.transparent_mesh};
      for (std::int32_t y = 0; y < b.size[1]; ++y) {
        for (std::int32_t z = 0; z < b.size[2]; ++z) {
          ProcessRow(ctx, {0, y, z}, y, z, b.size[0], rows, sink);
        }
      }
    } else {
      FaceSink<false> sink{&collect_opaque, &collect_transparent};
      for (std::int32_t y = 0; y < b.size[1]; ++y) {
        for (std::int32_t z = 0; z < b.size[2]; ++z) {
          ProcessRow(ctx, {0, y, z}, y, z, b.size[0], rows, sink);
        }
      }
      EmitCollectedFaces(collect_opaque,
                         collect_transparent,
                         result.atlas,
                         opts,
                         result.opaque_mesh,
                         result.transparent_mesh);
    }
  }

  // Entities mesh after the blocks: sheep quads carry their own atlas
  // slots and no AO/cull context.
  AppendSheep(schematic, texture_to_slot, result.atlas,
              result.opaque_mesh);

  return result;
}

}  // namespace fschema::geometry