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

#include "fschema/geometry/block_volume.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "fschema/ir/materialize.h"

namespace fschema::geometry {

RegionVolume::RegionVolume(const ir::Region& region, memory::Arena& arena)
    : region_(region), arena_(arena) {}

RegionVolume::RegionVolume(RegionVolume&& other) noexcept
    : region_(other.region_),
      arena_(other.arena_),
      props_owner_(std::move(other.props_owner_)),
      props_(other.props_),
      ready_(other.ready_) {
  // Leave `other` unready; EnsureReadyImpl() rebuilds its caches.
  other.props_ = {};
  other.ready_ = false;
}

RegionVolume::~RegionVolume() = default;

ParseResult<void> RegionVolume::EnsureReadyImpl() const {
  if (!region_.is_materialized) {
    auto r = ir::EnsureMaterialized(region_, arena_);
    if (!r) return std::unexpected(r.error());
  }

  region_.EnsureAirLut();

  if (props_.size() != region_.palette.size()) {
    if (!props_owner_) {
      props_owner_ = std::make_unique<std::vector<BlockProps>>();
    }
    auto& owned = *props_owner_;
    owned.clear();
    owned.reserve(region_.palette.size());
    for (const auto& state : region_.palette) {
      owned.emplace_back(state);
    }
    props_ = std::span<const BlockProps>(owned);
  }

  ready_ = true;
  return {};
}

struct SchematicVolume::Impl {
  // One RegionVolume per region, in schematic order; routing scans
  // them front-to-back, which is what makes first-region-wins fall
  // out of the loop order.
  std::vector<RegionVolume> volumes;
  ir::BoundingBox world_bounds{};
};

SchematicVolume::SchematicVolume(const ir::Schematic& schematic,
                                 memory::Arena& arena) {
  auto impl = std::make_unique<Impl>();
  impl->volumes.reserve(schematic.regions.size());

  // Union box over all regions (empty schematic stays all-zero).
  std::int32_t min_x = 0, min_y = 0, min_z = 0;
  std::int32_t max_x = 0, max_y = 0, max_z = 0;
  bool any = false;
  for (const ir::Region& region : schematic.regions) {
    impl->volumes.emplace_back(region, arena);
    const ir::BoundingBox& b = region.bounds;
    if (!any) {
      min_x = b.origin[0];
      min_y = b.origin[1];
      min_z = b.origin[2];
      max_x = b.origin[0] + b.size[0];
      max_y = b.origin[1] + b.size[1];
      max_z = b.origin[2] + b.size[2];
      any = true;
      continue;
    }
    min_x = std::min(min_x, b.origin[0]);
    min_y = std::min(min_y, b.origin[1]);
    min_z = std::min(min_z, b.origin[2]);
    max_x = std::max(max_x, b.origin[0] + b.size[0]);
    max_y = std::max(max_y, b.origin[1] + b.size[1]);
    max_z = std::max(max_z, b.origin[2] + b.size[2]);
  }
  if (any) {
    impl->world_bounds = ir::BoundingBox{{min_x, min_y, min_z},
                                         {max_x - min_x, max_y - min_y,
                                          max_z - min_z}};
  }
  impl_ = std::move(impl);
}

SchematicVolume::~SchematicVolume() = default;

const ir::BoundingBox& SchematicVolume::Bounds() const noexcept {
  return impl_->world_bounds;
}

std::optional<BlockRef> SchematicVolume::BlockAt(
    filters::LocalPos world_p) const noexcept {
  for (const RegionVolume& vol : impl_->volumes) {
    const ir::BoundingBox& b = vol.Bounds();
    const filters::LocalPos rel{world_p.x - b.origin[0],
                                world_p.y - b.origin[1],
                                world_p.z - b.origin[2]};
    if (rel.x < 0 || rel.y < 0 || rel.z < 0 || rel.x >= b.size[0] ||
        rel.y >= b.size[1] || rel.z >= b.size[2]) {
      continue;  // this region does not own the cell
    }
    return vol.BlockAt(rel);  // first containing region wins
  }
  return std::nullopt;
}

ParseResult<void> SchematicVolume::EnsureReady() const {
  for (const RegionVolume& vol : impl_->volumes) {
    auto r = vol.EnsureReady();
    if (!r) return r;
  }
  return {};
}

}  // namespace fschema::geometry
