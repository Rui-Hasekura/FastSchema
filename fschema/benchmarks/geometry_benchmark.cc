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

// Geometry subsystem benchmarks: TessellateRegion variants, ExportGlb
// serialization, and vertex/triangle counters. Standard google/benchmark
// flags apply, e.g. --benchmark_filter=Parallel --benchmark_min_time=2s.

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fschema/base/random.h"
#include "fschema/geometry/export.h"
#include "fschema/geometry/resource_pack.h"
#include "fschema/geometry/tessellate.h"
#include "fschema/geometry/tests/png_writer.h"
#include "fschema/geometry/types.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"

namespace {

using fschema::geometry::ExportGlb;
using fschema::geometry::ResourcePack;
using fschema::geometry::TessellateRegion;
using fschema::geometry::TessellationOptions;
using fschema::ir::BlockDataEncoding;
using fschema::ir::BlockState;
using fschema::ir::BoundingBox;
using fschema::ir::PropertyEncoding;
using fschema::ir::Region;
using fschema::memory::Arena;
using fschema::memory::UnInitBuffer;
using fschema::tests::WritePngFile;

// Synthetic resource pack shared by all benchmarks, built once on first use.
struct GeometryFixture {
  std::filesystem::path root;
  std::unique_ptr<ResourcePack> pack;

  GeometryFixture() {
    std::random_device rd;
    fschema::base::Xoshiro256PP gen(rd());
    std::uniform_int_distribution<int> dist(0, 100'000'000);
    root = std::filesystem::temp_directory_path() /
           ("fschema_bench_" + std::to_string(dist(gen)));
    namespace fs = std::filesystem;
    fs::create_directories(root / "assets" / "minecraft" / "blockstates");
    fs::create_directories(root / "assets" / "minecraft" / "models" / "block");
    fs::create_directories(root / "assets" / "minecraft" / "textures" /
                           "block");

    WriteStr(root / "assets" / "minecraft" / "blockstates" / "stone.json",
             R"({"variants": {"": {"model": "block/cube_all"}}})");
    WriteStr(
        root / "assets" / "minecraft" / "models" / "block" / "cube_all.json",
        R"({"parent": "block/cube", "textures": {"all": "block/stone"}})");
    WriteStr(root / "assets" / "minecraft" / "models" / "block" / "cube.json",
             R"({"textures": {"particle": "block/stone"},)"
             R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
             R"( "faces": {)"
             R"(   "north": {"texture": "#all", "uv": [0,0,16,16]},)"
             R"(   "south": {"texture": "#all", "uv": [0,0,16,16]},)"
             R"(   "east":  {"texture": "#all", "uv": [0,0,16,16]},)"
             R"(   "west":  {"texture": "#all", "uv": [0,0,16,16]},)"
             R"(   "up":    {"texture": "#all", "uv": [0,0,16,16]},)"
             R"(   "down":  {"texture": "#all", "uv": [0,0,16,16]})"
            R"( }}]})");

    std::vector<std::uint8_t> rgba(16 * 16 * 4);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
      rgba[i + 0] = 0xAA;
      rgba[i + 1] = 0xBB;
      rgba[i + 2] = 0xCC;
      rgba[i + 3] = 0xDD;
    }
    WritePngFile(
        root / "assets" / "minecraft" / "textures" / "block" / "stone.png",
        16,
        16,
        rgba.data());

    auto r = ResourcePack::Load(root.generic_string());
    if (r.has_value()) pack = std::move(*r);
  }

  ~GeometryFixture() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }

  static void WriteStr(const std::filesystem::path& p, std::string_view s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
  }
};

const ResourcePack& GetPack() {
  static GeometryFixture* const fixture = new GeometryFixture();
  return *fixture->pack;
}

std::pair<std::unique_ptr<Arena>, Region> MakeStoneRegion(int edge) {
  auto arena = std::make_unique<Arena>();
  Region region{};
  region.bounds =
      BoundingBox{.origin = {0, 0, 0}, .size = {edge, edge, edge}};
  region.palette = {
      BlockState{.name = "minecraft:air",
                 .prop_encoding = PropertyEncoding::kNone},
      BlockState{.name = "minecraft:stone",
                 .prop_encoding = PropertyEncoding::kNone},
  };
  region.is_air_lut.assign(2, 0);
  region.is_air_lut[0] = 1;
  const std::size_t vol = static_cast<std::size_t>(edge) * edge * edge;
  region.block_indices = UnInitBuffer<std::uint16_t>(vol, arena.get());
  std::uint16_t* data =
      reinterpret_cast<std::uint16_t*>(region.block_indices.data());
  for (std::size_t i = 0; i < vol; ++i) data[i] = 1;
  region.is_materialized = true;
  region.lazy_source.encoding = BlockDataEncoding::kNone;
  return {std::move(arena), std::move(region)};
}

void BM_TessellateScalar(benchmark::State& state) {
  const int edge = static_cast<int>(state.range(0));
  auto [arena, region] = MakeStoneRegion(edge);
  const ResourcePack& pack = GetPack();

  TessellationOptions opts;
  opts.chunk_edge = 0;
  opts.merge_coplanar_faces = false;
  opts.ambient_occlusion = false;

  for (auto _ : state) {
    auto result = TessellateRegion(region, *arena, pack, opts);
    benchmark::DoNotOptimize(result);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(edge) * edge * edge);
  state.SetLabel(std::to_string(edge) + "^3 scalar");
}
BENCHMARK(BM_TessellateScalar)->RangeMultiplier(2)->Range(1, 64);

