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

#include "fschema/geometry/resource_pack.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/synchronization/mutex.h"
#include "fast_float/fast_float.h"
#include "fschema/base/error.h"
#include "fschema/geometry/internal/png_decode.h"
#include "fschema/geometry/internal/resolver.h"
#include "fschema/geometry/types.h"
#include "fschema/third_party/simdjson/simdjson.h"

namespace fschema::geometry {

namespace fs = std::filesystem;

namespace {

using internal::BlockNameToBlockstateRelPath;
using internal::BuildCubeFace;
using internal::CubeFaceGeometry;
using internal::CubeFaceUvRect;
using internal::FaceDir;
using internal::FluidCubeFaceUvRect;
using internal::IsPathSafe;
using internal::ModelFaceAxes;
using internal::ModelRefToRelPath;
using internal::ParseFaceDir;
using internal::ReadFileBytes;
using internal::StripNamespace;
using internal::TextureRefToRelPath;
using internal::VariantKeyMatches;

// Tint colormap slot for blocks whose model faces carry "tintindex",
// mirroring vanilla's block color handlers: which colormap applies
// depends on the block; blocks without a handler stay untinted.
[[nodiscard]] std::uint8_t TintSlotForBlock(
    std::string_view block_name) noexcept {
  std::string_view n = StripNamespace(block_name);
  if (n == "grass_block" || n == "grass" || n == "short_grass" ||
      n == "tall_grass" || n == "fern" || n == "large_fern" || n == "vine" ||
      n == "lily_pad" || n == "sugar_cane") {
    return 1;  // biome grass colormap
  }
  if (n == "birch_leaves") return 5;   // fixed birch color
  if (n == "spruce_leaves") return 6;  // fixed spruce color
  if (n == "oak_leaves" || n == "jungle_leaves" || n == "acacia_leaves" ||
      n == "dark_oak_leaves" || n == "mangrove_leaves") {
    return 2;  // biome foliage colormap
  }
  if (n == "water" || n == "flowing_water") return 3;
  if (n == "redstone_wire") return 4;
  return 0;
}

// Whether a block's faces belong to the transparent (blended) render
// layer, mirroring vanilla's per-block render types. Vanilla packs
// declare no render layer in their JSON (it is client-side data), so
// classification is by name; a model-declared "render_type" (custom
// packs) flags faces additionally during model resolution. Packed ice
// and blue ice stay opaque, as in vanilla; leaves render in the
// cutout layer (fancy graphics).
[[nodiscard]] bool TransparentForBlock(std::string_view block_name) noexcept {
  std::string_view n = StripNamespace(block_name);
  if (n == "water" || n == "flowing_water" || n == "glass" ||
      n == "tinted_glass" || n == "ice" || n == "frosted_ice" ||
      n == "slime_block" || n == "honey_block" || n == "nether_portal" ||
      n == "glass_pane" || n == "azalea" || n == "flowering_azalea") {
    return true;
  }
  return n.ends_with("_stained_glass") || n.ends_with("_stained_glass_pane") ||
         n.ends_with("_leaves");
}

// A blockstate-level model placement: the model ref plus the quarter
// turns declared next to it ("x"/"y"/"z" in a variants entry or a
// multipart apply object). Non-quarter values are dropped at parse
// time (the model itself still applies).
struct ModelPlacement {
  std::string model_ref;
  int rot_x = 0;  // degrees, normalized to [0,360), multiple of 90
  int rot_y = 0;
  int rot_z = 0;
  bool uvlock = false;
};

[[nodiscard]] int ReadQuarterTurn(simdjson::dom::element el, const char* key) {
  auto res = el[key];
  if (res.error() != simdjson::SUCCESS) return 0;
  // Some packs serialize the turn as a float ("y": 90.0); fall back
  // to integral doubles. Fractional values (90.5) stay ignored --
  // vanilla Gson would truncate those, a documented divergence.
  std::int64_t deg = 0;
  if (res.value().get_int64().get(deg) != simdjson::SUCCESS) {
    double d = 0.0;
    if (res.value().get_double().get(d) != simdjson::SUCCESS) return 0;
    if (d != std::trunc(d) || d < -1e9 || d > 1e9) return 0;
    deg = static_cast<std::int64_t>(d);
  }
  if (deg % 90 != 0) return 0;
  return static_cast<int>(((deg % 360) + 360) % 360);
}

// Reads the optional "uvlock" flag next to a model ref: rotated faces
// re-anchor their UVs to the final face direction's standard basis
// instead of riding the quad.
[[nodiscard]] bool ReadUvlock(simdjson::dom::element el) {
  auto res = el["uvlock"];
  if (res.error() != simdjson::SUCCESS) return false;
  bool uvlock = false;
  if (res.value().get_bool().get(uvlock) != simdjson::SUCCESS) return false;
  return uvlock;
}

// Vanilla FaceBakery.defaultFaceUV, in block units (px / 16): when a
// face omits its uv rect, the rect derives from the element bounds
// with the north/east axes mirrored (1 - coord). Bounds are used
// unclamped, as in vanilla: out-of-bounds elements produce
// out-of-range uvs (same GL_REPEAT semantics as merged quads).
[[nodiscard]] std::array<float, 4> DefaultFaceUvRect(
    FaceDir dir,
    const std::array<float, 3>& from,
    const std::array<float, 3>& to) {
  switch (dir) {
    case FaceDir::kNegY:  // down
      return {from[0], 1.0f - to[2], to[0], 1.0f - from[2]};
    case FaceDir::kPosY:  // up
      return {from[0], from[2], to[0], to[2]};
    case FaceDir::kNegZ:  // north
      return {1.0f - to[0], 1.0f - to[1], 1.0f - from[0], 1.0f - from[1]};
    case FaceDir::kPosZ:  // south
      return {from[0], 1.0f - to[1], to[0], 1.0f - from[1]};
    case FaceDir::kNegX:  // west
      return {from[2], 1.0f - to[1], to[2], 1.0f - from[1]};
    case FaceDir::kPosX:  // east
    default:
      return {1.0f - to[2], 1.0f - to[1], 1.0f - from[2], 1.0f - from[1]};
  }
}

// One 90-degree step of the blockstate convention on an in-plane
// coordinate pair (offsets from the rotation center):
//   axis 0 (x): (y,z) -> (z,-y)    up -> north
//   axis 1 (y): (x,z) -> (-z, x)   north -> east
//   axis 2 (z): (x,y) -> (-y, x)   east  -> up
//
// The x-axis convention runs OPPOSITE to y/z. Vanilla reference:
// piston.json maps facing=down with x=90 while the piston model
// (template_piston.json) carries its platform face on north, so
// vanilla x=90 rotates north -> down (cycle up -> north -> down ->
// south -> up). observer.json agrees (front on north, facing=up uses
// x=270). y follows the dropper/furnace family (front on north,
// facing=east uses y=90 -> north -> east).
inline void QuarterStepInPlane(float& a, float& b) {
  const float u = a;
  a = -b;
  b = u;
}

// The inverse quarter step: (a,b) -> (b,-a).
inline void QuarterStepInPlaneInv(float& a, float& b) {
  const float u = a;
  a = b;
  b = -u;
}

inline void RotateOffsetSteps(float v[3], int axis, int steps) {
  for (int i = 0; i < steps; ++i) {
    switch (axis) {
      case 0:
        QuarterStepInPlaneInv(v[1], v[2]);
        break;  // (y,z), vanilla sign: up -> north
      case 1:
        QuarterStepInPlane(v[0], v[2]);
        break;  // (x,z)
      case 2:
        QuarterStepInPlane(v[0], v[1]);
        break;  // (x,y)
    }
  }
}

// Cull face indices: 1=+X 2=-X 3=+Y 4=-Y 5=+Z 6=-Z (FaceDir values).
// Rotates the face normal with the same convention and snaps it back
// to an axis; 0 (no cull) stays 0.
[[nodiscard]] std::uint8_t RemapCullFace(std::uint8_t cull,
                                         int axis,
                                         int steps) {
  if (cull == 0 || steps == 0) return cull;
  static constexpr float kNormals[7][3] = {{0, 0, 0},
                                           {1, 0, 0},
                                           {-1, 0, 0},
                                           {0, 1, 0},
                                           {0, -1, 0},
                                           {0, 0, 1},
                                           {0, 0, -1}};
  float n[3] = {kNormals[cull][0], kNormals[cull][1], kNormals[cull][2]};
  RotateOffsetSteps(n, axis, steps);
  if (n[0] > 0.5f) return 1;
  if (n[0] < -0.5f) return 2;
  if (n[1] > 0.5f) return 3;
  if (n[1] < -0.5f) return 4;
  if (n[2] > 0.5f) return 5;
  if (n[2] < -0.5f) return 6;
  return 0;
}

// Applies variant/multipart quarter turns to a resolved model's
// faces: corners rotate about the block center (0.5,0.5,0.5), cull
// faces remap with the normals, and uvlock re-anchors the texture to
// the block (see internal::ModelFaceAxes). Element rotation already
// cleared the cull face of rotated elements, so remap only moves
// survivors.
void ApplyPlacementRotation(std::vector<ModelFace>& faces,
                            const ModelPlacement& p) {
  const int qx = (p.rot_x / 90) % 4;
  const int qy = (p.rot_y / 90) % 4;
  const int qz = (p.rot_z / 90) % 4;
  if (qx == 0 && qy == 0 && qz == 0) return;
  for (ModelFace& f : faces) {
    const std::uint8_t orig_cull = f.CullFace();
    for (std::array<float, 3>& c : f.corners) {
      float v[3] = {c[0] - 0.5f, c[1] - 0.5f, c[2] - 0.5f};
      RotateOffsetSteps(v, 0, qx);
      RotateOffsetSteps(v, 1, qy);
      RotateOffsetSteps(v, 2, qz);
      c = {v[0] + 0.5f, v[1] + 0.5f, v[2] + 0.5f};
    }
    std::uint8_t cull = f.CullFace();
    cull = RemapCullFace(cull, 0, qx);
    cull = RemapCullFace(cull, 1, qy);
    cull = RemapCullFace(cull, 2, qz);
    f.SetCullFace(cull);

    // uvlock: each corner samples the texture at its rotated position
    // projected onto the final direction's model image axes, as a
    // native face would. Rotated elements (cull 0) keep their
    // quad-attached UVs, as in vanilla.
    if (p.uvlock && orig_cull >= 1 && orig_cull <= 6 && f.CullFace() >= 1 &&
        f.CullFace() <= 6) {
      float ub[3], vb[3];
      ModelFaceAxes(static_cast<FaceDir>(f.CullFace()), ub, vb);
      float su[4], sv[4];
      for (int i = 0; i < 4; ++i) {
        su[i] = f.corners[i][0] * ub[0] + f.corners[i][1] * ub[1] +
                f.corners[i][2] * ub[2];
        sv[i] = f.corners[i][0] * vb[0] + f.corners[i][1] * vb[1] +
                f.corners[i][2] * vb[2];
      }
      float su_min = su[0], su_max = su[0], sv_min = sv[0], sv_max = sv[0];
      for (int i = 1; i < 4; ++i) {
        su_min = std::min(su_min, su[i]);
        su_max = std::max(su_max, su[i]);
        sv_min = std::min(sv_min, sv[i]);
        sv_max = std::max(sv_max, sv[i]);
      }
      const float du = su_max - su_min;
      const float dv = sv_max - sv_min;
      if (du > 1e-6f && dv > 1e-6f) {
        // Anchor to the face's own uv rect: min projection -> u0/v0,
        // max -> u1/v1. Scanning is required: corners 0/2 are not
        // always the rect's extremes under the per-direction vanilla
        // assignment.
        float ua = f.uvs[0][0], uc = f.uvs[0][0];
        float vb0 = f.uvs[0][1], vd = f.uvs[0][1];
        for (int i = 1; i < 4; ++i) {
          ua = std::min(ua, f.uvs[i][0]);
          uc = std::max(uc, f.uvs[i][0]);
          vb0 = std::min(vb0, f.uvs[i][1]);
          vd = std::max(vd, f.uvs[i][1]);
        }
        for (int i = 0; i < 4; ++i) {
          f.uvs[i][0] = ua + (su[i] - su_min) * (uc - ua) / du;
          f.uvs[i][1] = vb0 + (sv[i] - sv_min) * (vd - vb0) / dv;
        }
      }
    }
  }
}

// Optional element "rotation":
//   {"origin":[x,y,z], "axis":"x"|"y"|"z", "angle":deg, "rescale":bool}
struct ElementRotation {
  bool present = false;
  std::array<float, 3> origin{0.5f, 0.5f, 0.5f};  // normalized /16
  int axis = -1;                                  // 0=x 1=y 2=z
  float angle_deg = 0.0f;
  bool rescale = false;
};

[[nodiscard]] ElementRotation ParseElementRotation(
    simdjson::dom::element elem) {
  ElementRotation rot;
  auto rot_res = elem["rotation"];
  if (rot_res.error() != simdjson::SUCCESS) return rot;
  simdjson::dom::element rot_el = rot_res.value();

  auto origin_res = rot_el["origin"];
  if (origin_res.error() == simdjson::SUCCESS) {
    auto arr_res = origin_res.value().get_array();
    if (arr_res.error() == simdjson::SUCCESS && arr_res.value().size() >= 3) {
      for (int i = 0; i < 3; ++i) {
        double v = 0.0;
        if (arr_res.value().at(i).get_double().get(v) == simdjson::SUCCESS) {
          rot.origin[i] = static_cast<float>(v) / 16.0f;
        }
      }
    }
  }

  std::string axis;
  auto axis_res = rot_el["axis"];
  if (axis_res.error() == simdjson::SUCCESS &&
      axis_res.value().get_string().get(axis) == simdjson::SUCCESS) {
    if (axis == "x")
      rot.axis = 0;
    else if (axis == "y")
      rot.axis = 1;
    else if (axis == "z")
      rot.axis = 2;
  }

  auto angle_res = rot_el["angle"];
  if (angle_res.error() == simdjson::SUCCESS) {
    double angle = 0.0;
    if (angle_res.value().get_double().get(angle) == simdjson::SUCCESS) {
      rot.angle_deg = static_cast<float>(angle);
    }
  }

  auto rescale_res = rot_el["rescale"];
  if (rescale_res.error() == simdjson::SUCCESS) {
    bool rescale = false;
    if (rescale_res.value().get_bool().get(rescale) == simdjson::SUCCESS) {
      rot.rescale = rescale;
    }
  }

  rot.present = rot.axis >= 0;
  return rot;
}

// Applies an element rotation to one face. Note the opposite
// handedness to the blockstate quarter turns (vanilla quirk). A
// rotated face loses its cull face; rescale multiplies the two
// in-plane axes by 1 / max(|cos|, |sin|) about the origin (the
// vanilla CuboidRotation factor: 1/cos up to 45 degrees, saturating
// at 90). Every angle rescales, not just 45-degree multiples.
void ApplyElementRotation(ModelFace& mf, const ElementRotation& rot) {
  const float rad = rot.angle_deg * 3.14159265f / 180.0f;
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  const float axis_max = std::max(std::abs(c), std::abs(s));
  const float scale = rot.rescale && axis_max > 1e-6f ? 1.0f / axis_max : 1.0f;
  for (std::array<float, 3>& corner : mf.corners) {
    const float dx = corner[0] - rot.origin[0];
    const float dy = corner[1] - rot.origin[1];
    const float dz = corner[2] - rot.origin[2];
    switch (rot.axis) {
      case 1:  // y: (x,z) plane
        corner[0] = rot.origin[0] + (dx * c + dz * s) * scale;
        corner[1] = rot.origin[1] + dy;
        corner[2] = rot.origin[2] + (-dx * s + dz * c) * scale;
        break;
      case 0:  // x: (z,y) plane, same pattern
        corner[0] = rot.origin[0] + dx;
        corner[1] = rot.origin[1] + (-dz * s + dy * c) * scale;
        corner[2] = rot.origin[2] + (dz * c + dy * s) * scale;
        break;
      case 2:  // z: (y,x) plane, same pattern
        corner[0] = rot.origin[0] + (-dy * s + dx * c) * scale;
        corner[1] = rot.origin[1] + (dy * c + dx * s) * scale;
        corner[2] = rot.origin[2] + dz;
        break;
    }
  }
  mf.SetCullFace(0);
}

// Flow-texture path for a fluid block, or empty for non-fluids.
[[nodiscard]] std::string_view FluidFlowTexturePath(
    std::string_view block_name) noexcept {
  std::string_view n = StripNamespace(block_name);
  if (n == "water" || n == "flowing_water") return "minecraft:block/water_flow";
  if (n == "lava" || n == "flowing_lava") return "minecraft:block/lava_flow";
  return {};
}

// Vanilla renders fluids with a dedicated fluid renderer, not block
// models; fluid models resolve to zero faces, so synthesize a
// flat-topped cuboid instead: own height = amount / 9 from the
// blockstate `level` property. This cached cuboid is the isolated-cell
// shape; the tessellator morphs it into vanilla's sloped surface
// (corner averaging incl. the same-fluid-above full-height rule) and
// repaints/rotates the flow texture. Vanilla's 0.001 z-fighting insets
// stay out of scope.
[[nodiscard]] std::vector<ModelFace> SynthesizeFluidFaces(
    std::string_view block_name,
    const BlockProps& props,
    std::uint8_t block_tint_slot,
    std::string_view flow_texture) {
  std::vector<ModelFace> faces;
  std::string_view n = StripNamespace(block_name);
  const bool is_water = n == "water" || n == "flowing_water";
  const char* texture =
      is_water ? "minecraft:block/water_still" : "minecraft:block/lava_still";

  std::int64_t level = 0;
  if (auto lv = props.Get("level"); lv.has_value() && !lv->empty()) {
    const char* first = lv->data();
    const char* last = first + lv->size();
    fast_float::from_chars(first, last, level);  // unparsable stays level 0
  }
  if (level < 0) level = 0;
  if (level > 15) level = 15;
  // Vanilla fluid amounts (FlowingFluid#getOwnHeight): a source holds
  // amount 8, flowing level L holds 8 - L, and the falling range
  // L = 8..15 encodes amount 16 - L. Own height is amount / 9; 1.0 is
  // only reached through the "same fluid above" rule (tessellator).
  const float amount = level >= 8 ? 16.0f - static_cast<float>(level)
                                  : 8.0f - static_cast<float>(level);
  const float height = amount / 9.0f;

  const std::array<float, 3> from{0.0f, 0.0f, 0.0f};
  const std::array<float, 3> to{1.0f, height, 1.0f};
  const FaceDir dirs[6] = {FaceDir::kPosX,
                           FaceDir::kNegX,
                           FaceDir::kPosY,
                           FaceDir::kNegY,
                           FaceDir::kPosZ,
                           FaceDir::kNegZ};
  for (FaceDir dir : dirs) {
    const CubeFaceGeometry geom = BuildCubeFace(dir, from, to);
    ModelFace mf{};
    mf.corners = geom.corners;
    mf.uvs = FluidCubeFaceUvRect(
        dir, geom.corners, std::array<float, 4>{0.0f, 0.0f, 1.0f, 1.0f});
    // Up/down faces use the still sprite, the four sides the flow
    // sprite; sides fall back to still when the pack ships no flow
    // texture.
    const bool is_side = dir != FaceDir::kPosY && dir != FaceDir::kNegY;
    if (is_side && !flow_texture.empty()) {
      mf.texture_path = std::string(flow_texture);
    } else {
      mf.texture_path = texture;
    }
    mf.SetCullFace(geom.cull_face);
    mf.SetTintSlot(block_tint_slot);
    mf.SetTransparent(is_water);  // lava renders opaque
    mf.SetFluidFace(true);        // morphable at tessellate time
    faces.push_back(std::move(mf));
  }
  return faces;
}

// Overrides the transparent flag on every face.
void ApplyRenderTypeFlags(std::vector<ModelFace>& faces,
                          bool declared,
                          bool transparent) {
  if (!declared) return;
  for (ModelFace& f : faces) f.SetTransparent(transparent);
}

[[nodiscard]] bool ModelIsTransparent(std::span<const ModelFace> faces) {
  for (const ModelFace& f : faces) {
    if (f.IsTransparent()) return true;
  }
  return false;
}

}  // namespace

struct ResourcePack::Impl {
  fs::path root;

