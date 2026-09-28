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
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "fschema/base/block_utils.h"
#include "fschema/base/compressor.h"
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematic_handler.h"
#include "fschema/ir/schem_handler.h"
#include "fschema/ir/types.h"
#include "fschema/tests/testdata_util.h"
#include "hwy/targets.h"

#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace fir = fschema::ir;
namespace firc = fschema::ir::format;

struct TestFile {
  std::string filename;
  std::vector<std::byte> bytes;
  fir::SourceFormat format;
};

[[nodiscard]] std::vector<TestFile> LoadTestFiles() {
  std::vector<TestFile> out;
  const std::filesystem::path dir = fschema::test::GetTestDataDir();

  if (!std::filesystem::exists(dir)) {
    std::cerr << "  [warn] Samples directory not found: " << dir << "\n";
    return out;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;

    auto path = entry.path();
    auto ext = path.extension();
    if (ext == ".litematic" || ext == ".schem") {
      std::string filename = path.filename().string();
      auto r = fschema::base::DecompressGzipFile(path);
      if (!r) {
        std::cerr << "  [warn] Decompress error: " << filename << "\n";
        continue;
      }
      out.push_back({filename,
                     std::move(*r),
                     (ext == ".litematic") ? fir::SourceFormat::kLitematica
                                           : fir::SourceFormat::kSchem});
    }
  }
  return out;
}

static std::vector<TestFile> files = LoadTestFiles();

void RegisterHandlers() {
  static bool initialized = false;
  if (!initialized) {
    firc::FormatRegistry::Instance().Register(
        std::make_unique<firc::LitematicaHandler>());
    firc::FormatRegistry::Instance().Register(
        std::make_unique<firc::SchemHandler>());
    initialized = true;
  }
}

[[nodiscard]] std::uint64_t CalculateTotalBlocks(const fir::Schema& ir) {
  std::uint64_t total = 0;
  for (const auto& reg : ir.regions) {
    if (reg.palette.empty() || reg.block_indices.empty()) continue;

    std::vector<std::uint64_t> counts(reg.palette.size(), 0);
    for (std::uint16_t idx : reg.block_indices) {
      if (idx < counts.size()) ++counts[idx];
    }
    for (size_t i = 0; i < reg.palette.size(); ++i) {
      if (counts[i] > 0 && !fschema::base::IsAirVariant(reg.palette[i].name)) {
        total += counts[i];
      }
    }
  }
  return total;
}

static void BM_ConvertFormat(benchmark::State& st) {
  const auto& tf = files[st.range(0)];
  const auto bytes_size = tf.bytes.size();

  fir::SourceFormat target_fmt = (tf.format == fir::SourceFormat::kLitematica)
                                     ? fir::SourceFormat::kSchem
                                     : fir::SourceFormat::kLitematica;

  std::vector<std::byte> reusable_buffer = tf.bytes;

  for (auto _ : st) {
    auto owner = std::make_unique<std::vector<std::byte>>(reusable_buffer);

    firc::EncodeOptions opts;
    opts.multi_region =
        firc::EncodeOptions::MultiRegionStrategy::kMergeBoundingBox;

    auto result = firc::Convert(tf.format, target_fmt, std::move(owner), opts);
    if (!result) {
      st.SkipWithError("Conversion failed");
      return;
    }
    benchmark::DoNotOptimize(result);
  }

  st.SetBytesProcessed(static_cast<std::int64_t>(st.iterations()) *
                       static_cast<std::int64_t>(bytes_size));
  st.counters["MiB_in"] =
      benchmark::Counter(static_cast<double>(bytes_size) / (1024.0 * 1024.0));
}

static void BM_RoundTripSameFormat(benchmark::State& st) {
  const auto& tf = files[st.range(0)];
  const auto bytes_size = tf.bytes.size();

  std::vector<std::byte> reusable_buffer = tf.bytes;

  for (auto _ : st) {
    auto owner = std::make_unique<std::vector<std::byte>>(reusable_buffer);

    firc::EncodeOptions opts;
    // Encode back to original format to test raw span zero-copy efficiency
    auto result = firc::Convert(tf.format, tf.format, std::move(owner), opts);
    if (!result) {
      st.SkipWithError("Round-trip failed");
      return;
    }
    benchmark::DoNotOptimize(result);
  }

  st.SetBytesProcessed(static_cast<std::int64_t>(st.iterations()) *
                       static_cast<std::int64_t>(bytes_size));
  st.counters["MiB_in"] =
      benchmark::Counter(static_cast<double>(bytes_size) / (1024.0 * 1024.0));
}

