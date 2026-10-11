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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fschema/base/block_utils.h"
#include "fschema/base/compressor.h"
#include "fschema/editors/bounding_box_trim.h"
#include "fschema/editors/extract.h"
#include "fschema/editors/fill.h"
#include "fschema/editors/hollow.h"
#include "fschema/editors/mirror.h"
#include "fschema/editors/move.h"
#include "fschema/editors/replace.h"
#include "fschema/editors/rotate.h"
#include "fschema/filters/box_filter.h"
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
namespace ed = fschema::editors;

namespace {

struct LoadedSchema {
  std::string filename;
  std::unique_ptr<fir::Schema> schema;
  std::vector<std::uint16_t> original_blocks;
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

      fir::Region& region = ls.schema->regions[0];
      const auto& b = region.bounds;

      // Snapshot pristine block indices so in-place editors can be reset
      // between iterations without re-decoding the file.
      ls.original_blocks.assign(region.block_indices.begin(),
                                region.block_indices.end());

      // Cache a full-region view for editors that take a view argument.
      filt::BoxFilter all_box{
          0, 0, 0, b.size[0] - 1, b.size[1] - 1, b.size[2] - 1};
      auto view_res = filt::MakeView(region, all_box, *ls.schema->arena);
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

void BM_Fill(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  if (!ls.view) return;
  fir::Region& region = ls.schema->regions[0];

  for (auto _ : st) {
    std::memcpy(region.block_indices.data(),
                ls.original_blocks.data(),
                ls.original_blocks.size() * sizeof(std::uint16_t));
    region.edited = false;

    auto res = ed::Fill(*ls.view, "minecraft:stone");
    benchmark::DoNotOptimize(res);
  }
  st.SetBytesProcessed(static_cast<std::int64_t>(ls.original_blocks.size()) *
                       sizeof(std::uint16_t) * st.iterations());
}

void BM_ReplaceWith(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  if (!ls.view) return;
  fir::Region& region = ls.schema->regions[0];

  // Pick any non-air palette entry as the source block.
  std::string_view from_block;
  for (const auto& bs : region.palette) {
    if (!fschema::base::IsAirVariant(bs.name)) {
      from_block = bs.name;
      break;
    }
  }
  if (from_block.empty()) {
    st.SkipWithError("No non-air block found in palette");
    return;
  }

  for (auto _ : st) {
    std::memcpy(region.block_indices.data(),
                ls.original_blocks.data(),
                ls.original_blocks.size() * sizeof(std::uint16_t));
    region.edited = false;

    auto res = ed::ReplaceWith(*ls.view, from_block, "minecraft:diamond_block");
    benchmark::DoNotOptimize(res);
  }
  st.SetBytesProcessed(static_cast<std::int64_t>(ls.original_blocks.size()) *
                       sizeof(std::uint16_t) * st.iterations());
}

void BM_Move(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  if (!ls.view) return;
  fir::Region& region = ls.schema->regions[0];

  for (auto _ : st) {
    std::memcpy(region.block_indices.data(),
                ls.original_blocks.data(),
                ls.original_blocks.size() * sizeof(std::uint16_t));
    region.edited = false;

    auto res = ed::Move(*ls.view, 1, 0, 0);
    benchmark::DoNotOptimize(res);
  }
  st.SetBytesProcessed(static_cast<std::int64_t>(ls.original_blocks.size()) *
                       sizeof(std::uint16_t) * st.iterations());
}

void BM_Hollow(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  if (!ls.view) return;
  fir::Region& region = ls.schema->regions[0];

  for (auto _ : st) {
    std::memcpy(region.block_indices.data(),
                ls.original_blocks.data(),
                ls.original_blocks.size() * sizeof(std::uint16_t));
    region.edited = false;

    auto res = ed::Hollow(*ls.view, *ls.schema->arena);
    benchmark::DoNotOptimize(res);
  }
  st.SetBytesProcessed(static_cast<std::int64_t>(ls.original_blocks.size()) *
                       sizeof(std::uint16_t) * st.iterations());
}

void BM_Extract(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  if (!ls.view) return;

  for (auto _ : st) {
    auto res = ed::Extract(*ls.view, *ls.schema->arena);
    benchmark::DoNotOptimize(res);
  }
  st.SetBytesProcessed(static_cast<std::int64_t>(ls.original_blocks.size()) *
                       sizeof(std::uint16_t) * st.iterations());
}

void BM_Mirror(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  if (!ls.view) return;

  for (auto _ : st) {
    auto res = ed::Mirror(*ls.view, filt::Axis::X, *ls.schema->arena);
    benchmark::DoNotOptimize(res);
  }
  st.SetBytesProcessed(static_cast<std::int64_t>(ls.original_blocks.size()) *
                       sizeof(std::uint16_t) * st.iterations());
}

void BM_Rotate(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  if (!ls.view) return;

  for (auto _ : st) {
    auto res = ed::Rotate(*ls.view, filt::Axis::Y, 1, *ls.schema->arena);
    benchmark::DoNotOptimize(res);
  }
  st.SetBytesProcessed(static_cast<std::int64_t>(ls.original_blocks.size()) *
                       sizeof(std::uint16_t) * st.iterations());
}

void BM_BoundingBoxTrim(benchmark::State& st) {
  const auto& schemas = GetTestSchemas();
  if (schemas.empty()) return;
  const auto& ls = schemas[st.range(0)];
  fir::Region& region = ls.schema->regions[0];

  for (auto _ : st) {
    auto res = ed::BoundingBoxTrim(region, *ls.schema->arena);
    benchmark::DoNotOptimize(res);
  }
  st.SetBytesProcessed(static_cast<std::int64_t>(ls.original_blocks.size()) *
                       sizeof(std::uint16_t) * st.iterations());
}

}  // namespace

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);

  const auto& g_schemas = GetTestSchemas();
  for (size_t i = 0; i < g_schemas.size(); ++i) {
    const std::string& name = g_schemas[i].filename;

    benchmark::RegisterBenchmark(("Fill/" + name).c_str(), BM_Fill)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("ReplaceWith/" + name).c_str(),
                                 BM_ReplaceWith)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("Move/" + name).c_str(), BM_Move)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("Hollow/" + name).c_str(), BM_Hollow)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("Extract/" + name).c_str(), BM_Extract)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("Mirror/" + name).c_str(), BM_Mirror)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("Rotate/" + name).c_str(), BM_Rotate)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("BoundingBoxTrim/" + name).c_str(),
                                 BM_BoundingBoxTrim)
        ->Arg(static_cast<int64_t>(i))
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);
  }

  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return 0;
}