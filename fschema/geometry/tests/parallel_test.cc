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

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/geometry/resource_pack.h"
#include "fschema/geometry/tessellate.h"
#include "fschema/geometry/tests/geometry_test_util.h"
#include "fschema/geometry/types.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"

namespace {

using fschema::ToString;
using fschema::geometry::ResourcePack;
using fschema::geometry::TessellateRegion;
using fschema::geometry::TessellationOptions;
using fschema::geometry::TessellationResult;
using fschema::tests::ResourcePackFixture;
using fschema::tests::StoneRegion;

TessellationOptions ScalarOpts() {
  TessellationOptions o;
  o.chunk_edge = 0;
  return o;
}

TEST(ParallelTest, ChunkEdge0AndChunkEdge4MatchFor4x4x4Cube) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(4, 4, 4);  // 64 blocks; with chunk_edge=4, 1 chunk only

  auto scalar = TessellateRegion(sr.region, *sr.arena, **pack, ScalarOpts());
  ASSERT_TRUE(scalar.has_value()) << ToString(scalar.error());

  TessellationOptions parallel_opts;
  parallel_opts.chunk_edge = 2;  // 2x2x2 = 8 chunks
  auto parallel = TessellateRegion(sr.region, *sr.arena, **pack, parallel_opts);
  ASSERT_TRUE(parallel.has_value()) << ToString(parallel.error());

  EXPECT_EQ(scalar->opaque_mesh.VertexCount(),
            parallel->opaque_mesh.VertexCount());
  EXPECT_EQ(scalar->opaque_mesh.TriangleCount(),
            parallel->opaque_mesh.TriangleCount());
  EXPECT_EQ(scalar->transparent_mesh.VertexCount(),
            parallel->transparent_mesh.VertexCount());
  EXPECT_EQ(scalar->atlas.slots.size(), parallel->atlas.slots.size());
}

TEST(ParallelTest, ChunkEdge1MaxParallelismMatchesScalar) {
  // chunk_edge=1 = each block is its own chunk = max parallelism.
  // For a 4x4x4 cube, that's 64 chunks dispatched to TBB.
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(4, 4, 4);

  auto scalar = TessellateRegion(sr.region, *sr.arena, **pack, ScalarOpts());
  ASSERT_TRUE(scalar.has_value());

  TessellationOptions parallel_opts;
  parallel_opts.chunk_edge = 1;
  auto parallel = TessellateRegion(sr.region, *sr.arena, **pack, parallel_opts);
  ASSERT_TRUE(parallel.has_value());

  EXPECT_EQ(scalar->opaque_mesh.VertexCount(),
            parallel->opaque_mesh.VertexCount());
  EXPECT_EQ(scalar->opaque_mesh.TriangleCount(),
            parallel->opaque_mesh.TriangleCount());
}

TEST(ParallelTest, ParallelAndScalarProduceValidIndices) {
  // After AppendMesh with index offset adjustment, every index in the
  // merged Mesh must point to a valid vertex (i.e., < VertexCount).
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(4, 4, 4);
  TessellationOptions parallel_opts;
  parallel_opts.chunk_edge = 2;
  auto r = TessellateRegion(sr.region, *sr.arena, **pack, parallel_opts);
  ASSERT_TRUE(r.has_value());

  const auto vc = r->opaque_mesh.VertexCount();
  for (std::uint32_t idx : r->opaque_mesh.indices) {
    ASSERT_LT(idx, vc) << "index " << idx << " >= vertex count " << vc;
  }
}

TEST(ParallelTest, ParallelUVsAreIn01) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(4, 4, 4);
  TessellationOptions parallel_opts;
  parallel_opts.chunk_edge = 2;
  auto r = TessellateRegion(sr.region, *sr.arena, **pack, parallel_opts);
  ASSERT_TRUE(r.has_value());

  for (std::size_t i = 0; i < r->opaque_mesh.uvs.size(); i += 2) {
    const float u = r->opaque_mesh.uvs[i];
    const float v = r->opaque_mesh.uvs[i + 1];
    EXPECT_GE(u, 0.0f);
    EXPECT_LE(u, 1.0f);
    EXPECT_GE(v, 0.0f);
    EXPECT_LE(v, 1.0f);
  }
}

