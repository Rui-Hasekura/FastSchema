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

#ifndef FSCHEMA_GEOMETRY_EXPORT_H_
#define FSCHEMA_GEOMETRY_EXPORT_H_

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/geometry/tint.h"
#include "fschema/geometry/types.h"

namespace fschema::geometry {

// Opaque byte buffer for serialized exports.
struct ByteBuffer {
  std::vector<std::uint8_t> bytes;
};

// Serialize a TessellationResult to a single .glb file (binary glTF 2.0).
// The returned ByteBuffer owns all bytes; write it to disk verbatim.
// `tint` is applied to the vertex COLOR_0 RGB channels; the default
// TintTable is identity.
[[nodiscard]] ParseResult<ByteBuffer> ExportGlb(
    const TessellationResult& result,
    const TintTable& tint = TintTable{});

// Serialize to Wavefront OBJ text plus a companion MTL, returned as
// {obj, mtl}. Vertices share one index across position/UV/normal, so
// faces use `f i/i/i` (OBJ is 1-indexed). The MTL references the atlas
// as `<name>_atlas.png`; the caller writes the atlas PNG separately.
[[nodiscard]] ParseResult<std::pair<ByteBuffer, ByteBuffer>> ExportObj(
    const TessellationResult& result,
    std::string_view name);

// Raw structure-of-arrays dump: each ByteBuffer holds the opaque mesh
// followed by the transparent mesh for that attribute.
struct RawMesh {
  ByteBuffer positions, normals, uvs, indices;
  std::uint32_t vertex_count = 0;
  std::uint32_t index_count = 0;
};
[[nodiscard]] RawMesh ExportRaw(const TessellationResult& result);

}  // namespace fschema::geometry

#endif  // FSCHEMA_GEOMETRY_EXPORT_H_
