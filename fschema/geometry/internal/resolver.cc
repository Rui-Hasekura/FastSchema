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

#include "fschema/geometry/internal/resolver.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "fschema/base/error.h"
#include "fschema/geometry/types.h"

namespace fschema::geometry::internal {

namespace fs = std::filesystem;

std::string_view StripNamespace(std::string_view ref) {
  constexpr std::string_view kNs = "minecraft:";
  if (ref.starts_with(kNs)) ref.remove_prefix(kNs.size());
  return ref;
}

std::string TextureRefToRelPath(std::string_view ref) {
  std::string_view local = StripNamespace(ref);
  std::string s;
  s.reserve(32 + local.size());
  s.append("assets/minecraft/textures/");
  s.append(local);
  s.append(".png");
  return s;
}

std::string BlockNameToBlockstateRelPath(std::string_view block_name) {
  std::string_view local = StripNamespace(block_name);
  if (const std::size_t slash = local.rfind('/');
      slash != std::string_view::npos) {
    local = local.substr(slash + 1);
  }
  std::string s;
  s.reserve(40 + local.size());
  s.append("assets/minecraft/blockstates/");
  s.append(local);
  s.append(".json");
  return s;
}

std::string ModelRefToRelPath(std::string_view model_ref) {
  std::string_view local = StripNamespace(model_ref);
  std::string s;
  s.reserve(40 + local.size());
  s.append("assets/minecraft/models/");
  s.append(local);
  s.append(".json");
  return s;
}

bool IsPathSafe(std::string_view relative) {
  if (relative.empty()) return false;
  if (relative[0] == '/' || relative[0] == '\\') return false;
  if (relative.size() >= 2 && relative[1] == ':') return false;

  std::size_t i = 0;
  while (i < relative.size()) {
    std::size_t next = relative.find_first_of("/\\", i);
    std::string_view seg = relative.substr(
        i, next == std::string_view::npos ? std::string_view::npos : next - i);
    if (seg == "..") return false;
    if (next == std::string_view::npos) break;
    i = next + 1;
  }
  return true;
}

ParseResult<std::string> ReadFileBytes(const fs::path& path) {
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    return std::unexpected(ParseError::At(
        ParseError::Code::kModelMissingTexture, path.generic_string(), 0));
  }
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return std::unexpected(ParseError::At(
        ParseError::Code::kModelMissingTexture, path.generic_string(), 0));
  }
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool VariantKeyMatches(std::string_view key, const BlockProps& props) {
  if (key.empty()) return true;

  std::size_t pair_start = 0;
  while (pair_start <= key.size()) {
    std::size_t comma = key.find(',', pair_start);
    std::string_view pair =
        key.substr(pair_start,
                   comma == std::string_view::npos ? std::string_view::npos
                                                   : comma - pair_start);
    if (!pair.empty()) {
      const std::size_t eq = pair.find('=');
      if (eq == std::string_view::npos) return false;
      std::string_view k = pair.substr(0, eq);
      std::string_view v = pair.substr(eq + 1);
      auto got = props.Get(k);
      if (!got.has_value() || *got != v) return false;
    }
    if (comma == std::string_view::npos) break;
    pair_start = comma + 1;
  }
  return true;
}

FaceDir ParseFaceDir(std::string_view name) {
  if (name == "south") return FaceDir::kPosZ;
  if (name == "north") return FaceDir::kNegZ;
  if (name == "east") return FaceDir::kPosX;
  if (name == "west") return FaceDir::kNegX;
  if (name == "up") return FaceDir::kPosY;
  return FaceDir::kNegY;  // "down"
}

