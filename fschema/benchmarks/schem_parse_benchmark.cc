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
#include <filesystem>
#include <memory>
#include <print>
#include <string>
#include <vector>

#include "fschema/base/block_utils.h"
#include "fschema/base/compressor.h"
#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/base/nbt/reader.h"
#include "fschema/memory/arena.h"
#include "fschema/schem/internal/block_data.h"
#include "fschema/schem/internal/root.h"
#include "fschema/schem/types.h"
#include "fschema/tests/testdata_util.h"
#include "hwy/targets.h"

namespace fb = fschema::base;
namespace fm = fschema::memory;
namespace fsc = fschema::schem;

namespace {

struct TestFile {
  std::string filename;
  std::vector<std::byte> bytes;
};

[[nodiscard]] std::vector<TestFile> LoadTestFiles() {
  std::vector<TestFile> out;
  const std::filesystem::path dir = fschema::test::GetTestDataDir();

  if (!std::filesystem::exists(dir)) {
    std::println("  [warn] Testdata directory not found: {}", dir.string());
    return out;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    auto path = entry.path();
    if (path.extension() == ".schem") {
      std::string filename = path.filename().string();

      auto r = fb::DecompressGzipFile(path);
      if (!r) {
        std::println("  [warn] Decompress error or invalid schem: {}",
                     filename);
        continue;
      }
      out.push_back({filename, std::move(*r)});
    }
  }
  return out;
}

static std::vector<TestFile> files = LoadTestFiles();

void PrintErrorFn(const fschema::ParseError& e) {
  std::println("  [ParseError] {} at path=\"{}\" offset={}",
               ToString(e.code),
               e.path,
               e.offset);
}

[[nodiscard]] bool ValidateSchematic(const fsc::Schematic& s,
                                     bool full,
                                     fm::Arena& arena) {
  bool ok = true;
  const std::uint64_t expected = fsc::VolumeOf(s);
  if (expected == 0) return s.raw_block_data.empty();

  auto res = fsc::internal::DecodeVarintArray(
      s.raw_block_data, expected, s.palette.size(), arena);
  if (!res) return false;

  if (full) {
    for (std::uint64_t i = 0; i < res->size(); ++i) {
      if ((*res)[i] >= s.palette.size()) {
        ok = false;
        break;
      }
    }
  } else {
    const std::uint64_t n = res->size();
    constexpr std::uint64_t stride = 4096;
    for (std::uint64_t i = 0; i < n; i += stride) {
      if ((*res)[i] >= s.palette.size()) {
        ok = false;
        break;
      }
    }
  }
  if (s.palette.empty()) {
    ok = false;
  }
  return ok;
}

[[nodiscard]] std::uint64_t NonAirCount(const fsc::Schematic& s,
                                        fm::Arena& arena) {
  if (s.palette.empty() || s.raw_block_data.empty()) {
    return 0;
  }
  const std::uint64_t expected = fsc::VolumeOf(s);
  if (expected == 0) return 0;

  auto res = fsc::internal::DecodeVarintArray(
      s.raw_block_data, expected, s.palette.size(), arena);
  if (!res) return 0;

  std::vector<std::uint64_t> counts(s.palette.size(), 0);
  for (std::uint16_t idx : *res) {
    ++counts[idx];
  }
  std::uint64_t non_air = 0;
  for (std::size_t i = 0; i < s.palette.size(); ++i) {
    if (counts[i] > 0 && !fschema::base::IsAirVariant(s.palette[i].name)) {
      non_air += counts[i];
    }
  }
  return non_air;
}

static void BM_ParseSchem(benchmark::State& st) {
  if (files.empty()) {
    st.SkipWithError("No test files loaded");
    return;
  }

  const auto& tf = files[st.range(0)];
  const auto bytes_size = tf.bytes.size();

  std::vector<std::byte> reusable_buffer = tf.bytes;

  for (auto _ : st) {
    fsc::Schematic schematic;
    schematic.arena = std::make_unique<fm::Arena>();
    schematic.owner =
        std::make_unique<std::vector<std::byte>>(std::move(reusable_buffer));

    fb::DecodeLimits limits;
    fb::ByteReader reader(std::span<const std::byte>(schematic.owner->data(),
                                                     schematic.owner->size()),
                          limits);

    auto result = fsc::internal::ParseRoot(reader, schematic);
    if (!result) {
      PrintErrorFn(result.error());
      st.SkipWithError("parse failed");
      return;
    }
    benchmark::DoNotOptimize(schematic);
    reusable_buffer = std::move(*schematic.owner);
  }
  st.SetBytesProcessed(static_cast<std::int64_t>(st.iterations()) *
                       static_cast<std::int64_t>(bytes_size));

  {
    fsc::Schematic schematic;
    schematic.arena = std::make_unique<fm::Arena>();
    schematic.owner = std::make_unique<std::vector<std::byte>>(tf.bytes);

    fb::DecodeLimits limits;
    fb::ByteReader reader(std::span<const std::byte>(schematic.owner->data(),
                                                     schematic.owner->size()),
                          limits);

    auto r = fsc::internal::ParseRoot(reader, schematic);
    if (r) {
      std::uint64_t total_blocks = fsc::VolumeOf(schematic);
      st.counters["blocks"] = benchmark::Counter(total_blocks);
      st.counters["MiB_in"] = benchmark::Counter(
          static_cast<double>(bytes_size) / (1024.0 * 1024.0));
    }
  }
}

static void BM_UnpackSchem(benchmark::State& st) {
  if (files.empty()) {
    st.SkipWithError("No test files loaded");
    return;
  }

  const auto& tf = files[st.range(0)];

  fsc::Schematic schematic;
  schematic.arena = std::make_unique<fm::Arena>();
  schematic.owner = std::make_unique<std::vector<std::byte>>(tf.bytes);

  fb::DecodeLimits limits;
  fb::ByteReader reader(std::span<const std::byte>(schematic.owner->data(),
                                                   schematic.owner->size()),
                        limits);
  auto r = fsc::internal::ParseRoot(reader, schematic);
  if (!r) {
    st.SkipWithError("Parse failed");
    return;
  }

  std::uint64_t volume = fsc::VolumeOf(schematic);
  std::size_t bytes_size = schematic.raw_block_data.size();

  for (auto _ : st) {
    fschema::memory::Arena arena;
    auto res = fsc::internal::DecodeVarintArray(
        schematic.raw_block_data, volume, schematic.palette.size(), arena);
    if (!res) {
      st.SkipWithError("Unpack failed");
      return;
    }
    benchmark::DoNotOptimize(res);
  }
  st.SetBytesProcessed(static_cast<std::int64_t>(st.iterations()) *
                       static_cast<std::int64_t>(bytes_size));
  st.counters["MiB_in"] =
      benchmark::Counter(static_cast<double>(bytes_size) / (1024.0 * 1024.0));
}

}  // namespace

