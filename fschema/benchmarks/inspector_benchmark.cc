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
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"
#include "fschema/inspectors/count.h"
#include "fschema/inspectors/diff.h"
#include "fschema/inspectors/floating_check.h"
#include "fschema/inspectors/list_material.h"
#include "fschema/inspectors/support_check.h"
#include "fschema/inspectors/version_check.h"
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematica_handler.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/schem_handler.h"
#include "fschema/ir/types.h"
#include "fschema/tests/testdata_util.h"

namespace fs = std::filesystem;
namespace fir = fschema::ir;
namespace firc = fschema::ir::format;
namespace filt = fschema::filters;
namespace insp = fschema::inspectors;

namespace {

struct LoadedSchema {
  std::string filename;
  std::unique_ptr<fir::Schema> schema;
  std::unique_ptr<filt::View> view;
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

      // Cache a full-region view so view-based inspectors do not pay for
      // filter construction inside the timed loop.
      const auto& b = ls.schema->regions[0].bounds;
      filt::BoxFilter all_box{
          0, 0, 0, b.size[0] - 1, b.size[1] - 1, b.size[2] - 1};
      auto view_res =
          filt::MakeView(ls.schema->regions[0], all_box, *ls.schema->arena);
      if (view_res) {
        ls.view = std::make_unique<filt::View>(*view_res);
      }
    }
    out.push_back(std::move(ls));
  }
  return out;
}

const std::vector<LoadedSchema>& GetTestSchemas() {
  static const std::vector<LoadedSchema> schemas = LoadAllSchemas();
  return schemas;
}

void BM_Count(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  if (!ls.view) return;

  std::size_t count = 0;
  for (auto _ : st) {
    count = insp::Count(*ls.view, "minecraft:air");
    benchmark::DoNotOptimize(count);
  }
  st.SetBytesProcessed(static_cast<std::int64_t>(count) *
                       sizeof(std::uint16_t) * st.iterations());
}

void BM_ListMaterial(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  const auto& region = ls.schema->regions[0];

  std::vector<insp::MaterialEntry> materials;
  for (auto _ : st) {
    materials = insp::ListMaterial(region, false);
    benchmark::DoNotOptimize(materials.data());
  }

  std::uint64_t volume = region.block_indices.size();
  st.SetBytesProcessed(static_cast<std::int64_t>(volume) *
                       sizeof(std::uint16_t) * st.iterations());
}

void BM_Diff(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  const auto& region = ls.schema->regions[0];

  std::vector<insp::DiffEntry> diffs;
  for (auto _ : st) {
    auto diff_res = insp::Diff(region, region);
    if (!diff_res) continue;
    diffs = std::move(*diff_res);
    benchmark::DoNotOptimize(diffs.data());
  }

  std::uint64_t volume = region.block_indices.size();
  st.SetBytesProcessed(static_cast<std::int64_t>(volume) *
                       sizeof(std::uint16_t) * 2 * st.iterations());
}

void BM_FloatingCheck(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  const auto& region = ls.schema->regions[0];

  insp::FloatingCheckResult result;
  for (auto _ : st) {
    result = insp::FloatingCheck(region);
    benchmark::DoNotOptimize(result.blocks.data());
  }

  std::uint64_t volume = region.block_indices.size();
  st.SetBytesProcessed(static_cast<std::int64_t>(volume) *
                       sizeof(std::uint16_t) * st.iterations());
}

void BM_SupportCheck(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  const auto& region = ls.schema->regions[0];

  std::vector<insp::UnsupportedBlock> unsupported;
  for (auto _ : st) {
    unsupported = insp::SupportCheck(region);
    benchmark::DoNotOptimize(unsupported.data());
  }

  std::uint64_t volume = region.block_indices.size();
  st.SetBytesProcessed(static_cast<std::int64_t>(volume) *
                       sizeof(std::uint16_t) * st.iterations());
}

void BM_VersionCheck(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  if (!ls.view) return;

  std::int32_t version = 0;
  for (auto _ : st) {
    version = insp::GetMinimumRequiredDataVersion(*ls.view);
    benchmark::DoNotOptimize(version);
  }

  std::uint64_t volume = ls.schema->regions[0].block_indices.size();
  st.SetBytesProcessed(static_cast<std::int64_t>(volume) *
                       sizeof(std::uint16_t) * st.iterations());
}

}  // namespace

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);

  const auto& g_schemas = GetTestSchemas();
  for (size_t i = 0; i < g_schemas.size(); ++i) {
    const std::string& name = g_schemas[i].filename;

    benchmark::RegisterBenchmark(("Count/" + name).c_str(), BM_Count)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("ListMaterial/" + name).c_str(),
                                 BM_ListMaterial)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("Diff/" + name).c_str(), BM_Diff)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("FloatingCheck/" + name).c_str(),
                                 BM_FloatingCheck)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("SupportCheck/" + name).c_str(),
                                 BM_SupportCheck)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("VersionCheck/" + name).c_str(),
                                 BM_VersionCheck)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);
  }

  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return 0;
}