  struct TextureCacheEntry {
    std::unique_ptr<std::vector<std::uint8_t>> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
  };
  mutable absl::flat_hash_map<std::string, TextureCacheEntry>
      texture_pixel_cache;
  mutable absl::Mutex texture_mutex;

  mutable absl::flat_hash_map<std::string,
                              std::shared_ptr<simdjson::padded_string>>
      json_doc_cache;
  mutable absl::Mutex json_mutex;

  struct ModelKey {
    std::string name;
    std::size_t props_hash;
    bool operator==(const ModelKey&) const = default;
  };
  struct ModelKeyHash {
    std::size_t operator()(const ModelKey& k) const {
      return absl::Hash<std::pair<std::string, std::size_t>>{}(
          {k.name, k.props_hash});
    }
  };
  mutable absl::flat_hash_map<ModelKey,
                              std::unique_ptr<std::vector<ModelFace>>,
                              ModelKeyHash>
      resolved_model_cache;
  mutable absl::Mutex resolved_mutex;

  Impl() = default;

  // Cached texture -> returned view. Animated textures stack square
  // frames vertically; the entry stores first-frame dimensions and
  // the span is a prefix of the full decoded buffer.
  [[nodiscard]] static DecodedTexture MakeTexture(
      const TextureCacheEntry& e) noexcept {
    const std::size_t n = static_cast<std::size_t>(e.width) *
                          static_cast<std::size_t>(e.height) * 4;
    return DecodedTexture{
        std::span<const std::uint8_t>(e.pixels->data(), n),
        e.width,
        e.height,
    };
  }