CubeFaceGeometry BuildCubeFace(FaceDir dir,
                               const std::array<float, 3>& from,
                               const std::array<float, 3>& to) {
  const float x0 = from[0], y0 = from[1], z0 = from[2];
  const float x1 = to[0], y1 = to[1], z1 = to[2];

  CubeFaceGeometry g{};
  switch (dir) {
    case FaceDir::kNegZ:  // north, normal -Z
      g.corners = {
          {{{x0, y0, z0}}, {{x1, y0, z0}}, {{x1, y1, z0}}, {{x0, y1, z0}}}};
      g.normal = {0.0f, 0.0f, -1.0f};
      g.cull_face = static_cast<std::uint8_t>(FaceDir::kNegZ);
      break;
    case FaceDir::kPosZ:  // south, normal +Z
      g.corners = {
          {{{x0, y0, z1}}, {{x0, y1, z1}}, {{x1, y1, z1}}, {{x1, y0, z1}}}};
      g.normal = {0.0f, 0.0f, 1.0f};
      g.cull_face = static_cast<std::uint8_t>(FaceDir::kPosZ);
      break;
    case FaceDir::kPosX:  // east, normal +X
      g.corners = {
          {{{x1, y0, z0}}, {{x1, y0, z1}}, {{x1, y1, z1}}, {{x1, y1, z0}}}};
      g.normal = {1.0f, 0.0f, 0.0f};
      g.cull_face = static_cast<std::uint8_t>(FaceDir::kPosX);
      break;
    case FaceDir::kNegX:  // west, normal -X
      g.corners = {
          {{{x0, y0, z0}}, {{x0, y1, z0}}, {{x0, y1, z1}}, {{x0, y0, z1}}}};
      g.normal = {-1.0f, 0.0f, 0.0f};
      g.cull_face = static_cast<std::uint8_t>(FaceDir::kNegX);
      break;
    case FaceDir::kPosY:  // up, normal +Y
      g.corners = {
          {{{x0, y1, z0}}, {{x1, y1, z0}}, {{x1, y1, z1}}, {{x0, y1, z1}}}};
      g.normal = {0.0f, 1.0f, 0.0f};
      g.cull_face = static_cast<std::uint8_t>(FaceDir::kPosY);
      break;
    case FaceDir::kNegY:  // down, normal -Y
      g.corners = {
          {{{x0, y0, z0}}, {{x0, y0, z1}}, {{x1, y0, z1}}, {{x1, y0, z0}}}};
      g.normal = {0.0f, -1.0f, 0.0f};
      g.cull_face = static_cast<std::uint8_t>(FaceDir::kNegY);
      break;
  }
  return g;
}

void FluidFaceAxes(FaceDir dir, float u[3], float v[3]) noexcept {
  switch (dir) {
    case FaceDir::kPosX:  // east:  u = +Z, v = -Y
      u[0] = 0;
      u[1] = 0;
      u[2] = 1;
      v[0] = 0;
      v[1] = -1;
      v[2] = 0;
      break;
    case FaceDir::kNegX:  // west:  u = -Z, v = -Y
      u[0] = 0;
      u[1] = 0;
      u[2] = -1;
      v[0] = 0;
      v[1] = -1;
      v[2] = 0;
      break;
    case FaceDir::kPosY:  // up:    u = +X, v = +Z
    case FaceDir::kNegY:  // down:  u = +X, v = +Z
      u[0] = 1;
      u[1] = 0;
      u[2] = 0;
      v[0] = 0;
      v[1] = 0;
      v[2] = 1;
      break;
    case FaceDir::kPosZ:  // south: u = -X, v = -Y
      u[0] = -1;
      u[1] = 0;
      u[2] = 0;
      v[0] = 0;
      v[1] = -1;
      v[2] = 0;
      break;
    case FaceDir::kNegZ:  // north: u = +X, v = -Y
    default:
      u[0] = 1;
      u[1] = 0;
      u[2] = 0;
      v[0] = 0;
      v[1] = -1;
      v[2] = 0;
      break;
  }
}

void EntityFaceAxes(FaceDir dir, float u[3], float v[3]) noexcept {
  // Entity box net: the side strips form a ring, so the north strip's
  // low-u edge is the box's west side and the south strip's low-u edge
  // its east side -- the u of every side face runs opposite to
  // ModelFaceAxes (see the header comment for the full net).
  switch (dir) {
    case FaceDir::kPosX:  // east:  u = +Z, v = -Y
      u[0] = 0;
      u[1] = 0;
      u[2] = 1;
      v[0] = 0;
      v[1] = -1;
      v[2] = 0;
      break;
    case FaceDir::kNegX:  // west:  u = -Z, v = -Y
      u[0] = 0;
      u[1] = 0;
      u[2] = -1;
      v[0] = 0;
      v[1] = -1;
      v[2] = 0;
      break;
    case FaceDir::kPosZ:  // south: u = -X, v = -Y
      u[0] = -1;
      u[1] = 0;
      u[2] = 0;
      v[0] = 0;
      v[1] = -1;
      v[2] = 0;
      break;
    case FaceDir::kNegZ:  // north: u = +X, v = -Y
      u[0] = 1;
      u[1] = 0;
      u[2] = 0;
      v[0] = 0;
      v[1] = -1;
      v[2] = 0;
      break;
    case FaceDir::kPosY:  // up / down fold back onto the block axes
    case FaceDir::kNegY:
    default:
      ModelFaceAxes(dir, u, v);
      break;
  }
}

