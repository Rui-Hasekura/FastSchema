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

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"

namespace {

using fschema::filters::LocalPos;
using fschema::geometry::BlockProps;
using fschema::geometry::BlockRef;
using fschema::geometry::RegionVolume;
using fschema::geometry::SchematicVolume;
using fschema::ir::BlockState;
using fschema::ir::BoundingBox;
using fschema::ir::PropertyEncoding;
using fschema::ir::Region;
using fschema::memory::Arena;
using fschema::memory::UnInitBuffer;

// Synthetic 2x2x2 Region. Palette = [air, stone, dirt].
// block_indices (YZX layout):
//   idx 0..3 = y=0 layer (4 stones)
//   idx 4..6 = y=1 first 3 = stones
//   idx 7   = y=1, z=1, x=1 = dirt
struct SyntheticRegion {
  std::unique_ptr<Arena> arena = std::make_unique<Arena>();
  Region region{};

  SyntheticRegion() {
    region.bounds = BoundingBox{/*.origin=*/{0, 0, 0},
                                /*.size=*/{2, 2, 2}};
    region.palette = {
        BlockState{.name = "minecraft:air",
                   .prop_encoding = PropertyEncoding::kNone},
        BlockState{.name = "minecraft:stone",
                   .prop_encoding = PropertyEncoding::kNone},
        BlockState{.name = "minecraft:dirt",
                   .prop_encoding = PropertyEncoding::kNone},
    };
    region.is_air_lut.assign(3, 0);
    region.is_air_lut[0] = 1;

    constexpr std::uint16_t kData[8] = {
        1,
        1,
        1,
        1,  // y=0 layer
        1,
        1,
        1,
        2,  // y=1 layer (last block is dirt)
    };
    region.block_indices = UnInitBuffer<std::uint16_t>(8, arena.get());
    std::memcpy(region.block_indices.data(), kData, sizeof(kData));
    region.is_materialized = true;
    region.lazy_source.encoding = fschema::ir::BlockDataEncoding::kNone;
  }
};

TEST(RegionVolumeTest, BoundsAreRegionBounds) {
  SyntheticRegion sr;
  RegionVolume vol(sr.region, *sr.arena);
  ASSERT_TRUE(vol.EnsureReady().has_value());
  const auto& b = vol.Bounds();
  EXPECT_EQ(b.size[0], 2);
  EXPECT_EQ(b.size[1], 2);
  EXPECT_EQ(b.size[2], 2);
}

TEST(RegionVolumeTest, BlockAtReturnsCorrectEntries) {
  SyntheticRegion sr;
  RegionVolume vol(sr.region, *sr.arena);
  ASSERT_TRUE(vol.EnsureReady().has_value());

  // idx = y*(sx*sz) + z*sx + x = y*4 + z*2 + x
  // kData = [1,1,1,1, 1,1,1,2]:
  //   (0,0,0)=stone, (1,0,0)=stone, (0,0,1)=stone, (1,0,1)=stone
  //   (0,1,0)=stone, (1,1,0)=stone, (0,1,1)=stone, (1,1,1)=dirt
  auto s = vol.BlockAt(LocalPos{0, 0, 0});
  ASSERT_TRUE(s.has_value());
  ASSERT_FALSE(s->is_air);
  EXPECT_EQ(s->name, "minecraft:stone");

  auto d = vol.BlockAt(LocalPos{1, 1, 1});
  ASSERT_TRUE(d.has_value());
  ASSERT_FALSE(d->is_air);
  EXPECT_EQ(d->name, "minecraft:dirt");
}

TEST(RegionVolumeTest, BlockAtReturnsNulloptForOutOfBounds) {
  SyntheticRegion sr;
  RegionVolume vol(sr.region, *sr.arena);
  ASSERT_TRUE(vol.EnsureReady().has_value());

  EXPECT_FALSE(vol.BlockAt(LocalPos{-1, 0, 0}).has_value());
  EXPECT_FALSE(vol.BlockAt(LocalPos{0, -1, 0}).has_value());
  EXPECT_FALSE(vol.BlockAt(LocalPos{2, 0, 0}).has_value());
  EXPECT_FALSE(vol.BlockAt(LocalPos{0, 2, 0}).has_value());
  EXPECT_FALSE(vol.BlockAt(LocalPos{0, 0, 2}).has_value());
}

TEST(RegionVolumeTest, ForEachSkipsAirByDefault) {
  SyntheticRegion sr;
  auto* data = reinterpret_cast<std::uint16_t*>(sr.region.block_indices.data());
  data[0] = 0;  // (0,0,0) = air

  RegionVolume vol(sr.region, *sr.arena);
  ASSERT_TRUE(vol.EnsureReady().has_value());

  std::vector<std::string> visited;
  vol.ForEach([&](LocalPos, BlockRef ref) {
    visited.push_back(std::string(ref.name));
  });
  EXPECT_EQ(visited.size(), 7u);  // 8 - 1 air
}

TEST(RegionVolumeTest, ForEachCanIncludeAir) {
  SyntheticRegion sr;
  auto* data = reinterpret_cast<std::uint16_t*>(sr.region.block_indices.data());
  data[0] = 0;

  RegionVolume vol(sr.region, *sr.arena);
  ASSERT_TRUE(vol.EnsureReady().has_value());

  std::size_t air_count = 0, total = 0;
  vol.ForEach(
      [&](LocalPos, BlockRef ref) {
        ++total;
        if (ref.is_air) ++air_count;
      },
      /*include_air=*/true);
  EXPECT_EQ(total, 8u);
  EXPECT_EQ(air_count, 1u);
}

TEST(RegionVolumeTest, EnsureReadyIsIdempotent) {
  SyntheticRegion sr;
  RegionVolume vol(sr.region, *sr.arena);
  auto r1 = vol.EnsureReady();
  auto r2 = vol.EnsureReady();
  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());
}