  [[nodiscard]] ParseResult<DecodedTexture> LoadTexturePixels(
      std::string_view texture_ref) const {
    if (!IsPathSafe(texture_ref)) {
      return std::unexpected(ParseError::At(
          ParseError::Code::kModelMissingTexture, std::string(texture_ref), 0));
    }
    const std::string rel = TextureRefToRelPath(texture_ref);

    {
      absl::ReaderMutexLock lock(&texture_mutex);
      auto it = texture_pixel_cache.find(rel);
      if (it != texture_pixel_cache.end()) {
        return MakeTexture(it->second);
      }
    }

    absl::WriterMutexLock lock(&texture_mutex);
    // Double-check: another thread may have populated the cache while
    // we waited.
    auto it = texture_pixel_cache.find(rel);
    if (it != texture_pixel_cache.end()) {
      return MakeTexture(it->second);
    }

    auto bytes = ReadFileBytes(root / rel);
    if (!bytes) return std::unexpected(bytes.error());

    // Decode; ownership of the pixel buffer moves into the cache so the
    // returned span stays valid for the pack's lifetime.
    std::unique_ptr<std::vector<std::uint8_t>> owned_pixels;
    auto decoded = internal::DecodePng(
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(bytes->data()),
            bytes->size()),
        owned_pixels);
    if (!decoded) return std::unexpected(decoded.error());

