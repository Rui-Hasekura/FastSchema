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

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fschema/base/compressor.h"
#include "fschema/filters/axis_filter.h"
#include "fschema/filters/block_name_filter.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/composite_filter.h"
#include "fschema/filters/neighbor_filter.h"
#include "fschema/filters/random_filter.h"
#include "fschema/filters/sphere_filter.h"
#include "fschema/filters/surface_filter.h"
#include "fschema/filters/view.h"
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematic_handler.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/schem_handler.h"
#include "fschema/ir/types.h"
#include "fschema/tests/testdata_util.h"

namespace fs = std::filesystem;
namespace fir = fschema::ir;
namespace firc = fschema::ir::format;
namespace filt = fschema::filters;

namespace {

struct LoadedSchema {
  std::string filename;
  std::unique_ptr<fir::Schema> schema;
};

[[nodiscard]] std::vector<LoadedSchema> LoadAllSchemas() {
  std::vector<LoadedSchema> out;
  const fs::path dir = fschema::test::GetTestDataDir();
  if (!fs::exists(dir)) return out;

  static bool initialized = false;
  if (!initialized) {
    firc::FormatRegistry::Instance().Register(
        std::make_unique<firc::LitematicaHandler>());
    firc::FormatRegistry::Instance().Register(
        std::make_unique<firc::SchemHandler>());
    initialized = true;
  }

  for (const auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    auto path = entry.path();
    auto ext = path.extension();
    if (ext != ".litematic" && ext != ".schem") continue;

    auto decompressed = fschema::base::DecompressGzipFile(path);
    if (!decompressed) continue;

    auto owner =
        std::make_unique<std::vector<std::byte>>(std::move(*decompressed));
    fir::SourceFormat fmt = (ext == ".litematic")
                                ? fir::SourceFormat::kLitematica
                                : fir::SourceFormat::kSchem;

    auto schema_res = firc::DecodeFromFormat(fmt, std::move(owner));
    if (!schema_res) continue;

    LoadedSchema ls;
    ls.filename = path.filename().string();
    ls.schema = std::make_unique<fir::Schema>(std::move(*schema_res));

    if (!ls.schema->regions.empty()) {
      auto mat_res =
          fir::EnsureMaterialized(ls.schema->regions[0], *ls.schema->arena);
      if (!mat_res) continue;
    }
    out.push_back(std::move(ls));
  }
  return out;
}

const std::vector<LoadedSchema>& GetTestSchemas() {
  static const std::vector<LoadedSchema> schemas = LoadAllSchemas();
  return schemas;
}

template <typename FilterType>
void RunFilterBenchmark(benchmark::State& st, FilterType filter) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) {
    st.SkipWithError("No test schemas found");
    return;
  }

  const auto& schema = schemas[st.range(0)];
  if (schema.schema->regions.empty()) {
    st.SkipWithError("No regions found");
    return;
  }
  fir::Region& region = schema.schema->regions[0];

  std::size_t total_blocks = 0;
  for (auto _ : st) {
    auto view_res = filt::MakeView(region, filter, *schema.schema->arena);
    if (!view_res) {
      st.SkipWithError("MakeView failed");
      return;
    }
    // Force full traversal so the filter's predicate is actually exercised.
    total_blocks = view_res->count();
    benchmark::DoNotOptimize(total_blocks);
  }

  st.counters["Blocks"] = benchmark::Counter(static_cast<double>(total_blocks));
  st.SetBytesProcessed(static_cast<std::int64_t>(total_blocks) *
                       sizeof(std::uint16_t) * st.iterations());
}

void BM_BoxFilter(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& region = schemas[st.range(0)].schema->regions[0];
  const auto& b = region.bounds;

  // Lower half along each axis; roughly one eighth of the volume.
  filt::BoxFilter box{0, 0, 0, b.size[0] / 2, b.size[1] / 2, b.size[2] / 2};
  RunFilterBenchmark(st, box);
}

void BM_SphereFilter(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& region = schemas[st.range(0)].schema->regions[0];
  const auto& b = region.bounds;

  std::int32_t cx = b.size[0] / 2;
  std::int32_t cy = b.size[1] / 2;
  std::int32_t cz = b.size[2] / 2;
  std::int64_t r2 = static_cast<std::int64_t>(cx) * cx +
                    static_cast<std::int64_t>(cy) * cy +
                    static_cast<std::int64_t>(cz) * cz;

  filt::SphereFilter sphere{cx, cy, cz, r2};
  RunFilterBenchmark(st, sphere);
}

void BM_AxisFilter(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& region = schemas[st.range(0)].schema->regions[0];
  const auto& b = region.bounds;

  filt::AxisFilter axis{filt::Axis::Y, filt::Cmp::Ge, b.size[1] / 2};
  RunFilterBenchmark(st, axis);
}

void BM_BlockNameFilter(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& region = schemas[st.range(0)].schema->regions[0];
  if (region.palette.empty()) return;

  // Use the last palette entry, which is typically a non-trivial block.
  filt::BlockNameFilter filter(region.palette.back().name, region);
  RunFilterBenchmark(st, filter);
}

void BM_IsAirFilter(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& region = schemas[st.range(0)].schema->regions[0];

  filt::IsAirFilter filter(region);
  RunFilterBenchmark(st, filter);
}

void BM_SurfaceFilter(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& region = schemas[st.range(0)].schema->regions[0];

  filt::SurfaceFilter filter(region);
  RunFilterBenchmark(st, filter);
}

void BM_NeighborFilter(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& region = schemas[st.range(0)].schema->regions[0];

  filt::IsAirFilter air_filter(region);
  filt::NeighborFilter neighbor_filter(region, air_filter);
  RunFilterBenchmark(st, neighbor_filter);
}

void BM_CompositeFilter(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& region = schemas[st.range(0)].schema->regions[0];
  const auto& b = region.bounds;

  filt::BoxFilter box{0, 0, 0, b.size[0] - 1, b.size[1] - 1, b.size[2] - 1};
  filt::IsAirFilter air_filter(region);
  auto composite = filt::And(box, air_filter);

  RunFilterBenchmark(st, composite);
}

}  // namespace

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);

  const auto& g_schemas = GetTestSchemas();
  for (size_t i = 0; i < g_schemas.size(); ++i) {
    const std::string& name = g_schemas[i].filename;

    benchmark::RegisterBenchmark(("BoxFilter/" + name).c_str(), BM_BoxFilter)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("SphereFilter/" + name).c_str(),
                                 BM_SphereFilter)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("AxisFilter/" + name).c_str(), BM_AxisFilter)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("BlockNameFilter/" + name).c_str(),
                                 BM_BlockNameFilter)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("IsAirFilter/" + name).c_str(),
                                 BM_IsAirFilter)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("SurfaceFilter/" + name).c_str(),
                                 BM_SurfaceFilter)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("NeighborFilter/" + name).c_str(),
                                 BM_NeighborFilter)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("CompositeFilter/" + name).c_str(),
                                 BM_CompositeFilter)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);
  }

  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return 0;
}