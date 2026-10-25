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

#ifndef FSCHEMA_GEOMETRY_BLOCK_VOLUME_H_
#define FSCHEMA_GEOMETRY_BLOCK_VOLUME_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/filters/pos.h"
#include "fschema/filters/view.h"
#include "fschema/geometry/block_props.h"
#include "fschema/geometry/types.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"

namespace fschema::geometry {

// Read-only block view over a single ir::Region. EnsureReady()
// materializes the region, builds its air LUT, and caches BlockProps per
// palette slot; BlockAt is O(1) afterward. Does not mutate the region
// (never sets region.edited).
class RegionVolume {
 public:
  RegionVolume(const ir::Region& region, memory::Arena& arena);
  RegionVolume(const RegionVolume&) = delete;
  RegionVolume& operator=(const RegionVolume&) = delete;
  RegionVolume(RegionVolume&& other) noexcept;
  // Reference members cannot be rebound; move-assign is meaningless.
  RegionVolume& operator=(RegionVolume&&) = delete;
  ~RegionVolume();

  [[nodiscard]] const ir::BoundingBox& Bounds() const noexcept {
    return region_.bounds;
  }

  [[nodiscard]] std::optional<BlockRef> BlockAt(
      filters::LocalPos p) const noexcept {
    if (!InBounds(p)) return std::nullopt;
    const auto linear = filters::LinearIndex(p, region_.bounds);
    const auto pal = region_.block_indices[linear];
    if (region_.IsAir(pal)) {
      return BlockRef{/*name=*/{}, /*props=*/nullptr, /*is_air=*/true};
    }
    return BlockRef{region_.palette[pal].name, &props_[pal], false};
  }

  [[nodiscard]] ParseResult<void> EnsureReady() const {
    if (ready_) return {};
    return EnsureReadyImpl();
  }

  template <typename Fn>
  void ForEach(Fn&& fn, bool include_air = false) const {
    const auto& b = region_.bounds;
    for (std::int32_t y = 0; y < b.size[1]; ++y) {
      for (std::int32_t z = 0; z < b.size[2]; ++z) {
        for (std::int32_t x = 0; x < b.size[0]; ++x) {
          filters::LocalPos p{x, y, z};
          const auto linear = filters::LinearIndex(p, b);
          const auto pal = region_.block_indices[linear];
          if (region_.IsAir(pal) && !include_air) continue;
          fn(p, BlockAt(p).value());
        }
      }
    }
  }

 private:
  [[nodiscard]] bool InBounds(filters::LocalPos p) const noexcept {
    const auto& b = region_.bounds;
    return p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x < b.size[0] &&
           p.y < b.size[1] && p.z < b.size[2];
  }
  [[nodiscard]] ParseResult<void> EnsureReadyImpl() const;

  const ir::Region& region_;
  memory::Arena& arena_;
  // Lazily allocated in EnsureReadyImpl() so move-construction stays
  // allocation-free. Mutable for const EnsureReady().
  mutable std::unique_ptr<std::vector<BlockProps>> props_owner_;
  mutable std::span<const BlockProps> props_;
  mutable bool ready_ = false;
};

// World-space read-only view over every region of a schematic.
// EnsureReady() materializes each region (air LUT + per-palette
// BlockProps cache); BlockAt() routes a world position to the owning
// region and reads the block there. When regions overlap, the first
// region in schematic order wins; region data is never merged or
// mutated. Bounds() is the union box; an empty schematic yields an
// all-zero box.
class SchematicVolume {
 public:
  SchematicVolume(const ir::Schematic& schematic, memory::Arena& arena);
  ~SchematicVolume();
  SchematicVolume(const SchematicVolume&) = delete;
  SchematicVolume& operator=(const SchematicVolume&) = delete;

  [[nodiscard]] const ir::BoundingBox& Bounds() const noexcept;
  [[nodiscard]] std::optional<BlockRef> BlockAt(
      filters::LocalPos world_p) const noexcept;
  // Materializes every region; can fail. Call before BlockAt.
  [[nodiscard]] ParseResult<void> EnsureReady() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fschema::geometry

#endif  // FSCHEMA_GEOMETRY_BLOCK_VOLUME_H_