    TextureCacheEntry entry;
    entry.width = decoded->width;
    entry.height = decoded->height;
    // Animated strip: keep the full buffer, report the first frame.
    if (entry.height > entry.width && entry.height % entry.width == 0) {
      entry.height = entry.width;
    }
    entry.pixels = std::move(owned_pixels);
    auto [new_it, _] = texture_pixel_cache.emplace(rel, std::move(entry));
    return MakeTexture(new_it->second);
  }

  [[nodiscard]] ParseResult<std::shared_ptr<simdjson::padded_string>>
  LoadJsonDoc(const std::string& rel_path,
              ParseError::Code missing_code) const {
    {
      absl::ReaderMutexLock lock(&json_mutex);
      auto it = json_doc_cache.find(rel_path);
      if (it != json_doc_cache.end()) return it->second;
    }

    absl::WriterMutexLock lock(&json_mutex);
    // Double-check after acquiring the writer lock.
    auto it = json_doc_cache.find(rel_path);
    if (it != json_doc_cache.end()) return it->second;

    auto bytes = ReadFileBytes(root / rel_path);
    if (!bytes)
      return std::unexpected(ParseError::At(missing_code, rel_path, 0));

    auto padded = std::make_shared<simdjson::padded_string>(std::move(*bytes));
    json_doc_cache.emplace(rel_path, padded);
    return padded;
  }

