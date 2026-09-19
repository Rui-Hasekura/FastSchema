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
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hwy/targets.h"

#include "parser/unpacker.h"
#include "parser/error.h"
#include "parser/litematic/parse.h"
#include "parser/litematic/types.h"

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

void PrintError(const fp::ParseError& e) {
  std::cerr << "  [ParseError] " << ToString(e.code)
    << " at path=\"" << e.path << "\""
    << " offset=" << e.offset << "\n";
}

[[nodiscard]] uint64_t ExpectedVolume(
  const std::array<int32_t, 3>& size) noexcept {
  const auto abs = [](int32_t v) -> uint64_t {
    return v < 0 ? uint64_t(-(int64_t(v) - 1)) - 1 : uint64_t(v);
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
  std::filesystem::path path;
  std::vector<std::byte> bytes;
};

[[nodiscard]] std::vector<TestFile> LoadTestFiles() {
  const std::filesystem::path paths[] = {
    R"(C:\Users\22744\Desktop\vss.litematic)"
  };
  std::vector<TestFile> out;
  for (const auto& p : paths) {
    if (!std::filesystem::exists(p)) {
      std::cerr << "  [warn] File not found: " << p << "\n";
      continue;
    }
    auto r = fschema::parser::UnpackLitematicFrom(p);
    if (!r) {
      std::cerr << "  [warn] Decompress error: " << p << "\n";
      continue;
    }
    out.push_back({ p, std::move(*r) });
  }
  return out;
}

static std::vector<TestFile> g_files = LoadTestFiles();

[[nodiscard]] bool ValidateRegion(const fl::Region& r, int region_idx) {
  bool ok = true;
  const std::string prefix =
    "  Region[" + std::to_string(region_idx) + "] \"" + r.name + "\": ";

  const uint64_t expected = ExpectedVolume(r.size);
  if (r.block_indices.size() != expected) {
    std::cerr << prefix << "FAIL block_indices.size()="
      << r.block_indices.size()
      << " != volume=" << expected << "\n";
    ok = false;
  }
  else {
    std::cout << prefix << "volume check OK ("
      << expected << " blocks)\n";
  }

  for (uint64_t i = 0; i < r.block_indices.size(); ++i) {
    if (r.block_indices[i] >= r.palette.size()) {
      std::cerr << prefix << "FAIL palette index out of range at "
        << i << ": " << r.block_indices[i]
        << " >= " << r.palette.size() << "\n";
      ok = false;
      break;
    }
  }
  if (ok) {
    std::cout << prefix << "palette range check OK ("
      << r.palette.size() << " entries)\n";
  }

  if (r.palette.empty()) {
    std::cerr << prefix << "FAIL palette is empty\n";
    ok = false;
  }

  return ok;
}

bool TestMaterialCount(const fl::Region& r) {
  if (r.palette.empty() || r.block_indices.empty()) return true;

  std::vector<uint64_t> counts(r.palette.size(), 0);
  for (uint32_t idx : r.block_indices) {
    ++counts[idx];
  }

  struct Entry { std::string name; uint64_t count; };
  std::vector<Entry> entries;
  entries.reserve(r.palette.size());
  for (size_t i = 0; i < r.palette.size(); ++i) {
    if (counts[i] > 0) {
      entries.push_back({ r.palette[i].name, counts[i] });
    }
  }

  std::sort(entries.begin(), entries.end(),
    [](const Entry& a, const Entry& b) {
      return a.count > b.count;
    });

  std::cout << "    Materials (top 15):\n";
  const size_t show = std::min(entries.size(), size_t{ 15 });
  for (size_t i = 0; i < show; ++i) {
    std::cout << "      " << entries[i].count << "x "
      << entries[i].name << "\n";
  }
  if (entries.size() > show) {
    std::cout << "      ... and " << (entries.size() - show)
      << " more types\n";
  }

  uint64_t non_air = 0;
  for (auto& e : entries) {
    if (!IsAirVariant(e.name)) non_air += e.count;
  }
  std::cout << "    Non-air total: " << non_air << "\n";

  return true;
}

void PrintOverview(const fl::Litematic& l) {
  std::cout << "  ── Overview ──\n";
  std::cout << "    Version: " << static_cast<int>(l.version) << "\n";
  std::cout << "    DataVersion: " << l.data_version << "\n";
  std::cout << "    Name: \"" << l.metadata.name << "\"\n";
  std::cout << "    Author: \"" << l.metadata.author << "\"\n";
  std::cout << "    Description: \""
    << l.metadata.description << "\"\n";
  std::cout << "    RegionCount: " << l.metadata.region_count << "\n";
  std::cout << "    TotalBlocks: " << l.metadata.total_blocks << "\n";
  std::cout << "    TotalVolume: " << l.metadata.total_volume << "\n";
  std::cout << "    EnclosingSize: ("
    << l.metadata.enclosing_size[0] << ", "
    << l.metadata.enclosing_size[1] << ", "
    << l.metadata.enclosing_size[2] << ")\n";
  std::cout << "    TimeCreated: " << l.metadata.time_created << "\n";
  std::cout << "    TimeModified: " << l.metadata.time_modified << "\n";
  std::cout << "    Regions parsed: " << l.regions.size() << "\n";

  for (size_t i = 0; i < l.regions.size(); ++i) {
    const auto& r = l.regions[i];
    std::cout << "  ── Region[" << i << "] \"" << r.name << "\" ──\n";
    std::cout << "    Position: ("
      << r.position[0] << ", "
      << r.position[1] << ", "
      << r.position[2] << ")\n";
    std::cout << "    Size: ("
      << r.size[0] << ", "
      << r.size[1] << ", "
      << r.size[2] << ")\n";
    std::cout << "    Palette entries: " << r.palette.size() << "\n";
    std::cout << "    BlockIndices: " << r.block_indices.size() << "\n";
    std::cout << "    Entities: " << r.entities.size() << "\n";
    std::cout << "    TileEntities: " << r.tile_entities.size() << "\n";

    if (!r.palette.empty()) {
      std::cout << "    Palette (first 5):\n";
      const size_t show = std::min(r.palette.size(), size_t{ 5 });
      for (size_t p = 0; p < show; ++p) {
        std::cout << "      [" << p << "] " << r.palette[p].name;
        if (!r.palette[p].properties.empty()) {
          std::cout << " (+props " << r.palette[p].properties.size()
            << "B)";
        }
        std::cout << "\n";
      }
    }

    if (!r.entities.empty()) {
      std::cout << "    Entities (first 3):\n";
      const size_t show = std::min(r.entities.size(), size_t{ 3 });
      for (size_t e = 0; e < show; ++e) {
        const auto& ent = r.entities[e];
        std::cout << "      " << ent.id << " at ("
          << ent.position[0] << ", "
          << ent.position[1] << ", "
          << ent.position[2] << ")\n";
      }
    }

    if (!r.tile_entities.empty()) {
      std::cout << "    TileEntities (first 3):\n";
      const size_t show = std::min(r.tile_entities.size(), size_t{ 3 });
      for (size_t t = 0; t < show; ++t) {
        const auto& te = r.tile_entities[t];
        std::cout << "      " << te.id << " at ("
          << te.block_position[0] << ", "
          << te.block_position[1] << ", "
          << te.block_position[2] << ")\n";
      }
    }
  }
}

// ── GBench ──
static void BM_ParseLitematic(benchmark::State& st) {
  const auto& tf = g_files[st.range(0)];
  const auto bytes_size = tf.bytes.size();

  for (auto _ : st) {
    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
    auto result = fl::ParseLitematic(std::move(owner));
    if (!result) {
      PrintError(result.error());
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
BENCHMARK(BM_ParseLitematic)
->Arg(0)
->Unit(benchmark::kMillisecond)
->Repetitions(20)
->DisplayAggregatesOnly(true);

int main(int argc, char* argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  std::cout << "Highway supported: 0x" << std::hex
    << hwy::SupportedTargets() << std::dec << "\n";

  int total = 0, passed = 0;

  for (const auto& tf : g_files) {
    ++total;
    std::cout << "\n════════════════════════════════════════════\n";
    std::cout << "Testing: " << tf.path.filename() << "\n";
    std::cout << "════════════════════════════════════════════\n";

    const auto file_size = std::filesystem::file_size(tf.path);
    std::cout << "  File size: " << file_size << " bytes\n";
    std::cout << "  [OK] Decompress: " << tf.bytes.size() << " bytes\n";

    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
    auto result = fl::ParseLitematic(std::move(owner));
    if (!result) {
      PrintError(result.error());
      continue;
    }
    fl::Litematic litematic = std::move(*result);
    std::cout << "  [OK] Parse\n";

    PrintOverview(litematic);

    bool all_valid = true;
    for (size_t i = 0; i < litematic.regions.size(); ++i) {
      if (!ValidateRegion(litematic.regions[i], int(i))) {
        all_valid = false;
      }
    }

    for (auto& r : litematic.regions) {
      TestMaterialCount(r);
    }

    if (all_valid) {
      std::cout << "\n  ═══ RESULT: PASS ═══\n";
      ++passed;
    }
    else {
      std::cout << "\n  ═══ RESULT: FAIL ═══\n";
    }
  }

  std::cout << "\n════════════════════════════════════════════\n";
  std::cout << "Summary: " << passed << "/" << total << " passed\n";
  std::cout << "════════════════════════════════════════════\n\n";

  // ── GBench ──
  ::benchmark::Initialize(&argc, argv);
  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();

  return (passed == total) ? 0 : 1;
}