TEST(RegionVolumeTest, MoveConstructPreservesState) {
  SyntheticRegion sr;
  RegionVolume v1(sr.region, *sr.arena);
  ASSERT_TRUE(v1.EnsureReady().has_value());

  // Read a block before moving to populate props_.
  auto before = v1.BlockAt(LocalPos{0, 0, 0});
  ASSERT_TRUE(before.has_value());
  ASSERT_FALSE(before->is_air);

  RegionVolume v2(std::move(v1));
  auto after = v2.BlockAt(LocalPos{0, 0, 0});
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(after->name, before->name);
  EXPECT_EQ(after->is_air, before->is_air);

  // Moved-from volume should still be usable (lazy re-init on EnsureReady).
  auto r = v1.EnsureReady();
  EXPECT_TRUE(r.has_value());
}

TEST(RegionVolumeTest, MoveConstructIsAllocationFree) {
  // After move-construct, the source's props_owner_ is null but the
  // destination owns the transferred data. This is a compile-time
  // property (noexcept); we just verify the noexcept contract holds.
  SyntheticRegion sr;
  RegionVolume v1(sr.region, *sr.arena);
  static_assert(
      std::is_nothrow_constructible_v<RegionVolume, RegionVolume&&>,
      "move-construct must be noexcept; if it becomes throwing, "
      "RegionVolume can no longer be placed in noexcept TBB contexts");
  (void)v1;
}

// SchematicVolume: world-space aggregator over a schematic's regions.

// Builds a synthetic schematic: regions of one solid block type each,
// placed at world origins (the pattern mirrors the region fixtures of
// tessellate_test.cc's schematic tests).
struct SchematicBuilder {
  std::unique_ptr<Arena> arena = std::make_unique<Arena>();
  fschema::ir::Schematic schematic{};

  SchematicBuilder() {
    schematic.source_format = fschema::ir::SourceFormat::kLitematica;
    schematic.source_version = 6;
    schematic.data_version = 3105;
  }

  // Adds a region filled with one block (palette [air, name]) at a
  // world origin. `name` must outlive the builder (test literals do).
  fschema::ir::Region& AddRegion(const char* name,
                                 std::array<std::int32_t, 3> origin,
                                 std::array<std::int32_t, 3> size) {
    schematic.regions.resize(schematic.regions.size() + 1);
    fschema::ir::Region& region = schematic.regions.back();
    region.bounds = BoundingBox{/*.origin=*/{origin[0], origin[1], origin[2]},
                                /*.size=*/{size[0], size[1], size[2]}};
    region.palette = {
        BlockState{.name = "minecraft:air",
                   .prop_encoding = PropertyEncoding::kNone},
        BlockState{.name = name,
                   .prop_encoding = PropertyEncoding::kNone},
    };
    region.is_air_lut = {1, 0};
    const std::size_t n = static_cast<std::size_t>(size[0]) *
                          static_cast<std::size_t>(size[1]) *
                          static_cast<std::size_t>(size[2]);
    region.block_indices = UnInitBuffer<std::uint16_t>(n, arena.get());
    std::uint16_t* data =
        reinterpret_cast<std::uint16_t*>(region.block_indices.data());
    for (std::size_t i = 0; i < n; ++i) data[i] = 1;
    region.is_materialized = true;
    region.lazy_source.encoding = fschema::ir::BlockDataEncoding::kNone;
    return region;
  }

  fschema::ir::Schematic& Build() {
    schematic.arena = std::move(arena);
    return schematic;
  }
};