  [[nodiscard]] ParseResult<std::vector<ModelPlacement>>
  ResolveBlockstateToModels(std::string_view block_name,
                            const BlockProps& props) const {
    const std::string rel = BlockNameToBlockstateRelPath(block_name);
    auto doc = LoadJsonDoc(rel, ParseError::Code::kMalformedBlockstateJson);
    if (!doc) return std::unexpected(doc.error());

    simdjson::dom::parser parser;
    auto root_res = parser.parse(**doc);
    if (root_res.error() != simdjson::SUCCESS) {
      return std::unexpected(
          ParseError::At(ParseError::Code::kMalformedBlockstateJson, rel, 0));
    }
    simdjson::dom::element root = root_res.value();

    auto variants_res = root["variants"];
    if (variants_res.error() == simdjson::SUCCESS) {
      return ResolveVariantsForm(variants_res.value(), props, rel);
    }
    auto multipart_res = root["multipart"];
    if (multipart_res.error() == simdjson::SUCCESS) {
      return ResolveMultipartForm(multipart_res.value(), props, rel);
    }
    return std::unexpected(
        ParseError::At(ParseError::Code::kMalformedBlockstateJson, rel, 0));
  }

  [[nodiscard]] ParseResult<std::vector<ModelFace>> ResolveModelToFaces(
      std::string_view model_ref,
      std::uint8_t block_tint_slot,
      absl::flat_hash_set<std::string>* texture_paths_out) const {
    absl::flat_hash_set<std::string> visited;
    absl::flat_hash_map<std::string, std::string> textures;
    return ResolveModelToFacesImpl(
        model_ref, 0, visited, textures, texture_paths_out, block_tint_slot);
  }

 private:
  [[nodiscard]] ParseResult<std::vector<ModelPlacement>> ResolveVariantsForm(
      const simdjson::dom::element& variants_el,
      const BlockProps& props,
      const std::string& rel_path) const {
    std::vector<ModelPlacement> out;
    simdjson::dom::object variants = variants_el.get_object();
    for (auto [key, value] : variants) {
      if (!VariantKeyMatches(key, props)) continue;
      simdjson::dom::element chosen = value;
      if (value.is_array()) {
        auto arr = value.get_array();
        if (arr.size() == 0) continue;
        chosen = arr.at(0);
      }
      std::string model_ref;
      auto model_res = chosen["model"];
      if (model_res.error() != simdjson::SUCCESS) continue;
      if (model_res.value().get_string().get(model_ref) != simdjson::SUCCESS) {
        continue;
      }

      if (!model_ref.empty() && model_ref[0] != ':' &&
          model_ref.find(':') == std::string::npos) {
        model_ref = "minecraft:" + model_ref;
      }
      ModelPlacement placement;
      placement.model_ref = std::move(model_ref);
      placement.rot_x = ReadQuarterTurn(chosen, "x");
      placement.rot_y = ReadQuarterTurn(chosen, "y");
      placement.rot_z = ReadQuarterTurn(chosen, "z");
      placement.uvlock = ReadUvlock(chosen);
      out.push_back(std::move(placement));
      return out;
    }
    return std::unexpected(
        ParseError::At(ParseError::Code::kUnknownBlockState, rel_path, 0));
  }

  [[nodiscard]] ParseResult<std::vector<ModelPlacement>> ResolveMultipartForm(
      const simdjson::dom::element& multipart_el,
      const BlockProps& props,
      const std::string& rel_path) const {
    std::vector<ModelPlacement> out;
    simdjson::dom::array cases = multipart_el.get_array();
    for (simdjson::dom::element case_el : cases) {
      auto when_res = case_el["when"];
      bool matches = true;
      if (when_res.error() == simdjson::SUCCESS) {
        auto m = MultipartWhenMatches(when_res.value(), props, 0);
        if (!m) return std::unexpected(m.error());
        matches = *m;
      }
      if (!matches) continue;

      auto apply_res = case_el["apply"];
      if (apply_res.error() != simdjson::SUCCESS) continue;
      simdjson::dom::element apply = apply_res.value();
      if (apply.is_array()) {
        // An apply array is a weighted random pick in vanilla; the
        // variants form above takes the first entry, so mirror that
        // here for determinism.
        auto arr = apply.get_array();
        if (arr.size() == 0) continue;
        ExtractPlacement(arr.at(0), out);
      } else {
        ExtractPlacement(apply, out);
      }
    }
    if (out.empty()) {
      return std::unexpected(
          ParseError::At(ParseError::Code::kUnknownBlockState, rel_path, 0));
    }
    return out;
  }

