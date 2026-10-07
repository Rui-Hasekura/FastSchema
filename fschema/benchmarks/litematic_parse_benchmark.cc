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
#include <array>
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
#include "fschema/litematic/internal/block_states.h"
#include "fschema/litematic/parse.h"
#include "fschema/litematic/types.h"
#include "fschema/memory/arena.h"
#include "fschema/tests/testdata_util.h"
#include "hwy/targets.h"

namespace {

[[nodiscard]] std::uint64_t ExpectedVolume(
    const std::array<std::int32_t, 3>& size) noexcept {
  const auto abs = [](std::int32_t v) -> std::uint64_t {
    return v < 0 ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(v))
                 : static_cast<std::uint64_t>(v);
  };
  return abs(size[0]) * abs(size[1]) * abs(size[2]);
}

struct TestFile {
  std::string filename;
  std::vector<std::byte> bytes;
};

[[nodiscard]] std::vector<TestFile> LoadTestFiles() {
  std::vector<TestFile> out;
  const std::filesystem::path dir = fschema::test::GetTestDataDir();

  if (!std::filesystem::exists(dir)) {
    std::println("  [warn] Samples directory not found: {}", dir.string());
    return out;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    auto path = entry.path();
    if (path.extension() == ".litematic") {
      std::string filename = path.filename().string();

      auto r = fschema::base::DecompressGzipFile(path);
      if (!r) {
        std::println("  [warn] Decompress error or invalid litematic: {}",
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

static void BM_ParseLitematic(benchmark::State& st) {
  if (files.empty()) {
    st.SkipWithError("No test files loaded");
    return;
  }

  const auto& tf = files[st.range(0)];
  const auto bytes_size = tf.bytes.size();

  std::vector<std::byte> reusable_buffer = tf.bytes;

  for (auto _ : st) {
    auto owner =
        std::make_unique<std::vector<std::byte>>(std::move(reusable_buffer));
    auto result = fschema::litematic::ParseLitematic(std::move(owner));
    if (!result) {
      PrintErrorFn(result.error());
      st.SkipWithError("parse failed");
      return;
    }
    benchmark::DoNotOptimize(result);
    reusable_buffer = std::move(*result->owner);
  }
  st.SetBytesProcessed(static_cast<std::int64_t>(st.iterations()) *
                       static_cast<std::int64_t>(bytes_size));

  {
    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
    auto r = fschema::litematic::ParseLitematic(std::move(owner));
    if (r) {
      std::uint64_t total_blocks = 0;
      for (const auto& reg : r->regions) {
        total_blocks += ExpectedVolume(reg.size);
      }
      st.counters["blocks"] = benchmark::Counter(total_blocks);
      st.counters["MiB_in"] = benchmark::Counter(
          static_cast<double>(bytes_size) / (1024.0 * 1024.0));
    }
  }
}

static void BM_UnpackLitematic(benchmark::State& st) {
  if (files.empty()) {
    st.SkipWithError("No test files loaded");
    return;
  }

  const auto& tf = files[st.range(0)];

  auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
  auto result = fschema::litematic::ParseLitematic(std::move(owner));
  if (!result || result->regions.empty()) {
    st.SkipWithError("Parse failed or no regions");
    return;
  }

  const auto& reg = result->regions[0];
  std::uint64_t volume = ExpectedVolume(reg.size);
  std::uint32_t bpb =
      fschema::litematic::internal::BitsPerBlock(reg.palette.size());
  std::size_t bytes_size = reg.raw_block_states.size();

  for (auto _ : st) {
    fschema::memory::Arena arena;
    auto res = fschema::litematic::internal::UnpackIndicesFused(
        reg.raw_block_states, bpb, volume, reg.palette.size());
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

[[nodiscard]] bool ValidateRegion(const fschema::litematic::Region& r,
                                  bool full,
                                  fschema::memory::Arena& arena) {
  bool ok = true;
  const std::uint64_t expected = ExpectedVolume(r.size);

  if (expected == 0) return r.raw_block_states.empty();

  const std::uint32_t bpb =
      fschema::litematic::internal::BitsPerBlock(r.palette.size());
  const std::uint64_t min_longs = (expected * bpb + 63) / 64;
  if (static_cast<std::uint64_t>(r.raw_block_states.size() / 8) < min_longs) {
    return false;
  }

  auto res = fschema::litematic::internal::UnpackIndicesFused(
      r.raw_block_states, bpb, expected, r.palette.size(), arena);
  if (!res) return false;

  if (full) {
    for (std::uint64_t i = 0; i < res->size(); ++i) {
      if ((*res)[i] >= r.palette.size()) {
        ok = false;
        break;
      }
    }
  } else {
    const std::uint64_t n = res->size();
    constexpr std::uint64_t stride = 4096;
    for (std::uint64_t i = 0; i < n; i += stride) {
      if ((*res)[i] >= r.palette.size()) {
        ok = false;
        break;
      }
    }
  }
  if (r.palette.empty()) {
    ok = false;
  }
  return ok;
}

[[nodiscard]] std::uint64_t NonAirCount(const fschema::litematic::Region& r,
                                        fschema::memory::Arena& arena) {
  if (r.palette.empty() || r.raw_block_states.empty()) {
    return 0;
  }
  const std::uint64_t expected = ExpectedVolume(r.size);
  if (expected == 0) return 0;

  const std::uint32_t bpb =
      fschema::litematic::internal::BitsPerBlock(r.palette.size());

  auto res = fschema::litematic::internal::UnpackIndicesFused(
      r.raw_block_states, bpb, expected, r.palette.size(), arena);
  if (!res) return 0;

  std::vector<std::uint64_t> counts(r.palette.size(), 0);
  for (std::uint16_t idx : *res) {
    ++counts[idx];
  }
  std::uint64_t non_air = 0;
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (counts[i] > 0 && !fschema::base::IsAirVariant(r.palette[i].name)) {
      non_air += counts[i];
    }
  }
  return non_air;
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
    std::println("  [fatal] No valid .litematic test files found.");
    all_ok = false;
  }

  for (std::size_t fi = 0; fi < files.size(); ++fi) {
    const auto& tf = files[fi];
    std::println("\n══════ Verify: {} ════", tf.filename);
    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
    auto r = fschema::litematic::ParseLitematic(std::move(owner));
    if (!r) {
      PrintErrorFn(r.error());
      all_ok = false;
      continue;
    }

    for (std::size_t i = 0; i < r->regions.size(); ++i) {
      if (!ValidateRegion(r->regions[i], false, *r->arena)) {
        all_ok = false;
      }
    }
    std::uint64_t non_air_total = 0;
    for (const auto& reg : r->regions) {
      non_air_total += NonAirCount(reg, *r->arena);
    }
    bool conserved =
        (non_air_total == static_cast<std::uint64_t>(r->metadata.total_blocks));
    std::println("  Conservation: {} {} {}",
                 non_air_total,
                 conserved ? "==" : "!=",
                 r->metadata.total_blocks);
    if (!conserved) {
      all_ok = false;
    }
  }
  std::println("Verify: {}", all_ok ? "PASS" : "FAIL");

  ::benchmark::Initialize(&argc, argv);

  for (std::size_t i = 0; i < files.size(); ++i) {
    benchmark::RegisterBenchmark(
        ("LitematicParse/" + files[i].filename).c_str(), BM_ParseLitematic)
        ->Arg(i)
        ->Unit(benchmark::kMillisecond)
        ->MinTime(2.0);
    benchmark::RegisterBenchmark(
        ("LitematicUnpack/" + files[i].filename).c_str(), BM_UnpackLitematic)
        ->Arg(i)
        ->Unit(benchmark::kMillisecond)
        ->MinTime(2.0);
  }

  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return all_ok ? 0 : 1;
}