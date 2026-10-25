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

#include "fschema/geometry/internal/cull.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "fschema/geometry/tests/geometry_test_util.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"

namespace {

using fschema::geometry::internal::BlockGroupTable;
using fschema::geometry::internal::ComputeNegXNeighborGroupBatch;
using fschema::geometry::internal::ComputePosXNeighborGroupBatch;
using fschema::geometry::internal::NeighborCulls;
using fschema::geometry::internal::kNonCullingGroupBase;
using fschema::ir::BlockState;
using fschema::ir::BoundingBox;
using fschema::ir::PropertyEncoding;
using fschema::ir::Region;
using fschema::memory::Arena;
using fschema::memory::UnInitBuffer;
using fschema::tests::BuildGroupTable;

// Scalar reference for the +X neighbor group: block at base+i has its
// +X neighbor at base+i+1 (0 when out of bounds).
std::uint8_t ScalarPosXGroup(const BlockGroupTable& t,
                             std::size_t base,
                             std::size_t i) {
  const std::size_t nb = base + i + 1;
  if (nb >= t.bytes.size()) return 0;  // out of bounds
  return t.bytes[nb];
}

std::uint8_t ScalarNegXGroup(const BlockGroupTable& t,
                             std::size_t base,
                             std::size_t i) {
  if (base + i == 0) return 0;  // -X of first block is out of bounds
  const std::size_t nb = base + i - 1;
  return t.bytes[nb];
}

TEST(CullKernelTest, AllOpaqueRowPosXAllOpaque) {
  // 16 blocks, all opaque (group 1). Every +X neighbor is opaque too.
  const std::vector<std::uint8_t> groups(17, 1);
  const BlockGroupTable t = BuildGroupTable(groups);

  std::vector<std::uint8_t> out(16);
  ComputePosXNeighborGroupBatch(t, 0, 16, 1000, out.data());
  for (std::size_t i = 0; i < 16; ++i) {
    EXPECT_EQ(out[i], 1) << "i=" << i << " should be opaque";
  }
}

TEST(CullKernelTest, AllAirRowPosXAllAir) {
  const std::vector<std::uint8_t> groups(17, 0);  // all air
  const BlockGroupTable t = BuildGroupTable(groups);

  std::vector<std::uint8_t> out(16);
  ComputePosXNeighborGroupBatch(t, 0, 16, 1000, out.data());
  for (std::size_t i = 0; i < 16; ++i) {
    EXPECT_EQ(out[i], 0) << "i=" << i << " should be air";
  }
}

TEST(CullKernelTest, TransparentGroupsPassThrough) {
  // Neighbor groups 0..5 in sequence; the batch must copy them verbatim.
  const std::vector<std::uint8_t> groups = {0, 1, 2, 3, 4, 5, 2};
  const BlockGroupTable t = BuildGroupTable(groups);

  std::vector<std::uint8_t> out(6);
  ComputePosXNeighborGroupBatch(t, 0, 6, 1000, out.data());
  for (std::size_t i = 0; i < 6; ++i) {
    EXPECT_EQ(out[i], groups[i + 1]) << "group passthrough mismatch at " << i;
  }
}

TEST(CullKernelTest, AlternatingPatternMatchesScalar) {
  // 32 blocks alternating air (0) / opaque (1).
  std::vector<std::uint8_t> groups(33, 0);
  for (std::size_t i = 0; i < groups.size(); ++i) {
    groups[i] = (i % 2 == 0) ? 0 : 1;  // even=air, odd=opaque
  }
  const BlockGroupTable t = BuildGroupTable(groups);

  std::vector<std::uint8_t> out(32);
  ComputePosXNeighborGroupBatch(t, 0, 32, 1000, out.data());

  for (std::size_t i = 0; i < 32; ++i) {
    EXPECT_EQ(out[i], ScalarPosXGroup(t, 0, i))
        << "+X mismatch at i=" << i;
  }
}

TEST(CullKernelTest, NegXMatchesScalarForAlternating) {
  std::vector<std::uint8_t> groups(33, 0);
  for (std::size_t i = 0; i < groups.size(); ++i) {
    groups[i] = (i % 2 == 0) ? 0 : 1;
  }
  const BlockGroupTable t = BuildGroupTable(groups);

  std::vector<std::uint8_t> out(32);
  ComputeNegXNeighborGroupBatch(
      t, 1, 32, 1000, out.data());  // base=1 keeps -X neighbor in bounds

  for (std::size_t i = 0; i < 32; ++i) {
    EXPECT_EQ(out[i], ScalarNegXGroup(t, 1, i))
        << "-X mismatch at i=" << i;
  }
}

TEST(CullKernelTest, FirstBlockNegXIsOutOfBounds) {
  // -X neighbor of block at base=0 is out of bounds -> group 0.
  const std::vector<std::uint8_t> groups(16, 1);  // all opaque
  const BlockGroupTable t = BuildGroupTable(groups);

  std::vector<std::uint8_t> out(16);
  ComputeNegXNeighborGroupBatch(t, 0, 16, 1000, out.data());

  EXPECT_EQ(out[0], 0) << "-X of first block should be out of bounds";
  // The rest: -X neighbor of block at i is at i-1, which is in bounds.
  for (std::size_t i = 1; i < 16; ++i) {
    EXPECT_EQ(out[i], 1) << "-X of block " << i << " should be opaque";
  }
}

TEST(CullKernelTest, LastBlockPosXIsOutOfBounds) {
  // +X neighbor of the last block is out of bounds -> group 0.
  const std::vector<std::uint8_t> groups(16, 1);  // 16 blocks, all opaque
  const BlockGroupTable t = BuildGroupTable(groups);

  std::vector<std::uint8_t> out(16);
  ComputePosXNeighborGroupBatch(t, 0, 16, 1000, out.data());

  // Blocks 0..14 have +X neighbors at indices 1..15 (in bounds).
  for (std::size_t i = 0; i < 15; ++i) {
    EXPECT_EQ(out[i], 1) << "+X of block " << i << " should be opaque";
  }
  // Block 15 has its +X neighbor at index 16 (out of bounds).
  EXPECT_EQ(out[15], 0) << "+X of last block should be out of bounds";
}

TEST(CullKernelTest, SubRowAtNonZeroBaseMatchesScalar) {
  // 64 blocks total, query the middle 16 (base=32).
  std::vector<std::uint8_t> groups(65, 0);
  for (std::size_t i = 0; i < groups.size(); ++i) {
    groups[i] = (i % 3 == 0) ? 0 : ((i % 3 == 1) ? 1 : 2);
  }
  const BlockGroupTable t = BuildGroupTable(groups);

  std::vector<std::uint8_t> pos_out(16);
  ComputePosXNeighborGroupBatch(t, 32, 16, 1000, pos_out.data());
  std::vector<std::uint8_t> neg_out(16);
  ComputeNegXNeighborGroupBatch(t, 32, 16, 1000, neg_out.data());

  for (std::size_t i = 0; i < 16; ++i) {
    EXPECT_EQ(pos_out[i], ScalarPosXGroup(t, 32, i))
        << "+X mismatch at base=32, i=" << i;
    EXPECT_EQ(neg_out[i], ScalarNegXGroup(t, 32, i))
        << "-X mismatch at base=32, i=" << i;
  }
}

TEST(CullKernelTest, NonPowerOfTwoCountMatchesScalar) {
  // 17 blocks (non-multiple of typical SIMD lane width 16). The
  // kernel's tail handling must produce the right results.
  std::vector<std::uint8_t> groups(18, 0);
  for (std::size_t i = 0; i < groups.size(); ++i) {
    groups[i] = (i % 5 == 0) ? 0 : 1;
  }
  const BlockGroupTable t = BuildGroupTable(groups);

  std::vector<std::uint8_t> out(17);
  ComputePosXNeighborGroupBatch(t, 0, 17, 1000, out.data());

  for (std::size_t i = 0; i < 17; ++i) {
    EXPECT_EQ(out[i], ScalarPosXGroup(t, 0, i))
        << "+X mismatch (tail) at i=" << i;
  }
}

TEST(CullKernelTest, NeighborCullsFollowsGroupSemantics) {
  // Air and out-of-bounds neighbors hide nothing.
  EXPECT_FALSE(NeighborCulls(0, 1));
  EXPECT_FALSE(NeighborCulls(0, 2));
  EXPECT_FALSE(NeighborCulls(0, 3));
  // Opaque neighbors always hide the face.
  EXPECT_TRUE(NeighborCulls(1, 1));
  EXPECT_TRUE(NeighborCulls(1, 2));
  EXPECT_TRUE(NeighborCulls(1, 3));
  // Transparent neighbors hide only same-group faces.
  EXPECT_TRUE(NeighborCulls(2, 2));
  EXPECT_TRUE(NeighborCulls(3, 3));
  EXPECT_FALSE(NeighborCulls(2, 3));
  EXPECT_FALSE(NeighborCulls(3, 2));
  EXPECT_FALSE(NeighborCulls(2, 1));  // opaque block behind glass
}

TEST(CullKernelTest, BlockGroupTableBuildFromRegionAndPalette) {
  // Palette [air, stone, glass, water] with groups [0, 1, 2, 3] over
  // indices air, stone, glass, water, air.
  auto arena = std::make_unique<Arena>();
  Region region{};
  region.bounds = BoundingBox{/*.origin=*/{0, 0, 0}, /*.size=*/{5, 1, 1}};
  region.palette = {
      BlockState{.name = "minecraft:air",
                 .prop_encoding = PropertyEncoding::kNone},
      BlockState{.name = "minecraft:stone",
                 .prop_encoding = PropertyEncoding::kNone},
      BlockState{.name = "minecraft:glass",
                 .prop_encoding = PropertyEncoding::kNone},
      BlockState{.name = "minecraft:water",
                 .prop_encoding = PropertyEncoding::kNone},
  };
  region.is_air_lut.assign(4, 0);
  region.is_air_lut[0] = 1;  // air
  constexpr std::uint16_t kIndices[5] = {0, 1, 2, 3, 0};
  region.block_indices = UnInitBuffer<std::uint16_t>(5, arena.get());
  std::memcpy(region.block_indices.data(), kIndices, sizeof(kIndices));
  region.is_materialized = true;
  region.lazy_source.encoding = fschema::ir::BlockDataEncoding::kNone;

  const std::vector<std::uint8_t> palette_groups = {0, 1, 2, 3};
  BlockGroupTable t;
  t.BuildFrom(region, palette_groups);
  ASSERT_EQ(t.bytes.size(), 5u);
  EXPECT_EQ(t.bytes[0], 0);  // air
  EXPECT_EQ(t.bytes[1], 1);  // stone: opaque
  EXPECT_EQ(t.bytes[2], 2);  // glass: transparent group 2
  EXPECT_EQ(t.bytes[3], 3);  // water: transparent group 3
  EXPECT_EQ(t.bytes[4], 0);  // air

  // Accessors: opaque is exactly group 1, air exactly group 0.
  EXPECT_FALSE(t.IsOpaque(0));
  EXPECT_TRUE(t.IsAir(0));
  EXPECT_TRUE(t.IsOpaque(1));
  EXPECT_FALSE(t.IsAir(1));
  EXPECT_FALSE(t.IsOpaque(2));  // glass is not opaque
  EXPECT_FALSE(t.IsAir(2));
}

TEST(CullKernelTest, NonCullingGroupsNeverCull) {
  // Groups >= kNonCullingGroupBase (the leaves family) hide nothing:
  // not other blocks' faces, and not even same-group neighbors
  // (fancy-graphics leaves keep their internal faces, unlike glass).
  // Opaque (1) still hides everything, and the self-culling groups
  // (2..127) keep their same-name-only behavior.
  const std::uint8_t kLeaves = kNonCullingGroupBase;
  EXPECT_FALSE(NeighborCulls(kLeaves, kLeaves));  // leaves vs leaves
  EXPECT_FALSE(NeighborCulls(kLeaves, 2));        // leaves vs water
  EXPECT_FALSE(NeighborCulls(2, kLeaves));        // water face vs leaves
  EXPECT_FALSE(NeighborCulls(kLeaves, 0));        // air neighbor
  EXPECT_TRUE(NeighborCulls(1, kLeaves));         // opaque still hides
  EXPECT_TRUE(NeighborCulls(1, 2));
  EXPECT_TRUE(NeighborCulls(2, 2));               // same group culls
  EXPECT_FALSE(NeighborCulls(3, 2));              // different groups
  // The boundary: 127 self-culls, 128 does not.
  EXPECT_TRUE(NeighborCulls(127, 127));
  EXPECT_FALSE(NeighborCulls(128, 128));
}

}  // namespace