void BM_TessellateParallel(benchmark::State& state) {
  const int edge = static_cast<int>(state.range(0));
  auto [arena, region] = MakeStoneRegion(edge);
  const ResourcePack& pack = GetPack();

  TessellationOptions opts;
  opts.chunk_edge = 16;
  opts.merge_coplanar_faces = false;
  opts.ambient_occlusion = false;

  for (auto _ : state) {
    auto result = TessellateRegion(region, *arena, pack, opts);
    benchmark::DoNotOptimize(result);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(edge) * edge * edge);
  state.SetLabel(std::to_string(edge) + "^3 parallel");
}
BENCHMARK(BM_TessellateParallel)->RangeMultiplier(2)->Range(1, 64);

void BM_TessellateMerge(benchmark::State& state) {
  const int edge = static_cast<int>(state.range(0));
  auto [arena, region] = MakeStoneRegion(edge);
  const ResourcePack& pack = GetPack();

  TessellationOptions opts;
  opts.chunk_edge = 16;
  opts.merge_coplanar_faces = true;
  opts.ambient_occlusion = false;

  for (auto _ : state) {
    auto result = TessellateRegion(region, *arena, pack, opts);
    benchmark::DoNotOptimize(result);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(edge) * edge * edge);
  state.SetLabel(std::to_string(edge) + "^3 merge");
}
BENCHMARK(BM_TessellateMerge)->RangeMultiplier(2)->Range(1, 64);

void BM_TessellateFullQuality(benchmark::State& state) {
  const int edge = static_cast<int>(state.range(0));
  auto [arena, region] = MakeStoneRegion(edge);
  const ResourcePack& pack = GetPack();

  TessellationOptions opts;
  opts.chunk_edge = 16;
  opts.merge_coplanar_faces = true;
  opts.ambient_occlusion = true;
  opts.ao_strength = 0.4f;

  for (auto _ : state) {
    auto result = TessellateRegion(region, *arena, pack, opts);
    benchmark::DoNotOptimize(result);
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(edge) * edge * edge);
  state.SetLabel(std::to_string(edge) + "^3 full (merge+AO)");
}
BENCHMARK(BM_TessellateFullQuality)->RangeMultiplier(2)->Range(1, 64);

void BM_ExportGlb(benchmark::State& state) {
  const int edge = static_cast<int>(state.range(0));
  auto [arena, region] = MakeStoneRegion(edge);
  const ResourcePack& pack = GetPack();

  TessellationOptions opts;
  opts.chunk_edge = 16;
  opts.merge_coplanar_faces = true;
  opts.ambient_occlusion = false;

  auto tess = TessellateRegion(region, *arena, pack, opts);
  if (!tess.has_value()) {
    state.SkipWithError("TessellateRegion failed");
    return;
  }

  for (auto _ : state) {
    auto glb = ExportGlb(*tess);
    benchmark::DoNotOptimize(glb);
  }

  // POSITION(12) + NORMAL(12) + TEXCOORD_0(8) + COLOR_0(4) bytes per vertex,
  // plus 4 bytes per index.
  const std::size_t bytes =
      tess->opaque_mesh.VertexCount() * (12 + 12 + 8 + 4) +
      tess->opaque_mesh.indices.size() * 4;
  state.SetBytesProcessed(static_cast<std::int64_t>(bytes));
  state.SetLabel(std::to_string(edge) + "^3 GLB export");
}
BENCHMARK(BM_ExportGlb)->RangeMultiplier(2)->Range(1, 64);

// Vertex/triangle count comparison (direct vs merge).
void BM_Counts(benchmark::State& state) {
  const int edge = static_cast<int>(state.range(0));
  auto [arena, region] = MakeStoneRegion(edge);
  const ResourcePack& pack = GetPack();

  TessellationOptions direct_opts;
  direct_opts.chunk_edge = 16;
  direct_opts.merge_coplanar_faces = false;
  direct_opts.ambient_occlusion = false;

  auto direct = TessellateRegion(region, *arena, pack, direct_opts);

  TessellationOptions merge_opts = direct_opts;
  merge_opts.merge_coplanar_faces = true;
  auto merged = TessellateRegion(region, *arena, pack, merge_opts);

  if (!direct.has_value() || !merged.has_value()) {
    state.SkipWithError("TessellateRegion failed");
    return;
  }
  state.counters["vertices"] = direct->opaque_mesh.VertexCount();
  state.counters["triangles"] = direct->opaque_mesh.TriangleCount();
  state.counters["merged_triangles"] = merged->opaque_mesh.TriangleCount();
  state.counters["atlas_slots"] = direct->atlas.slots.size();

  for (auto _ : state) {
    benchmark::DoNotOptimize(direct);
  }
}
BENCHMARK(BM_Counts)->RangeMultiplier(2)->Range(1, 64);

}  // namespace

BENCHMARK_MAIN();
