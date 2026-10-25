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

#ifndef FSCHEMA_GEOMETRY_TYPES_H_
#define FSCHEMA_GEOMETRY_TYPES_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fschema/geometry/block_props.h"

namespace fschema::geometry {

// Read-only view of a block: name + decoded props + air flag.
struct BlockRef {
  std::string_view name;
  const BlockProps* props = nullptr;
  bool is_air = false;
};

// A single textured face produced by the resolver. Block-local coordinates.
struct ModelFace {
  std::array<std::array<float, 3>, 4> corners{};
  std::array<std::array<float, 2>, 4> uvs{};
  // bit 0..2: cull_face (0=none, 1..6=axis)
  // bit 3..5: tint_slot (0=none, 1=grass, 2=foliage, 3=water, 4=redstone)
  // bit 6:    is_transparent
  // bit 7:    is_fluid_face (synthesized water/lava cuboid)
  std::uint8_t flags = 0;

  // Texture path (e.g. "minecraft:block/stone") used to look up the atlas
  // slot. Empty when the model JSON failed to resolve a texture variable;
  // such faces are skipped at emit time.
  std::string texture_path;

  [[nodiscard]] std::uint8_t CullFace() const noexcept { return flags & 0x07; }
  [[nodiscard]] std::uint8_t TintSlot() const noexcept {
    return (flags >> 3) & 0x07;
  }
  [[nodiscard]] bool IsTransparent() const noexcept {
    return (flags & 0x40) != 0;
  }
  void SetCullFace(std::uint8_t v) noexcept {
    flags = (flags & ~0x07) | (v & 0x07);
  }
  void SetTintSlot(std::uint8_t v) noexcept {
    flags = (flags & ~0x38) | ((v & 0x07) << 3);
  }
  void SetTransparent(bool b) noexcept {
    flags = b ? (flags | 0x40) : (flags & ~0x40);
  }
  [[nodiscard]] bool IsFluidFace() const noexcept {
    return (flags & 0x80) != 0;
  }
  void SetFluidFace(bool b) noexcept {
    flags = b ? (flags | 0x80) : (flags & ~0x80);
  }
};

struct ResolvedModel {
  std::span<const ModelFace> faces;
  bool is_transparent = false;
};

// Decoded PNG texture: row-major RGBA8 pixels, top-left origin. The
// `rgba` span points into a cache entry owned by the originating
// ResourcePack and stays valid for the pack's lifetime.
struct DecodedTexture {
  std::span<const std::uint8_t> rgba;  // 4 bytes per pixel
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

// Triangle mesh data, structure-of-arrays layout.
struct Mesh {
  std::vector<float> positions;        // 3/vertex
  std::vector<float> normals;          // 3/vertex
  std::vector<float> uvs;              // 2/vertex
  std::vector<std::uint8_t> ao;        // 1/vertex, 0..255
  std::vector<std::uint8_t> tints;     // 1/vertex
  std::vector<std::uint32_t> indices;  // triangle list

  [[nodiscard]] std::size_t VertexCount() const noexcept {
    return positions.size() / 3;
  }
  [[nodiscard]] std::size_t TriangleCount() const noexcept {
    return indices.size() / 3;
  }
  void Reserve(std::size_t vertex_capacity, std::size_t index_capacity) {
    positions.reserve(vertex_capacity * 3);
    normals.reserve(vertex_capacity * 3);
    uvs.reserve(vertex_capacity * 2);
    ao.reserve(vertex_capacity);
    tints.reserve(vertex_capacity);
    indices.reserve(index_capacity);
  }
};

struct TextureAtlas {
  std::vector<std::uint8_t> rgba;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  struct Slot {
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
  };
  std::vector<Slot> slots;
};

struct TessellationResult {
  Mesh opaque_mesh;
  Mesh transparent_mesh;
  TextureAtlas atlas;

  [[nodiscard]] std::size_t TotalVertices() const noexcept {
    return opaque_mesh.VertexCount() + transparent_mesh.VertexCount();
  }
  [[nodiscard]] std::size_t TotalTriangles() const noexcept {
    return opaque_mesh.TriangleCount() + transparent_mesh.TriangleCount();
  }
  [[nodiscard]] bool HasTransparency() const noexcept {
    return !transparent_mesh.indices.empty();
  }
};

}  // namespace fschema::geometry

#endif  // FSCHEMA_GEOMETRY_TYPES_H_