  static void ExtractPlacement(simdjson::dom::element apply_el,
                               std::vector<ModelPlacement>& out) {
    ModelPlacement placement;
    if (apply_el["model"].get_string().get(placement.model_ref) !=
        simdjson::SUCCESS) {
      return;
    }
    if (!placement.model_ref.empty() && placement.model_ref[0] != ':' &&
        placement.model_ref.find(':') == std::string::npos) {
      placement.model_ref = "minecraft:" + placement.model_ref;
    }
    placement.rot_x = ReadQuarterTurn(apply_el, "x");
    placement.rot_y = ReadQuarterTurn(apply_el, "y");
    placement.rot_z = ReadQuarterTurn(apply_el, "z");
    placement.uvlock = ReadUvlock(apply_el);
    out.push_back(std::move(placement));
  }

  // A `when` value matches when it equals the property or is a
  // pipe-separated alternative ("side|up", redstone_wire.json); JSON
  // arrays of strings carry the same any-of meaning.
  static bool WhenValueMatches(const BlockProps& props,
                               std::string_view key,
                               simdjson::dom::element value) {
    auto got = props.Get(key);
    if (!got.has_value()) return false;
    if (value.is_array()) {
      for (simdjson::dom::element v : value.get_array()) {
        std::string s;
        if (v.get_string().get(s) == simdjson::SUCCESS && *got == s) {
          return true;
        }
      }
      return false;
    }
    std::string v;
    if (value.get_string().get(v) != simdjson::SUCCESS) return false;
    std::string_view rest(v);
    while (!rest.empty()) {
      const std::size_t bar = rest.find('|');
      const std::string_view alt =
          bar == std::string_view::npos ? rest : rest.substr(0, bar);
      if (*got == alt) return true;
      if (bar == std::string_view::npos) break;
      rest = rest.substr(bar + 1);
    }
    return false;
  }

  // Evaluates a multipart `when` condition: a plain object matches when
  // every entry matches (values may be pipe-separated alternatives or
  // arrays); "OR"/"AND" entries take arrays of sub-conditions and may
  // nest. Vanilla examples: fire.json uses OR, redstone_wire.json uses
  // OR plus "side|up" values.
  static ParseResult<bool> MultipartWhenMatches(simdjson::dom::element when_el,
                                                const BlockProps& props,
                                                int depth) {
    if (depth > 8) {
      return std::unexpected(
          ParseError::At(ParseError::Code::kUnsupportedMultipart, "when", 0));
    }
    auto when_obj_res = when_el.get_object();
    if (when_obj_res.error() != simdjson::SUCCESS) return false;
    for (auto [key, value] : when_obj_res.value()) {
      if (key == "OR" || key == "AND") {
        auto arr_res = value.get_array();
        if (arr_res.error() != simdjson::SUCCESS) return false;
        bool sub = (key == "AND");
        for (simdjson::dom::element cond : arr_res.value()) {
          auto m = MultipartWhenMatches(cond, props, depth + 1);
          if (!m) return std::unexpected(m.error());
          if (key == "OR" && *m) {
            sub = true;
            break;
          }
          if (key == "AND" && !*m) {
            sub = false;
            break;
          }
        }
        if (!sub) return false;
        continue;
      }
      if (!WhenValueMatches(props, key, value)) return false;
    }
    return true;
  }

  [[nodiscard]] ParseResult<std::vector<ModelFace>> ResolveModelToFacesImpl(
      std::string_view model_ref,
      int depth,
      absl::flat_hash_set<std::string>& visited,
      absl::flat_hash_map<std::string, std::string>& textures,
      absl::flat_hash_set<std::string>* texture_paths_out,
      std::uint8_t block_tint_slot) const {
    const std::string key(model_ref);
    if (depth > 32 || visited.contains(key)) {
      return std::unexpected(
          ParseError::At(ParseError::Code::kMalformedModelJson,
                         ModelRefToRelPath(model_ref),
                         0));
    }
    visited.insert(key);

    const std::string rel = ModelRefToRelPath(model_ref);
    auto doc = LoadJsonDoc(rel, ParseError::Code::kMalformedModelJson);
    if (!doc) return std::unexpected(doc.error());

    simdjson::dom::parser parser;
    auto root_res = parser.parse(**doc);
    if (root_res.error() != simdjson::SUCCESS) {
      return std::unexpected(
          ParseError::At(ParseError::Code::kMalformedModelJson, rel, 0));
    }
    simdjson::dom::element root = root_res.value();

    // Model-declared render type (custom packs; vanilla leaves it out
    // and classification falls back to the block name): translucent and
    // cutout models render in the transparent layer. A model's
    // declaration applies to every face it returns, inherited ones
    // included, and a child's declaration replaces the parent's.
    bool render_type_declared = false;
    bool render_type_transparent = false;
    auto render_type_res = root["render_type"];
    if (render_type_res.error() == simdjson::SUCCESS) {
      std::string rt;
      if (render_type_res.value().get_string().get(rt) == simdjson::SUCCESS) {
        std::string_view r = rt;
        if (r.starts_with("minecraft:")) r.remove_prefix(10);
        render_type_declared = true;
        render_type_transparent = r == "translucent" || r == "cutout" ||
                                  r == "cutout_mipped" || r == "tripwire";
      }
    }

    // Merge this model's textures into the inherited map. The map fills
    // child-first (this model, then its ancestors), so emplace keeps the
    // child's value when parent and child define the same variable --
    // vanilla's override order. Newer vanilla packs may wrap the path in
    // an object ("all": {"force_translucent": true, "sprite": "..."});
    // only the sprite field names the texture.
    auto tex_res = root["textures"];
    if (tex_res.error() == simdjson::SUCCESS) {
      auto tex_obj_res = tex_res.value().get_object();
      if (tex_obj_res.error() == simdjson::SUCCESS) {
        for (auto [k, v] : tex_obj_res.value()) {
          std::string path;
          if (v.get_string().get(path) != simdjson::SUCCESS) {
            auto sprite_res = v["sprite"];
            if (sprite_res.error() != simdjson::SUCCESS ||
                sprite_res.value().get_string().get(path) !=
                    simdjson::SUCCESS) {
              continue;
            }
          }
          textures.emplace(std::string(k), std::move(path));
        }
      }
    }

    // Recurse into the parent; its faces are inherited when this model
    // defines no `elements` of its own. Parent failures propagate: a
    // missing or malformed ancestor must fail the resolve, not silently
    // produce an empty mesh.
    std::vector<ModelFace> parent_faces;
    auto parent_res = root["parent"];
    if (parent_res.error() == simdjson::SUCCESS) {
      std::string parent_ref;
      if (parent_res.value().get_string().get(parent_ref) ==
              simdjson::SUCCESS &&
          !parent_ref.empty()) {
        if (!parent_ref.starts_with("minecraft:") &&
            parent_ref.find(':') == std::string::npos) {
          parent_ref = "minecraft:" + parent_ref;
        }
        auto r = ResolveModelToFacesImpl(parent_ref,
                                         depth + 1,
                                         visited,
                                         textures,
                                         texture_paths_out,
                                         block_tint_slot);
        if (!r.has_value()) {
          return std::unexpected(r.error());
        }
        parent_faces = std::move(*r);
      }
    }

    // A model's own `elements` override the parent's; without them the
    // parent faces are inherited.
    auto elements_res = root["elements"];
    if (elements_res.error() != simdjson::SUCCESS) {
      ApplyRenderTypeFlags(
          parent_faces, render_type_declared, render_type_transparent);
      return parent_faces;
    }
    auto elements_arr_res = elements_res.value().get_array();
    if (elements_arr_res.error() != simdjson::SUCCESS) {
      ApplyRenderTypeFlags(
          parent_faces, render_type_declared, render_type_transparent);
      return parent_faces;
    }

    std::vector<ModelFace> faces;
    for (simdjson::dom::element elem : elements_arr_res.value()) {
      EmitElementFaces(
          elem, textures, faces, texture_paths_out, block_tint_slot);
    }
    ApplyRenderTypeFlags(faces, render_type_declared, render_type_transparent);
    return faces;
  }