int main(int argc, char* argv[]) {
#if defined(_WIN32) || defined(_WIN64)
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  std::println("Highway supported: 0x{:x}", hwy::SupportedTargets());

  bool all_ok = true;
  if (files.empty()) {
    std::println("  [fatal] No valid .schem test files found.");
    all_ok = false;
  }

  for (std::size_t fi = 0; fi < files.size(); ++fi) {
    const auto& tf = files[fi];
    std::println("\n--- Verify: {} ---", tf.filename);

    fsc::Schematic schematic;
    schematic.arena = std::make_unique<fm::Arena>();
    schematic.owner = std::make_unique<std::vector<std::byte>>(tf.bytes);

    fb::DecodeLimits limits;
    fb::ByteReader reader(std::span<const std::byte>(schematic.owner->data(),
                                                     schematic.owner->size()),
                          limits);

    auto r = fsc::internal::ParseRoot(reader, schematic);
    if (!r) {
      PrintErrorFn(r.error());
      all_ok = false;
      continue;
    }

    if (!ValidateSchematic(schematic, false, *schematic.arena)) {
      all_ok = false;
    }

    std::uint64_t non_air_total = NonAirCount(schematic, *schematic.arena);

    std::println("  Version:      {}", static_cast<int>(schematic.version));
    std::println("  DataVersion:  {}", schematic.data_version);
    std::println("  Dimensions:   {} x {} x {}",
                 schematic.width,
                 schematic.height,
                 schematic.length);
    std::println("  Volume:       {}", fsc::VolumeOf(schematic));
    std::println("  Palette:      {}", schematic.palette.size());
    std::println("  BlockEnts:    {}", schematic.block_entities.size());
    std::println("  Entities:     {}", schematic.entities.size());
    std::println("  Non-air:      {}", non_air_total);

    if (!schematic.raw_biome_data.empty()) {
      std::println("  BiomePal:     {}", schematic.biome_palette.size());
      std::println("  BiomeData:    {} bytes", schematic.raw_biome_data.size());
    }
  }
  std::println("Verify: {}", all_ok ? "PASS" : "FAIL");

  ::benchmark::Initialize(&argc, argv);

  for (std::size_t i = 0; i < files.size(); ++i) {
    benchmark::RegisterBenchmark(("SchemParse/" + files[i].filename).c_str(),
                                 BM_ParseSchem)
        ->Arg(i)
        ->Unit(benchmark::kMillisecond)
        ->MinTime(2.0);
    benchmark::RegisterBenchmark(("SchemUnpack/" + files[i].filename).c_str(),
                                 BM_UnpackSchem)
        ->Arg(i)
        ->Unit(benchmark::kMillisecond)
        ->MinTime(2.0);
  }

  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return all_ok ? 0 : 1;
}