int main(int argc, char* argv[]) {
#if defined(_WIN32) || defined(_WIN64)
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  std::cout << "Highway supported: 0x" << std::hex << hwy::SupportedTargets()
            << std::dec << "\n";

  RegisterHandlers();

  bool all_ok = true;
  if (files.empty()) {
    std::cerr << "  [fatal] No test files found.\n";
    all_ok = false;
  }

  for (std::size_t i = 0; i < files.size(); ++i) {
    const auto& tf = files[i];
    std::cout << "\n══════ Verify: " << tf.filename << " ════\n";

    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
    auto ir_result = firc::DecodeFromFormat(tf.format, std::move(owner));
    if (!ir_result) {
      std::cerr << "  [ParseError] Decode failed\n";
      all_ok = false;
      continue;
    }

    const auto& ir = *ir_result;
    std::uint64_t initial_blocks = CalculateTotalBlocks(ir);
    std::cout << "  Initial Regions: " << ir.regions.size() << "\n";
    std::cout << "  Initial NonAir: " << initial_blocks << "\n";

    // Cross-format conversion check
    struct TargetConfig {
      fir::SourceFormat fmt;
      int version;
      std::string ext;
      std::string label;
    };

    std::vector<TargetConfig> targets;
    if (tf.format == fir::SourceFormat::kLitematica) {
      // CROSS Litematica -> Schem v2, v3
      targets.push_back({fir::SourceFormat::kSchem, 2, ".schem", "SchemV2"});
      targets.push_back({fir::SourceFormat::kSchem, 3, ".schem", "SchemV3"});
      // INSIDE Litematica -> Litematica v5, v6, v7
      targets.push_back(
          {fir::SourceFormat::kLitematica, 5, ".litematic", "LiteV5"});
      targets.push_back(
          {fir::SourceFormat::kLitematica, 6, ".litematic", "LiteV6"});
      targets.push_back(
          {fir::SourceFormat::kLitematica, 7, ".litematic", "LiteV7"});
    } else {
      // CROSS Schem -> Litematica v5, v6, v7
      targets.push_back(
          {fir::SourceFormat::kLitematica, 5, ".litematic", "LiteV5"});
      targets.push_back(
          {fir::SourceFormat::kLitematica, 6, ".litematic", "LiteV6"});
      targets.push_back(
          {fir::SourceFormat::kLitematica, 7, ".litematic", "LiteV7"});
      // INSIDE Schem -> Schem v2, v3
      targets.push_back({fir::SourceFormat::kSchem, 2, ".schem", "SchemV2"});
      targets.push_back({fir::SourceFormat::kSchem, 3, ".schem", "SchemV3"});
    }

    for (const auto& tgt : targets) {
      firc::EncodeOptions opts;
      opts.multi_region =
          firc::EncodeOptions::MultiRegionStrategy::kMergeBoundingBox;
      opts.target_version = tgt.version;

      auto owner2 = std::make_unique<std::vector<std::byte>>(tf.bytes);
      auto target_bytes_res =
          firc::Convert(tf.format, tgt.fmt, std::move(owner2), opts);
      if (target_bytes_res) {
        std::filesystem::path out_path = tf.filename;
        out_path.replace_extension(tgt.ext);

        std::string out_name =
            "converted_" + tgt.label + "_" + out_path.string();
        std::ofstream ofs(out_name, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(target_bytes_res->data()),
                  target_bytes_res->size());

        auto target_owner = std::make_unique<std::vector<std::byte>>(
            std::move(*target_bytes_res));
        auto ir2_result =
            firc::DecodeFromFormat(tgt.fmt, std::move(target_owner));
        if (!ir2_result) {
          std::cerr << "  [ParseError] Failed to decode converted " << tgt.label
                    << " file\n";
          all_ok = false;
          continue;
        }

        std::uint64_t converted_blocks = CalculateTotalBlocks(*ir2_result);
        std::cout << "  Converted " << tgt.label
                  << " NonAir: " << converted_blocks << "\n";
        if (converted_blocks != initial_blocks) {
          std::cerr << "  [FAIL] Block count mismatch after conversion to "
                    << tgt.label << "!\n";
          all_ok = false;
        } else {
          std::cout << "  [PASS] " << tgt.label << " integrity verified.\n";
        }
      } else {
        std::cerr << "  [ConvertError] Cross-format conversion to " << tgt.label
                  << " failed\n";
        all_ok = false;
      }
    }
  }

  std::cout << "\nVerify: " << (all_ok ? "PASS" : "FAIL") << "\n\n";

  ::benchmark::Initialize(&argc, argv);

  for (std::size_t i = 0; i < files.size(); ++i) {
    std::string name = "CrossConvert/" + files[i].filename;
    benchmark::RegisterBenchmark(name.c_str(), BM_ConvertFormat)
        ->Arg(i)
        ->Unit(benchmark::kMillisecond)
        ->MinTime(2.0);

    std::string rt_name = "RoundTripSame/" + files[i].filename;
    benchmark::RegisterBenchmark(rt_name.c_str(), BM_RoundTripSameFormat)
        ->Arg(i)
        ->Unit(benchmark::kMillisecond)
        ->MinTime(30.0);
  }

  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return all_ok ? 0 : 1;
}