  void EmitElementFaces(
      simdjson::dom::element elem,
      const absl::flat_hash_map<std::string, std::string>& textures,
      std::vector<ModelFace>& faces,
      absl::flat_hash_set<std::string>* texture_paths_out,
      std::uint8_t block_tint_slot) const {
    std::array<float, 3> from{};
    std::array<float, 3> to{};
    auto from_res = elem["from"];
    auto to_res = elem["to"];
    if (from_res.error() != simdjson::SUCCESS ||
        to_res.error() != simdjson::SUCCESS)
      return;
    simdjson::dom::array from_arr = from_res.value().get_array();
    simdjson::dom::array to_arr = to_res.value().get_array();
    if (from_arr.size() < 3 || to_arr.size() < 3) return;
    for (int i = 0; i < 3; ++i) {
      double fv, tv;
      if (from_arr.at(i).get_double().get(fv) != simdjson::SUCCESS) return;
      if (to_arr.at(i).get_double().get(tv) != simdjson::SUCCESS) return;
      from[i] = static_cast<float>(fv) / 16.0f;
      to[i] = static_cast<float>(tv) / 16.0f;
    }

    auto faces_res = elem["faces"];
    if (faces_res.error() != simdjson::SUCCESS) return;
    simdjson::dom::object faces_obj = faces_res.value().get_object();
    const ElementRotation rot = ParseElementRotation(elem);
    for (auto [dir_name, face_el] : faces_obj) {
      const FaceDir dir = ParseFaceDir(dir_name);
      CubeFaceGeometry geom = BuildCubeFace(dir, from, to);

      ModelFace mf{};
      mf.corners = geom.corners;

      std::array<float, 4> uv_rect{0.0f, 0.0f, 1.0f, 1.0f};
      auto uv_res = face_el["uv"];
      if (uv_res.error() == simdjson::SUCCESS) {
        auto uv_arr_res = uv_res.value().get_array();
        if (uv_arr_res.error() == simdjson::SUCCESS) {
          simdjson::dom::array uv_arr = uv_arr_res.value();
          if (uv_arr.size() >= 4) {
            double u0, v0, u1, v1;
            if (uv_arr.at(0).get_double().get(u0) == simdjson::SUCCESS &&
                uv_arr.at(1).get_double().get(v0) == simdjson::SUCCESS &&
                uv_arr.at(2).get_double().get(u1) == simdjson::SUCCESS &&
                uv_arr.at(3).get_double().get(v1) == simdjson::SUCCESS) {
              uv_rect = {static_cast<float>(u0) / 16.0f,
                         static_cast<float>(v0) / 16.0f,
                         static_cast<float>(u1) / 16.0f,
                         static_cast<float>(v1) / 16.0f};
            }
          }
        }
      }
      if (uv_res.error() != simdjson::SUCCESS) {
        // No explicit uv rect: derive it from the element bounds like
        // vanilla FaceBakery.defaultFaceUV. A malformed-but-present
        // "uv" keeps the [0,1]^2 default.
        uv_rect = DefaultFaceUvRect(dir, from, to);
      }
      mf.uvs = CubeFaceUvRect(dir, geom.corners, uv_rect);

      // Resolve "#var" against the textures map; values may be
      // references themselves ("#skin" -> "#inner" -> path). Some
      // vanilla models omit the "#" (heavy_core.json uses a bare
      // "all"), so bare names get a map lookup before falling back to
      // a direct path. The hop bound leaves cycles unresolved.
      std::string texture_var;
      auto tex_res = face_el["texture"];
      if (tex_res.error() == simdjson::SUCCESS) {
        tex_res.value().get_string().get(texture_var);
      }
      std::string resolved_path;
      if (!texture_var.empty()) {
        std::string value = std::move(texture_var);
        bool direct_path = false;
        for (int hops = 0; hops < 8 && !value.empty(); ++hops) {
          std::string name = value[0] == '#' ? value.substr(1) : value;
          auto it = textures.find(name);
          if (it == textures.end()) {
            if (value[0] != '#') direct_path = true;
            break;
          }
          value = it->second;
        }
        if (direct_path || (!value.empty() && value[0] != '#')) {
          resolved_path = std::move(value);
        }
      }
      if (!resolved_path.empty() && texture_paths_out) {
        texture_paths_out->insert(resolved_path);
      }
      mf.texture_path = std::move(resolved_path);
      mf.SetCullFace(geom.cull_face);
      // Vanilla tints a face only when its model declares "tintindex";
      // any non-negative index selects the same block colormap.
      bool has_tintindex = false;
      auto tint_res = face_el["tintindex"];
      if (tint_res.error() == simdjson::SUCCESS) {
        std::int64_t tintindex = -1;
        if (tint_res.value().get_int64().get(tintindex) == simdjson::SUCCESS) {
          has_tintindex = tintindex >= 0;
        }
      }
      mf.SetTintSlot(has_tintindex ? block_tint_slot : 0);
      mf.SetTransparent(false);
      if (rot.present && rot.angle_deg != 0.0f) {
        ApplyElementRotation(mf, rot);
      }
      faces.push_back(mf);
    }
  }
};