void ModelFaceAxes(FaceDir dir, float u[3], float v[3]) noexcept {
  switch (dir) {
    case FaceDir::kPosX:  // east:  u = -Z, v = -Y
      u[0] = 0;
      u[1] = 0;
      u[2] = -1;
      v[0] = 0;
      v[1] = -1;
      v[2] = 0;
      break;
    case FaceDir::kNegX:  // west:  u = +Z, v = -Y
      u[0] = 0;
      u[1] = 0;
      u[2] = 1;
      v[0] = 0;
      v[1] = -1;
      v[2] = 0;
      break;
    case FaceDir::kPosY:  // up:    u = +X, v = +Z
      u[0] = 1;
      u[1] = 0;
      u[2] = 0;
      v[0] = 0;
      v[1] = 0;
      v[2] = 1;
      break;
    case FaceDir::kNegY:  // down:  u = +X, v = -Z
      u[0] = 1;
      u[1] = 0;
      u[2] = 0;
      v[0] = 0;
      v[1] = 0;
      v[2] = -1;
      break;
    case FaceDir::kPosZ:  // south: u = +X, v = -Y
      u[0] = 1;
      u[1] = 0;
      u[2] = 0;
      v[0] = 0;
      v[1] = -1;
      v[2] = 0;
      break;
    case FaceDir::kNegZ:  // north: u = -X, v = -Y
    default:
      u[0] = -1;
      u[1] = 0;
      u[2] = 0;
      v[0] = 0;
      v[1] = -1;
      v[2] = 0;
      break;
  }
}

namespace {

// Shared rect-to-corner projection: normalizes each corner's position
// over the face's extent on the image axes and maps it linearly into
// the [u0, v0, u1, v1] rect. A degenerate in-plane extent
// (zero-width element) collapses to the rect's image-min edge rather
// than dividing by zero.
std::array<std::array<float, 2>, 4> UvRectOnAxes(
    const float u[3],
    const float v[3],
    const std::array<std::array<float, 3>, 4>& corners,
    const std::array<float, 4>& rect) noexcept {
  float su[4], sv[4];
  for (int i = 0; i < 4; ++i) {
    su[i] = corners[i][0] * u[0] + corners[i][1] * u[1] + corners[i][2] * u[2];
    sv[i] = corners[i][0] * v[0] + corners[i][1] * v[1] + corners[i][2] * v[2];
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
  std::array<std::array<float, 2>, 4> uvs{};
  for (int i = 0; i < 4; ++i) {
    uvs[i][0] = du <= 1e-6f
                    ? rect[0]
                    : rect[0] + (su[i] - su_min) * (rect[2] - rect[0]) / du;
    uvs[i][1] = dv <= 1e-6f
                    ? rect[1]
                    : rect[1] + (sv[i] - sv_min) * (rect[3] - rect[1]) / dv;
  }
  return uvs;
}

}  // namespace

std::array<std::array<float, 2>, 4> CubeFaceUvRect(
    FaceDir dir,
    const std::array<std::array<float, 3>, 4>& corners,
    const std::array<float, 4>& rect) noexcept {
  float u[3], v[3];
  ModelFaceAxes(dir, u, v);
  return UvRectOnAxes(u, v, corners, rect);
}

std::array<std::array<float, 2>, 4> FluidCubeFaceUvRect(
    FaceDir dir,
    const std::array<std::array<float, 3>, 4>& corners,
    const std::array<float, 4>& rect) noexcept {
  float u[3], v[3];
  FluidFaceAxes(dir, u, v);
  return UvRectOnAxes(u, v, corners, rect);
}

std::array<std::array<float, 2>, 4> EntityBoxFaceUvRect(
    FaceDir dir,
    const std::array<std::array<float, 3>, 4>& corners,
    const std::array<float, 4>& rect) noexcept {
  float u[3], v[3];
  EntityFaceAxes(dir, u, v);
  return UvRectOnAxes(u, v, corners, rect);
}

}  // namespace fschema::geometry::internal