TEST(SchematicVolumeTest, RoutesWorldPositionsToOwningRegion) {
  SchematicBuilder sb;
  sb.AddRegion("minecraft:stone", {0, 0, 0}, {2, 1, 1});
  sb.AddRegion("minecraft:dirt", {5, 0, 0}, {2, 1, 1});
  auto& schematic = sb.Build();

  SchematicVolume vol(schematic, *schematic.arena);
  ASSERT_TRUE(vol.EnsureReady().has_value());

  // Union box: [0, 7) x [0, 1) x [0, 1).
  EXPECT_EQ(vol.Bounds().origin[0], 0);
  EXPECT_EQ(vol.Bounds().origin[1], 0);
  EXPECT_EQ(vol.Bounds().origin[2], 0);
  EXPECT_EQ(vol.Bounds().size[0], 7);
  EXPECT_EQ(vol.Bounds().size[1], 1);
  EXPECT_EQ(vol.Bounds().size[2], 1);

  auto stone = vol.BlockAt(LocalPos{0, 0, 0});
  ASSERT_TRUE(stone.has_value());
  ASSERT_FALSE(stone->is_air);
  EXPECT_EQ(stone->name, "minecraft:stone");

  auto dirt = vol.BlockAt(LocalPos{5, 0, 0});
  ASSERT_TRUE(dirt.has_value());
  EXPECT_EQ(dirt->name, "minecraft:dirt");

  // The gap between the regions and the world edges are nullopt.
  EXPECT_FALSE(vol.BlockAt(LocalPos{3, 0, 0}).has_value());
  EXPECT_FALSE(vol.BlockAt(LocalPos{7, 0, 0}).has_value());
  EXPECT_FALSE(vol.BlockAt(LocalPos{-1, 0, 0}).has_value());
  EXPECT_FALSE(vol.BlockAt(LocalPos{0, 1, 0}).has_value());
}

TEST(SchematicVolumeTest, NegativeOriginsRouteCorrectly) {
  SchematicBuilder sb;
  sb.AddRegion("minecraft:stone", {-2, 0, -3}, {1, 1, 1});
  auto& schematic = sb.Build();

  SchematicVolume vol(schematic, *schematic.arena);
  ASSERT_TRUE(vol.EnsureReady().has_value());

  EXPECT_EQ(vol.Bounds().origin[0], -2);
  EXPECT_EQ(vol.Bounds().origin[2], -3);
  EXPECT_EQ(vol.Bounds().size[0], 1);

  auto hit = vol.BlockAt(LocalPos{-2, 0, -3});
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->name, "minecraft:stone");
  EXPECT_FALSE(vol.BlockAt(LocalPos{-3, 0, -3}).has_value());
  EXPECT_FALSE(vol.BlockAt(LocalPos{-1, 0, -3}).has_value());
}

TEST(SchematicVolumeTest, OverlappingRegionsFirstWins) {
  SchematicBuilder sb;
  sb.AddRegion("minecraft:stone", {0, 0, 0}, {2, 1, 1});  // [0, 2)
  sb.AddRegion("minecraft:dirt", {1, 0, 0}, {2, 1, 1});   // [1, 3)
  auto& schematic = sb.Build();

  SchematicVolume vol(schematic, *schematic.arena);
  ASSERT_TRUE(vol.EnsureReady().has_value());

  // The overlap cell x=1 belongs to the FIRST region in schematic
  // order (documented policy).
  auto overlap = vol.BlockAt(LocalPos{1, 0, 0});
  ASSERT_TRUE(overlap.has_value());
  EXPECT_EQ(overlap->name, "minecraft:stone");

  EXPECT_EQ(vol.BlockAt(LocalPos{0, 0, 0})->name, "minecraft:stone");
  EXPECT_EQ(vol.BlockAt(LocalPos{2, 0, 0})->name, "minecraft:dirt");
}

TEST(SchematicVolumeTest, EmptySchematicAndAirCells) {
  SchematicBuilder sb;
  auto& region = sb.AddRegion("minecraft:stone", {0, 0, 0}, {2, 1, 1});
  reinterpret_cast<std::uint16_t*>(region.block_indices.data())[0] = 0;
  auto& schematic = sb.Build();

  SchematicVolume vol(schematic, *schematic.arena);
  ASSERT_TRUE(vol.EnsureReady().has_value());

  auto air = vol.BlockAt(LocalPos{0, 0, 0});
  ASSERT_TRUE(air.has_value());
  EXPECT_TRUE(air->is_air);

  auto stone = vol.BlockAt(LocalPos{1, 0, 0});
  ASSERT_TRUE(stone.has_value());
  EXPECT_FALSE(stone->is_air);
  EXPECT_EQ(stone->name, "minecraft:stone");

  // A schematic with no regions: all-zero bounds, no blocks, and
  // EnsureReady stays green.
  SchematicBuilder empty;
  auto& empty_schematic = empty.Build();
  SchematicVolume empty_vol(empty_schematic, *empty_schematic.arena);
  ASSERT_TRUE(empty_vol.EnsureReady().has_value());
  EXPECT_EQ(empty_vol.Bounds().size[0], 0);
  EXPECT_EQ(empty_vol.Bounds().size[1], 0);
  EXPECT_EQ(empty_vol.Bounds().size[2], 0);
  EXPECT_FALSE(empty_vol.BlockAt(LocalPos{0, 0, 0}).has_value());
}

}  // namespace