ResourcePack::ResourcePack(ResourcePack&&) noexcept = default;
ResourcePack& ResourcePack::operator=(ResourcePack&&) noexcept = default;
ResourcePack::~ResourcePack() = default;

ParseResult<std::unique_ptr<ResourcePack>> ResourcePack::Load(
    std::string_view path) {
  fs::path root(path);
  std::error_code ec;

  if (!fs::is_directory(root, ec)) {
    return std::unexpected(ParseError::At(
        ParseError::Code::kResourcePackNotFound, std::string(path), 0));
  }
  fs::path abs_root = fs::absolute(root, ec);
  if (ec) {
    return std::unexpected(ParseError::At(
        ParseError::Code::kResourcePackNotFound, std::string(path), 0));
  }
  if (!fs::is_directory(abs_root / "assets" / "minecraft", ec)) {
    return std::unexpected(
        ParseError::At(ParseError::Code::kResourcePackNotFound,
                       std::string(path) + " (missing assets/minecraft/)",
                       0));
  }

  auto pack = std::unique_ptr<ResourcePack>(new ResourcePack());
  pack->impl_ = std::make_unique<Impl>();
  pack->impl_->root = std::move(abs_root);
  return pack;
}

ParseResult<ResolvedModel> ResourcePack::Resolve(
    std::string_view block_name,
    const BlockProps& props) const {
  if (!impl_) {
    return std::unexpected(ParseError::At(
        ParseError::Code::kUnknownBlockState, std::string(block_name), 0));
  }

  const Impl::ModelKey key{std::string(block_name), props.StableHash()};
  {
    absl::ReaderMutexLock lock(&impl_->resolved_mutex);
    auto it = impl_->resolved_model_cache.find(key);
    if (it != impl_->resolved_model_cache.end()) {
      return ResolvedModel{std::span<const ModelFace>(*it->second),
                           ModelIsTransparent(*it->second)};
    }
  }

  auto models_res = impl_->ResolveBlockstateToModels(block_name, props);
  if (!models_res) return std::unexpected(models_res.error());

  // The tint colormap depends only on the block name, so it is a
  // stable function of the L2 cache key.
  const std::uint8_t block_tint_slot = TintSlotForBlock(block_name);
  std::vector<ModelFace> all_faces;
  for (const auto& placement : *models_res) {
    auto faces = impl_->ResolveModelToFaces(placement.model_ref,
                                            block_tint_slot,
                                            /*texture_paths_out=*/nullptr);
    if (!faces) return std::unexpected(faces.error());
    ApplyPlacementRotation(*faces, placement);
    for (auto& f : *faces) all_faces.push_back(std::move(f));
  }

  // Fluid models resolve to zero faces; synthesize the cuboid (see
  // SynthesizeFluidFaces). Sides reference the flow texture only when
  // the pack ships it: LoadTexturePixels doubles as the availability
  // probe, and the model cache keeps misses to one stat per state.
  //
  // The synthesis is gated on the block actually being a fluid:
  // modern vanilla (1.21.2+) gives several non-fluid blocks
  // particle-only models with no elements -- chest, trapped_chest,
  // ender_chest and the copper chests render through the block
  // entity renderer, bubble_column likewise -- and they used to fall
  // into this branch and come back as a full LAVA cuboid. Non-fluid
  // empty models now resolve to zero faces: they render nothing and
  // occlude nothing (BuildPaletteGroups).
  if (all_faces.empty() && !FluidFlowTexturePath(block_name).empty()) {
    std::string flow_tex;
    if (std::string_view p = FluidFlowTexturePath(block_name);
        impl_->LoadTexturePixels(p).has_value()) {
      flow_tex = std::string(p);
    }
    all_faces =
        SynthesizeFluidFaces(block_name, props, block_tint_slot, flow_tex);
  }

  // Name-classified render layer (vanilla packs declare no render
  // types); a model-declared "render_type" already flagged faces above.
  if (TransparentForBlock(block_name)) {
    for (ModelFace& f : all_faces) f.SetTransparent(true);
  }

  absl::WriterMutexLock lock(&impl_->resolved_mutex);
  auto it = impl_->resolved_model_cache.find(key);
  if (it != impl_->resolved_model_cache.end()) {
    return ResolvedModel{std::span<const ModelFace>(*it->second),
                         ModelIsTransparent(*it->second)};
  }
  auto owned = std::make_unique<std::vector<ModelFace>>(std::move(all_faces));
  const bool transparent = ModelIsTransparent(*owned);
  std::span<const ModelFace> view(*owned);
  impl_->resolved_model_cache.emplace(key, std::move(owned));
  return ResolvedModel{view, transparent};
}

ParseResult<DecodedTexture> ResourcePack::LoadTexturePixels(
    std::string_view texture_path) const {
  if (!impl_) {
    return std::unexpected(ParseError::At(
        ParseError::Code::kModelMissingTexture, std::string(texture_path), 0));
  }
  return impl_->LoadTexturePixels(texture_path);
}

}  // namespace fschema::geometry
