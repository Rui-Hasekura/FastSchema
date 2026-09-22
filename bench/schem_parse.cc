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

#include "parser/arena.h"
#include "parser/error.h"
#include "parser/limits.h"
#include "parser/nbt/reader.h"
#include "parser/schem/detail/root.h"
#include "parser/schem/types.h"
#include "parser/unpacker.h"

#ifndef FASTSCHEMA_SAMPLES_DIR
#error "FASTSCHEMA_SAMPLES_DIR is not defined. Please check CMakeLists.txt."
#endif

namespace fp = fschema::parser;
namespace fsc = fschema::parser::schem;

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
  case C::VarintOverflow:
    return "VarintOverflow";
  case C::BlockDataTooSmall:
    return "BlockDataTooSmall";
  default:
    return "Unknown";
  }
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
    if (path.extension() == ".schem") {
      std::string filename = path.filename().string();

      auto r = fschema::parser::DecompressGzipFile(path);
      if (!r) {
        std::cerr << "  [warn] Decompress error or invalid schem: "
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

[[nodiscard]] bool ValidateSchematic(const fsc::Schematic& s, bool full) {
  bool ok = true;
  const std::uint64_t expected = fsc::VolumeOf(s);
  if (s.block_indices.size() != expected) {
    ok = false;
  }
  if (full) {
    for (std::uint64_t i = 0; i < s.block_indices.size(); ++i) {
      if (s.block_indices[i] >= s.palette.size()) {
        ok = false;
        break;
      }
    }
  }
  else {
    const std::uint64_t n = s.block_indices.size();
    constexpr std::uint64_t stride = 4096;
    for (std::uint64_t i = 0; i < n; i += stride) {
      if (s.block_indices[i] >= s.palette.size()) {
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

[[nodiscard]] std::uint64_t NonAirCount(const fsc::Schematic& s) {
  if (s.palette.empty() || s.block_indices.empty()) {
    return 0;
  }
  std::vector<std::uint64_t> counts(s.palette.size(), 0);
  for (std::uint16_t idx : s.block_indices) {
    ++counts[idx];
  }
  std::uint64_t non_air = 0;
  for (std::size_t i = 0; i < s.palette.size(); ++i) {
    if (counts[i] > 0 && !IsAirVariant(s.palette[i].name)) {
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
    schematic.arena = std::make_unique<fp::Arena>();
    schematic.owner = std::make_unique<std::vector<std::byte>>(
      std::move(reusable_buffer));

    fp::DecodeLimits limits;
    fp::nbt::ByteReader reader(
      std::span<const std::byte>(
        schematic.owner->data(),
        schematic.owner->size()),
      limits);

    auto result = fsc::detail::ParseRoot(reader, schematic);
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
    schematic.arena = std::make_unique<fp::Arena>();
    schematic.owner =
      std::make_unique<std::vector<std::byte>>(tf.bytes);

    fp::DecodeLimits limits;
    fp::nbt::ByteReader reader(
      std::span<const std::byte>(
        schematic.owner->data(),
        schematic.owner->size()),
      limits);

    auto r = fsc::detail::ParseRoot(reader, schematic);
    if (r) {
      std::uint64_t total_blocks = schematic.block_indices.size();
      st.counters["blocks"] = benchmark::Counter(total_blocks);
      st.counters["MiB_in"] = benchmark::Counter(
        static_cast<double>(bytes_size) / (1024.0 * 1024.0));
    }
  }
}

int main(int argc, char* argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  std::cout << "Highway supported: 0x" << std::hex
    << hwy::SupportedTargets() << std::dec << "\n";

  bool all_ok = true;
  if (files.empty()) {
    std::cerr << "  [fatal] No valid .schem test files found.\n";
    all_ok = false;
  }

  for (std::size_t fi = 0; fi < files.size(); ++fi) {
    const auto& tf = files[fi];
    std::cout << "\n--- Verify: " << tf.filename << " ---\n";

    fsc::Schematic schematic;
    schematic.arena = std::make_unique<fp::Arena>();
    schematic.owner =
      std::make_unique<std::vector<std::byte>>(tf.bytes);

    fp::DecodeLimits limits;
    fp::nbt::ByteReader reader(
      std::span<const std::byte>(
        schematic.owner->data(),
        schematic.owner->size()),
      limits);

    auto r = fsc::detail::ParseRoot(reader, schematic);
    if (!r) {
      PrintErrorFn(r.error());
      all_ok = false;
      continue;
    }

    if (!ValidateSchematic(schematic, false)) {
      all_ok = false;
    }

    std::uint64_t non_air_total = NonAirCount(schematic);

    std::cout << "  Version:      "
      << static_cast<int>(schematic.version) << "\n";
    std::cout << "  DataVersion:  " << schematic.data_version << "\n";
    std::cout << "  Dimensions:   " << schematic.width << " x "
      << schematic.height << " x " << schematic.length << "\n";
    std::cout << "  Volume:       " << fsc::VolumeOf(schematic) << "\n";
    std::cout << "  Palette:      " << schematic.palette.size() << "\n";
    std::cout << "  BlockEnts:    "
      << schematic.block_entities.size() << "\n";
    std::cout << "  Entities:     " << schematic.entities.size() << "\n";
    std::cout << "  Non-air:      " << non_air_total << "\n";

    if (!schematic.biome_indices.empty()) {
      std::uint64_t biome_expected = fsc::BiomeVolumeOf(schematic);
      bool biome_ok =
        (schematic.biome_indices.size() == biome_expected);
      std::cout << "  BiomePal:     "
        << schematic.biome_palette.size() << "\n";
      std::cout << "  BiomeIdx:     "
        << schematic.biome_indices.size()
        << (biome_ok ? " (ok)" : " (MISMATCH)") << "\n";
      if (!biome_ok) {
        all_ok = false;
      }
    }
  }
  std::cout << "Verify: " << (all_ok ? "PASS" : "FAIL") << "\n\n";

  ::benchmark::Initialize(&argc, argv);

  for (std::size_t i = 0; i < files.size(); ++i) {
    benchmark::RegisterBenchmark(
      ("SchemParse/" + files[i].filename).c_str(),
      BM_ParseSchem)
      ->Arg(i)
      ->Unit(benchmark::kMillisecond)
      ->MinTime(2.0);
  }

  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return all_ok ? 0 : 1;
}