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

#ifndef FSCHEMA_GEOMETRY_ATLAS_H_
#define FSCHEMA_GEOMETRY_ATLAS_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "fschema/base/error.h"
#include "fschema/geometry/types.h"

namespace fschema::geometry {

class TextureAtlasBuilder {
 public:
  TextureAtlasBuilder(std::uint32_t max_extent, std::uint32_t padding);

  // Inserts an RGBA8 texture and returns its slot index in
  // TextureAtlas::slots. Deduplicates by texture_path; a repeated path
  // returns the existing slot (a later Insert of the same path ignores
  // the new repeat counts). `repeats_x`/`repeats_y` pack that many
  // tiled copies of the texture into the slot rect: the slot then
  // spans repeats_x * repeats_y texture tiles, which lets merged
  // quads address the slot in tile units (uv up to repeats) and
  // still sample the texture. On failure returns kAtlasOverflow and
  // the builder state is unchanged (insertion can continue or
  // Finalize).
  [[nodiscard]] ParseResult<std::uint32_t> Insert(
      std::string_view texture_path,
      std::span<const std::uint8_t> rgba,
      std::uint32_t w,
      std::uint32_t h,
      std::uint32_t repeats_x = 1,
      std::uint32_t repeats_y = 1);

  // Stitches all inserted textures into one RGBA8 canvas. Consumes the
  // builder (rvalue-only). Slot UVs are normalized against the final
  // canvas dimensions.
  [[nodiscard]] TextureAtlas Finalize() &&;

 private:
  // Per-texture record stored during Insert, stitched in Finalize.
  struct PendingEntry {
    std::uint32_t placement_x = 0;
    std::uint32_t placement_y = 0;
    // Source texture size (one tile).
    std::uint32_t src_w = 0;
    std::uint32_t src_h = 0;
    // Slot rect size: src_w * repeats_x by src_h * repeats_y.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t repeats_x = 1;
    std::uint32_t repeats_y = 1;
    std::vector<std::uint8_t> pixels;
  };

  const std::uint32_t max_extent_;
  const std::uint32_t padding_;

  std::uint32_t canvas_width_ = 0;
  std::uint32_t canvas_height_ = 0;
  std::uint32_t current_row_x_ = 0;
  std::uint32_t current_row_y_ = 0;
  std::uint32_t current_row_height_ = 0;

  absl::flat_hash_map<std::string, std::uint32_t> dedup_;
  std::vector<PendingEntry> pending_;
  std::vector<TextureAtlas::Slot> slots_;
};

}  // namespace fschema::geometry

#endif  // FSCHEMA_GEOMETRY_ATLAS_H_