TEST(ParallelTest, ParallelVertexPositionsSpanBlockVolume) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(4, 4, 4);
  TessellationOptions parallel_opts;
  parallel_opts.chunk_edge = 2;
  auto r = TessellateRegion(sr.region, *sr.arena, **pack, parallel_opts);
  ASSERT_TRUE(r.has_value());

  for (std::size_t i = 0; i < r->opaque_mesh.positions.size(); i += 3) {
    const float x = r->opaque_mesh.positions[i + 0];
    const float y = r->opaque_mesh.positions[i + 1];
    const float z = r->opaque_mesh.positions[i + 2];
    EXPECT_GE(x, 0.0f);
    EXPECT_LE(x, 4.0f);
    EXPECT_GE(y, 0.0f);
    EXPECT_LE(y, 4.0f);
    EXPECT_GE(z, 0.0f);
    EXPECT_LE(z, 4.0f);
  }
}

TEST(ParallelTest, SingleChunkFallsBackToScalarPath) {
  // When chunk_edge >= max(bounds.size), there's only one chunk, so
  // use_parallel is false. The result should be byte-identical to
  // running with chunk_edge=0 (scalar).
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(2, 2, 2);

  auto scalar = TessellateRegion(sr.region, *sr.arena, **pack, ScalarOpts());
  ASSERT_TRUE(scalar.has_value());

  TessellationOptions big_chunk_opts;
  big_chunk_opts.chunk_edge = 16;  // bigger than the region
  auto big = TessellateRegion(sr.region, *sr.arena, **pack, big_chunk_opts);
  ASSERT_TRUE(big.has_value());

  // Vertex counts match AND mesh byte layout matches (deterministic
  // because both go through the scalar path).
  EXPECT_EQ(scalar->opaque_mesh.VertexCount(), big->opaque_mesh.VertexCount());
  EXPECT_EQ(scalar->opaque_mesh.TriangleCount(),
            big->opaque_mesh.TriangleCount());
  EXPECT_EQ(scalar->opaque_mesh.positions, big->opaque_mesh.positions);
  EXPECT_EQ(scalar->opaque_mesh.indices, big->opaque_mesh.indices);
}

TEST(ParallelTest, RepeatedParallelRunsAreStableInCounts) {
  // TBB worker scheduling is non-deterministic, but the final merged
  // counts must be stable across runs.
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(4, 4, 4);
  TessellationOptions parallel_opts;
  parallel_opts.chunk_edge = 1;  // max parallelism, most non-determinism

  auto r1 = TessellateRegion(sr.region, *sr.arena, **pack, parallel_opts);
  auto r2 = TessellateRegion(sr.region, *sr.arena, **pack, parallel_opts);
  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());

  EXPECT_EQ(r1->opaque_mesh.VertexCount(), r2->opaque_mesh.VertexCount());
  EXPECT_EQ(r1->opaque_mesh.TriangleCount(), r2->opaque_mesh.TriangleCount());
  EXPECT_EQ(r1->atlas.slots.size(), r2->atlas.slots.size());
}

TEST(ParallelTest, BiggerRegionProducesExpectedFaceCount) {
  // 4x4x4 cube of stone: only the exterior 6 sides are visible.
  // Each side = 4*4 = 16 faces, total = 6*16 = 96 visible faces.
  // Each face = 4 vertices, 2 triangles.
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());

  StoneRegion sr(4, 4, 4);
  TessellationOptions parallel_opts;
  parallel_opts.chunk_edge = 2;
  auto r = TessellateRegion(sr.region, *sr.arena, **pack, parallel_opts);
  ASSERT_TRUE(r.has_value());

  // 96 faces * 4 vertices = 384 vertices
  EXPECT_EQ(r->opaque_mesh.VertexCount(), 384u);
  // 96 faces * 2 triangles * 3 indices = 576 indices / 3 = 192 triangles
  EXPECT_EQ(r->opaque_mesh.TriangleCount(), 192u);
}

TEST(ParallelTest, ConcurrentIsAirAfterEnsureAirLutIsRaceFree) {
  StoneRegion sr(4, 4, 4);

  // Force a stale LUT so EnsureAirLut() has real work to do.
  sr.region.is_air_lut.clear();
  ASSERT_NE(sr.region.is_air_lut.size(), sr.region.palette.size());

  // Serial pre-condition: build the LUT before workers touch IsAir().
  sr.region.EnsureAirLut();
  ASSERT_EQ(sr.region.is_air_lut.size(), sr.region.palette.size());

  // Worker phase: concurrent IsAir(). StoneRegion palette is
  // [air, stone].
  constexpr int kThreads = 8;
  constexpr int kIterationsPerThread = 10'000;
  std::atomic<int> mismatches{0};

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&]() {
      for (int i = 0; i < kIterationsPerThread; ++i) {
        if (!sr.region.IsAir(0)) {
          mismatches.fetch_add(1, std::memory_order_relaxed);
        }
        if (sr.region.IsAir(1)) {
          mismatches.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto& th : threads) th.join();

  EXPECT_EQ(mismatches.load(std::memory_order_relaxed), 0);
}

}  // namespace
