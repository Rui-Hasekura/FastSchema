// Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hwy/targets.h"

#include "parser/unpacker.h"
#include "parser/error.h"
#include "parser/litematic/parse.h"
#include "parser/litematic/types.h"

#ifndef FASTSCHEMA_SAMPLES_DIR
#error "FASTSCHEMA_SAMPLES_DIR is not defined. Please check CMakeLists.txt."
#endif

namespace fp = fschema::parser;
namespace fl = fschema::parser::litematic;

// NO #include "windows.h" pls...
#if defined(_WIN32) || defined(_WIN64)
#ifndef CP_UTF8
#define CP_UTF8 65001u
#endif
#if defined(_M_IX86) || defined(__i386__)
#define MY_WINAPI __stdcall
#else
#define MY_WINAPI
#endif
extern "C" {
  __declspec(dllimport) int MY_WINAPI SetConsoleOutputCP(unsigned int);
  __declspec(dllimport) int MY_WINAPI SetConsoleCP(unsigned int);
}
#undef MY_WINAPI
#endif

[[nodiscard]] std::string ToString(fp::ParseError::Code code) {
  using C = fp::ParseError::Code;
  switch (code) {
  case C::Truncated:              return "Truncated";
  case C::InvalidTagId:           return "InvalidTagId";
  case C::NegativeLength:         return "NegativeLength";
  case C::DepthLimitExceeded:     return "DepthLimitExceeded";
  case C::OversizedPayload:       return "OversizedPayload";
  case C::UnsupportedVersion:     return "UnsupportedVersion";
  case C::MissingField:           return "MissingField";
  case C::BlockStatesTooSmall:    return "BlockStatesTooSmall";
  case C::PaletteIndexOutOfRange: return "PaletteIndexOutOfRange";
  case C::VolumeOverflow:         return "VolumeOverflow";
  }
  return "Unknown";
}

[[nodiscard]] uint64_t ExpectedVolume(
  const std::array<int32_t, 3>& size) noexcept {
  const auto abs = [](int32_t v) -> uint64_t {
    return v < 0 ? uint64_t(-int64_t(v)) : uint64_t(v);
    };
  return abs(size[0]) * abs(size[1]) * abs(size[2]);
}

[[nodiscard]] bool IsAirVariant(std::string_view name) noexcept {
  return name == "minecraft:air"
    || name == "minecraft:void_air"
    || name == "minecraft:cave_air";
}

// COMMON DECOMPRESSION DATA
struct TestFile {
  std::string filename;
  std::vector<std::byte> bytes;
};

[[nodiscard]] std::vector<TestFile> LoadTestFiles() {
  std::vector<TestFile> out;
  const std::filesystem::path dir = FASTSCHEMA_SAMPLES_DIR;

  if (!std::filesystem::exists(dir)) {
    std::cerr << "  [warn] Samples directory not found: " << dir << "\n";
    return out;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;

    auto path = entry.path();
    if (path.extension() == ".litematic") {
      std::string filename = path.filename().string();

      auto r = fschema::parser::UnpackLitematicFrom(path);
      if (!r) {
        std::cerr << "  [warn] Decompress error or invalid litematic: " << filename << "\n";
        continue;
      }
      out.push_back({ filename, std::move(*r) });
    }
  }
  return out;
}

static std::vector<TestFile> g_files = LoadTestFiles();

void PrintErrorFn(const fp::ParseError& e) {
  std::cerr << "  [ParseError] " << ToString(e.code)
    << " at path=\"" << e.path << "\" offset=" << e.offset << "\n";
}

// E2E Test
static void BM_ParseLitematic(benchmark::State& st) {
  if (g_files.empty()) {
    st.SkipWithError("No test files loaded");
    return;
  }

  const auto& tf = g_files[st.range(0)];
  const auto bytes_size = tf.bytes.size();

  for (auto _ : st) {
    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
    auto result = fl::ParseLitematic(std::move(owner));
    if (!result) {
      PrintErrorFn(result.error());
      st.SkipWithError("parse failed");
      return;
    }
    benchmark::DoNotOptimize(result);
  }
  st.SetBytesProcessed(int64_t(st.iterations()) * int64_t(bytes_size));

  {
    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
    auto r = fl::ParseLitematic(std::move(owner));
    if (r) {
      uint64_t total_blocks = 0;
      for (auto& reg : r->regions) total_blocks += reg.block_indices.size();
      st.counters["blocks"] = benchmark::Counter(total_blocks);
      st.counters["MiB_in"] =
        benchmark::Counter(double(bytes_size) / (1024.0 * 1024.0));
    }
  }
}

[[nodiscard]] bool ValidateRegion(const fl::Region& r, bool full) {
  bool ok = true;
  const uint64_t expected = ExpectedVolume(r.size);
  if (r.block_indices.size() != expected) ok = false;
  if (full) {
    for (uint64_t i = 0; i < r.block_indices.size(); ++i) {
      if (r.block_indices[i] >= r.palette.size()) { ok = false; break; }
    }
  }
  else {
    const uint64_t n = r.block_indices.size();
    constexpr uint64_t stride = 4096;
    for (uint64_t i = 0; i < n; i += stride) {
      if (r.block_indices[i] >= r.palette.size()) { ok = false; break; }
    }
  }
  if (r.palette.empty()) ok = false;
  return ok;
}

[[nodiscard]] uint64_t NonAirCount(const fl::Region& r) {
  if (r.palette.empty() || r.block_indices.empty()) return 0;
  std::vector<uint64_t> counts(r.palette.size(), 0);
  for (uint32_t idx : r.block_indices) ++counts[idx];
  uint64_t non_air = 0;
  for (size_t i = 0; i < r.palette.size(); ++i) {
    if (counts[i] > 0 && !IsAirVariant(r.palette[i].name)) non_air += counts[i];
  }
  return non_air;
}

int main(int argc, char* argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  std::cout << "Highway supported: 0x" << std::hex
    << hwy::SupportedTargets() << std::dec << "\n";

  bool all_ok = true;
  if (g_files.empty()) {
    std::cerr << "  [fatal] No valid .litematic test files found.\n";
    all_ok = false;
  }

  for (size_t fi = 0; fi < g_files.size(); ++fi) {
    const auto& tf = g_files[fi];
    std::cout << "\n══════ Verify: " << tf.filename << " ════\n";
    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
    auto r = fl::ParseLitematic(std::move(owner));
    if (!r) { PrintErrorFn(r.error()); all_ok = false; continue; }

    for (size_t i = 0; i < r->regions.size(); ++i) {
      if (!ValidateRegion(r->regions[i], false)) all_ok = false;
    }
    uint64_t non_air_total = 0;
    for (auto& reg : r->regions) non_air_total += NonAirCount(reg);
    bool conserved =
      (non_air_total == uint64_t(r->metadata.total_blocks));
    std::cout << "  Conservation: " << non_air_total
      << (conserved ? " == " : " != ")
      << r->metadata.total_blocks << "\n";
    if (!conserved) all_ok = false;
  }
  std::cout << "Verify: " << (all_ok ? "PASS" : "FAIL") << "\n\n";

  // GBench
  ::benchmark::Initialize(&argc, argv);

  for (size_t i = 0; i < g_files.size(); ++i) {
    benchmark::RegisterBenchmark(
      ("LitematicParse/" + g_files[i].filename).c_str(),
      BM_ParseLitematic
    )->Arg(i)->Unit(benchmark::kMillisecond)->Repetitions(20)->DisplayAggregatesOnly(true);
  }

  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return all_ok ? 0 : 1;
}