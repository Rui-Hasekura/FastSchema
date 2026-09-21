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
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hwy/targets.h"

#include "parser/error.h"
#include "parser/litematic/parse.h"
#include "parser/litematic/types.h"
#include "parser/unpacker.h"

#ifndef FASTSCHEMA_SAMPLES_DIR
#error "FASTSCHEMA_SAMPLES_DIR is not defined. Please check CMakeLists.txt."
#endif

namespace fp = fschema::parser;
namespace fl = fschema::parser::litematic;

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
  case C::Truncated:
    return "Truncated";
  case C::InvalidTagId:
    return "InvalidTagId";
  case C::NegativeLength:
    return "NegativeLength";
  case C::DepthLimitExceeded:
    return "DepthLimitExceeded";
  case C::OversizedPayload:
    return "OversizedPayload";
  case C::UnsupportedVersion:
    return "UnsupportedVersion";
  case C::MissingField:
    return "MissingField";
  case C::BlockStatesTooSmall:
    return "BlockStatesTooSmall";
  case C::PaletteIndexOutOfRange:
    return "PaletteIndexOutOfRange";
  case C::VolumeOverflow:
    return "VolumeOverflow";
  default:
    return "Unknown";
  }
}

[[nodiscard]] std::uint64_t ExpectedVolume(
  const std::array<std::int32_t, 3>& size) noexcept {
  const auto abs = [](std::int32_t v) -> std::uint64_t {
    return v < 0 ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(v))
      : static_cast<std::uint64_t>(v);
    };
  return abs(size[0]) * abs(size[1]) * abs(size[2]);
}

[[nodiscard]] bool IsAirVariant(std::string_view name) noexcept {
  return name == "minecraft:air" ||
    name == "minecraft:void_air" ||
    name == "minecraft:cave_air";
}

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
    if (!entry.is_regular_file()) {
      continue;
    }

    auto path = entry.path();
    if (path.extension() == ".litematic") {
      std::string filename = path.filename().string();

      auto r = fschema::parser::DecompressGzipFile(path);
      if (!r) {
        std::cerr << "  [warn] Decompress error or invalid litematic: "
          << filename << "\n";
        continue;
      }
      out.push_back({ filename, std::move(*r) });
    }
  }
  return out;
}

static std::vector<TestFile> files = LoadTestFiles();

void PrintErrorFn(const fp::ParseError& e) {
  std::cerr << "  [ParseError] " << ToString(e.code)
    << " at path=\"" << e.path << "\" offset=" << e.offset << "\n";
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
    auto owner = std::make_unique<std::vector<std::byte>>(std::move(reusable_buffer));
    auto result = fl::ParseLitematic(std::move(owner));
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
    auto r = fl::ParseLitematic(std::move(owner));
    if (r) {
      std::uint64_t total_blocks = 0;
      for (const auto& reg : r->regions) {
        total_blocks += reg.block_indices.size();
      }
      st.counters["blocks"] = benchmark::Counter(total_blocks);
      st.counters["MiB_in"] =
        benchmark::Counter(static_cast<double>(bytes_size) / (1024.0 * 1024.0));
    }
  }
}

[[nodiscard]] bool ValidateRegion(const fl::Region& r, bool full) {
  bool ok = true;
  const std::uint64_t expected = ExpectedVolume(r.size);
  if (r.block_indices.size() != expected) {
    ok = false;
  }
  if (full) {
    for (std::uint64_t i = 0; i < r.block_indices.size(); ++i) {
      if (r.block_indices[i] >= r.palette.size()) {
        ok = false;
        break;
      }
    }
  }
  else {
    const std::uint64_t n = r.block_indices.size();
    constexpr std::uint64_t stride = 4096;
    for (std::uint64_t i = 0; i < n; i += stride) {
      if (r.block_indices[i] >= r.palette.size()) {
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

[[nodiscard]] std::uint64_t NonAirCount(const fl::Region& r) {
  if (r.palette.empty() || r.block_indices.empty()) {
    return 0;
  }
  std::vector<std::uint64_t> counts(r.palette.size(), 0);
  for (std::uint32_t idx : r.block_indices) {
    ++counts[idx];
  }
  std::uint64_t non_air = 0;
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (counts[i] > 0 && !IsAirVariant(r.palette[i].name)) {
      non_air += counts[i];
    }
  }
  return non_air;
}

int main(int argc, char* argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  std::cout << "Highway supported: 0x" << std::hex
    << hwy::SupportedTargets() << std::dec << "\n";

  bool all_ok = true;
  if (files.empty()) {
    std::cerr << "  [fatal] No valid .litematic test files found.\n";
    all_ok = false;
  }

  for (std::size_t fi = 0; fi < files.size(); ++fi) {
    const auto& tf = files[fi];
    std::cout << "\n══════ Verify: " << tf.filename << " ════\n";
    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
    auto r = fl::ParseLitematic(std::move(owner));
    if (!r) {
      PrintErrorFn(r.error());
      all_ok = false;
      continue;
    }

    for (std::size_t i = 0; i < r->regions.size(); ++i) {
      if (!ValidateRegion(r->regions[i], false)) {
        all_ok = false;
      }
    }
    std::uint64_t non_air_total = 0;
    for (const auto& reg : r->regions) {
      non_air_total += NonAirCount(reg);
    }
    bool conserved =
      (non_air_total == static_cast<std::uint64_t>(r->metadata.total_blocks));
    std::cout << "  Conservation: " << non_air_total
      << (conserved ? " == " : " != ")
      << r->metadata.total_blocks << "\n";
    if (!conserved) {
      all_ok = false;
    }
  }
  std::cout << "Verify: " << (all_ok ? "PASS" : "FAIL") << "\n\n";

  ::benchmark::Initialize(&argc, argv);

  for (std::size_t i = 0; i < files.size(); ++i) {
    benchmark::RegisterBenchmark(
      ("LitematicParse/" + files[i].filename).c_str(),
      BM_ParseLitematic)
      ->Arg(i)
      ->Unit(benchmark::kMillisecond)
      ->MinTime(2.0);
  }

  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return all_ok ? 0 : 1;
}