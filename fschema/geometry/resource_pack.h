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

#ifndef FSCHEMA_GEOMETRY_RESOURCE_PACK_H_
#define FSCHEMA_GEOMETRY_RESOURCE_PACK_H_

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

#include "fschema/base/error.h"
#include "fschema/geometry/block_props.h"
#include "fschema/geometry/types.h"

namespace fschema::geometry {

// A loaded Minecraft resource pack: blockstates JSON + models JSON +
// textures. Only the directory form (unzipped pack layout) is supported;
// a .zip path fails with kResourcePackNotFound. File contents are read
// lazily and cached. Const methods are thread-safe (mutex-guarded caches),
// so one pack can be shared across parallel Tessellate* runs.
class ResourcePack {
 public:
  ResourcePack(const ResourcePack&) = delete;
  ResourcePack& operator=(const ResourcePack&) = delete;
  ResourcePack(ResourcePack&&) noexcept;
  ResourcePack& operator=(ResourcePack&&) noexcept;
  ~ResourcePack();

  // Loads a pack from an unzipped resource pack directory; `path` must
  // contain assets/minecraft/. Load only validates the layout and records
  // the root; file contents are read on demand and cached.
  [[nodiscard]] static ParseResult<std::unique_ptr<ResourcePack>> Load(
      std::string_view path);

  // Resolves a block state to its model faces: blockstate JSON (variants
  // or multipart) -> model refs -> model JSON parent chain -> elements.
  // Cached per (block_name, props hash); the returned span points into
  // cache storage owned by this pack and is never invalidated.
  //
  // Element rotations (origin/axis/angle/rescale) apply to each
  // element's corners and clear the cull face of rotated faces;
  // variant/multipart quarter turns ("x"/"y"/"z", multiples of 90
  // degrees only) rotate the resolved model about the block center and
  // remap cull faces. Multipart `when` supports equality conditions
  // (pipe alternatives, arrays, nested OR/AND up to depth 8). Fluids
  // (water/lava) resolve to a synthesized cuboid that the tessellator
  // morphs into vanilla's sloped surface (corner averaging).
  // Transparency is classified by block name (vanilla packs declare
  // no render types), with a model-declared "render_type" as an
  // override.
  [[nodiscard]] ParseResult<ResolvedModel> Resolve(
      std::string_view block_name,
      const BlockProps& props) const;

  // Reads and decodes a PNG texture; results are cached per path, and the
  // returned span points into cache storage owned by this pack. Returns
  // kModelMissingTexture for missing or unsafe paths, kTextureDecodeFailed
  // if Wuffs rejects the bytes.
  [[nodiscard]] ParseResult<DecodedTexture> LoadTexturePixels(
      std::string_view texture_path) const;

 private:
  ResourcePack() = default;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fschema::geometry

#endif  // FSCHEMA_GEOMETRY_RESOURCE_PACK_H_
