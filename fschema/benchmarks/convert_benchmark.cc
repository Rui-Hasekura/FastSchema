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
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "fschema/base/block_utils.h"
#include "fschema/base/compressor.h"
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematic_handler.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/schem_handler.h"
#include "fschema/ir/types.h"
#include "fschema/tests/testdata_util.h"
#include "hwy/targets.h"

namespace fir = fschema::ir;
namespace firc = fschema::ir::format;

namespace {

constexpr double kMiB = 1024.0 * 1024.0;

struct TestFile {
  std::string filename;
  std::vector<std::byte> bytes;
  fir::SourceFormat format;
};

[[nodiscard]] std::vector<TestFile> LoadTestFiles() {
  std::vector<TestFile> out;
  const std::filesystem::path dir = fschema::test::GetTestDataDir();

  if (!std::filesystem::exists(dir)) {
    std::println(
        stderr, "  [warn] Samples directory not found: {}", dir.string());
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
        std::println(stderr, "  [warn] Decompress error: {}", filename);
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
    if (reg.palette.empty()) continue;
    if (ir.arena) {
      auto res = fir::EnsureMaterialized(reg, *ir.arena);
      if (!res) {
        std::println(
            stderr, "  [MaterializeError] {}", fschema::ToString(res.error()));
        continue;
      }
    }

    if (reg.block_indices.empty()) continue;

    std::vector<std::uint64_t> counts(reg.palette.size(), 0);
    for (std::uint16_t idx : reg.block_indices) {
      if (idx < counts.size()) ++counts[idx];
    }
    for (std::size_t i = 0; i < reg.palette.size(); ++i) {
      if (counts[i] > 0 && !fschema::base::IsAirVariant(reg.palette[i].name)) {
        total += counts[i];
      }
    }
  }
  return total;
}

struct IrStats {
  std::size_t regions = 0;
  std::uint64_t volume = 0;
  std::size_t pal_max = 0;
  unsigned bpb_max = 0;
};

[[nodiscard]] IrStats ComputeIrStats(const fir::Schema& ir) {
  IrStats s;
  s.regions = ir.regions.size();
  for (const auto& reg : ir.regions) {
    s.volume += static_cast<std::uint64_t>(reg.bounds.size[0]) *
                static_cast<std::uint64_t>(reg.bounds.size[1]) *
                static_cast<std::uint64_t>(reg.bounds.size[2]);
    s.pal_max = std::max(s.pal_max, reg.palette.size());
  }
  if (s.pal_max > 1) {
    unsigned bits = 1;
    while (bits < 32 && (std::size_t{1} << bits) < s.pal_max) ++bits;
    s.bpb_max = bits;
  } else {
    s.bpb_max = s.pal_max == 1 ? 1u : 0u;
  }
  return s;
}

[[nodiscard]] fir::SourceFormat OtherFormat(fir::SourceFormat f) noexcept {
  return f == fir::SourceFormat::kLitematica ? fir::SourceFormat::kSchem
                                             : fir::SourceFormat::kLitematica;
}

void SetShapeCounters(benchmark::State& st, const IrStats& s) {
  st.counters["regions"] = benchmark::Counter(static_cast<double>(s.regions));
  st.counters["vol_M"] =
      benchmark::Counter(static_cast<double>(s.volume) / 1e6);
  st.counters["pal_max"] = benchmark::Counter(static_cast<double>(s.pal_max));
  st.counters["bpb"] = benchmark::Counter(static_cast<double>(s.bpb_max));
}

// Benchmarks

// Full pipeline through the public Convert API.
// mode: 0 = cross-format,
// mode: 1 = same-format (round trip),
// mode: 2 = cross + gzip.
void BM_Convert(benchmark::State& st) {
  const auto& tf = files[static_cast<std::size_t>(st.range(0))];
  const int mode = static_cast<int>(st.range(1));
  const bool same_format = (mode == 1);
  const fir::SourceFormat target =
      same_format ? tf.format : OtherFormat(tf.format);

  firc::EncodeOptions opts;
  opts.compress = (mode == 2);
  if (!same_format) {
    opts.multi_region =
        firc::EncodeOptions::MultiRegionStrategy::kMergeBoundingBox;
  }

  std::uint64_t out_bytes = 0;
  for (auto _ : st) {
    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
    auto result = firc::Convert(tf.format, target, std::move(owner), opts);
    if (!result) {
      st.SkipWithError("Conversion failed");
      return;
    }
    out_bytes = result->size();
    benchmark::DoNotOptimize(result);
  }

  st.SetBytesProcessed(static_cast<std::int64_t>(tf.bytes.size()) *
                       st.iterations());
  st.counters["MiB_in"] =
      benchmark::Counter(static_cast<double>(tf.bytes.size()) / kMiB);
  st.counters["MiB_out"] =
      benchmark::Counter(static_cast<double>(out_bytes) / kMiB);
}

void BM_ConvertStaged(benchmark::State& st) {
  const auto& tf = files[static_cast<std::size_t>(st.range(0))];
  const bool same_format = (st.range(1) != 0);
  const fir::SourceFormat target =
      same_format ? tf.format : OtherFormat(tf.format);

  firc::EncodeOptions opts;
  opts.compress = false;
  if (!same_format) {
    opts.multi_region =
        firc::EncodeOptions::MultiRegionStrategy::kMergeBoundingBox;
  }

  std::uint64_t out_bytes = 0;
  std::optional<IrStats> shape;

  for (auto _ : st) {
    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);

    auto r = firc::DecodeFromFormat(tf.format, std::move(owner));
    if (!r) {
      st.SkipWithError("Decode failed");
      return;
    }
    if (!shape) shape = ComputeIrStats(*r);

    auto out = firc::EncodeToFormat(target, *r, opts);
    if (!out) {
      st.SkipWithError("Encode failed");
      return;
    }
    out_bytes = out->size();
    benchmark::DoNotOptimize(out->data());
  }

  st.SetBytesProcessed(static_cast<std::int64_t>(tf.bytes.size()) *
                       st.iterations());
  st.counters["MiB_in"] =
      benchmark::Counter(static_cast<double>(tf.bytes.size()) / kMiB);
  st.counters["MiB_out"] =
      benchmark::Counter(static_cast<double>(out_bytes) / kMiB);
  if (shape) SetShapeCounters(st, *shape);
}

void BM_DecodeOnly(benchmark::State& st) {
  const auto& tf = files[static_cast<std::size_t>(st.range(0))];
  const std::uint64_t in_bytes = tf.bytes.size();

  for (auto _ : st) {
    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
    auto r = firc::DecodeFromFormat(tf.format, std::move(owner));
    if (!r) {
      st.SkipWithError("Decode failed");
      return;
    }
    benchmark::DoNotOptimize(*r);
  }

  st.SetBytesProcessed(static_cast<std::int64_t>(in_bytes) * st.iterations());
  st.counters["MiB_in"] =
      benchmark::Counter(static_cast<double>(in_bytes) / kMiB);
}

void BM_EncodeOnly(benchmark::State& st) {
  const auto& tf = files[static_cast<std::size_t>(st.range(0))];
  const bool same_format = (st.range(1) != 0);
  const fir::SourceFormat target =
      same_format ? tf.format : OtherFormat(tf.format);

  auto ir = firc::DecodeFromFormat(
      tf.format, std::make_unique<std::vector<std::byte>>(tf.bytes));
  if (!ir) {
    st.SkipWithError("Decode failed");
    return;
  }
  const IrStats shape = ComputeIrStats(*ir);

  firc::EncodeOptions opts;
  opts.compress = false;
  if (!same_format) {
    opts.multi_region =
        firc::EncodeOptions::MultiRegionStrategy::kMergeBoundingBox;
  }

  std::uint64_t out_bytes = 0;
  for (auto _ : st) {
    auto out = firc::EncodeToFormat(target, *ir, opts);
    if (!out) {
      st.SkipWithError("Encode failed");
      return;
    }
    out_bytes = out->size();
    benchmark::DoNotOptimize(out->data());
  }

  st.SetBytesProcessed(static_cast<std::int64_t>(out_bytes) * st.iterations());
  st.counters["MiB_in"] =
      benchmark::Counter(static_cast<double>(tf.bytes.size()) / kMiB);
  st.counters["MiB_out"] =
      benchmark::Counter(static_cast<double>(out_bytes) / kMiB);
  SetShapeCounters(st, shape);
}

void BM_CopyOnly(benchmark::State& st) {
  const auto& tf = files[static_cast<std::size_t>(st.range(0))];
  const std::uint64_t in_bytes = tf.bytes.size();
  for (auto _ : st) {
    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
    benchmark::DoNotOptimize(owner->data());
  }
  st.SetBytesProcessed(static_cast<std::int64_t>(in_bytes) * st.iterations());
  st.counters["MiB_in"] =
      benchmark::Counter(static_cast<double>(in_bytes) / kMiB);
}

// Verification
[[nodiscard]] bool RunVerification() {
  bool all_ok = true;
  if (files.empty()) {
    std::println(stderr, "  [fatal] No test files found.");
    return false;
  }

  for (std::size_t i = 0; i < files.size(); ++i) {
    const auto& tf = files[i];
    std::println(stderr, "\n══════ Verify: {} ════", tf.filename);

    auto owner = std::make_unique<std::vector<std::byte>>(tf.bytes);
    auto ir_result = firc::DecodeFromFormat(tf.format, std::move(owner));
    if (!ir_result) {
      std::println(stderr, "  [ParseError] Decode failed");
      all_ok = false;
      continue;
    }

    const auto& ir = *ir_result;
    const IrStats shape = ComputeIrStats(ir);
    const std::uint64_t initial_blocks = CalculateTotalBlocks(ir);
    std::println(stderr, "  Initial Regions: {}", shape.regions);
    std::println(stderr, "  Initial NonAir: {}", initial_blocks);
    std::println(
        stderr, "  Volume: {}  pal_max: {}", shape.volume, shape.pal_max);
    std::println(stderr, "  bpb: {}", shape.bpb_max);

    struct TargetConfig {
      fir::SourceFormat fmt;
      int version;
      std::string ext;
      std::string label;
    };

    std::vector<TargetConfig> targets;
    if (tf.format == fir::SourceFormat::kLitematica) {
      targets.push_back({fir::SourceFormat::kSchem, 2, ".schem", "SchemV2"});
      targets.push_back({fir::SourceFormat::kSchem, 3, ".schem", "SchemV3"});
      targets.push_back(
          {fir::SourceFormat::kLitematica, 5, ".litematic", "LitematicV5"});
      targets.push_back(
          {fir::SourceFormat::kLitematica, 6, ".litematic", "LitematicV6"});
      targets.push_back(
          {fir::SourceFormat::kLitematica, 7, ".litematic", "LitematicV7"});
    } else {
      targets.push_back(
          {fir::SourceFormat::kLitematica, 5, ".litematic", "LitematicV5"});
      targets.push_back(
          {fir::SourceFormat::kLitematica, 6, ".litematic", "LitematicV6"});
      targets.push_back(
          {fir::SourceFormat::kLitematica, 7, ".litematic", "LitematicV7"});
      targets.push_back({fir::SourceFormat::kSchem, 2, ".schem", "SchemV2"});
      targets.push_back({fir::SourceFormat::kSchem, 3, ".schem", "SchemV3"});
    }

    for (const auto& tgt : targets) {
      firc::EncodeOptions opts;
      opts.compress = true;
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
          std::println(stderr,
                       "  [ParseError] Failed to decode converted {} file",
                       tgt.label);
          all_ok = false;
          continue;
        }

        std::uint64_t converted_blocks = CalculateTotalBlocks(*ir2_result);
        std::println(
            stderr, "  Converted {} NonAir: {}", tgt.label, converted_blocks);
        if (converted_blocks != initial_blocks) {
          std::println(stderr,
                       "  [FAIL] Block count mismatch after conversion to {}!",
                       tgt.label);
          all_ok = false;
        } else {
          std::println(stderr, "  [PASS] {} integrity verified.", tgt.label);
        }
      } else {
        std::println(stderr,
                     "  [ConvertError] Cross-format conversion to {} failed",
                     tgt.label);
        all_ok = false;
      }
    }
  }
  return all_ok;
}

}  // namespace

int main(int argc, char* argv[]) {
#if defined(_WIN32) || defined(_WIN64)
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  std::println(stderr, "Highway supported: 0x{:x}", hwy::SupportedTargets());

  RegisterHandlers();

  // All benchmarks use compress=false (raw NBT output) except
  // CrossConvertGzip
  // Tip: --benchmark_filter=Convert|EncodeOnly|CopyOnly|DecodeOnly

  const bool all_ok = RunVerification();
  std::println(stderr, "\nVerify: {}", all_ok ? "PASS" : "FAIL");

  ::benchmark::Initialize(&argc, argv);

  for (std::size_t i = 0; i < files.size(); ++i) {
    const std::string& name = files[i].filename;
    const auto idx = static_cast<std::int64_t>(i);

    benchmark::RegisterBenchmark(("CopyOnly/" + name).c_str(), BM_CopyOnly)
        ->Arg(idx)
        ->Unit(benchmark::kMillisecond)
        ->MinTime(2.0);

    benchmark::RegisterBenchmark(("DecodeOnly/" + name).c_str(), BM_DecodeOnly)
        ->Arg(idx)
        ->Unit(benchmark::kMillisecond)
        ->MinTime(2.0);

    benchmark::RegisterBenchmark(("EncodeOnlyCross/" + name).c_str(),
                                 BM_EncodeOnly)
        ->Args({idx, 0})
        ->Unit(benchmark::kMillisecond)
        ->MinTime(2.0);

    benchmark::RegisterBenchmark(("EncodeOnlySame/" + name).c_str(),
                                 BM_EncodeOnly)
        ->Args({idx, 1})
        ->Unit(benchmark::kMillisecond)
        ->MinTime(2.0);

    benchmark::RegisterBenchmark(("CrossConvert/" + name).c_str(), BM_Convert)
        ->Args({idx, 0})
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("RoundTripSame/" + name).c_str(), BM_Convert)
        ->Args({idx, 1})
        ->Unit(benchmark::kMillisecond)
        ->MinTime(5.0);

    benchmark::RegisterBenchmark(("CrossConvertGzip/" + name).c_str(),
                                 BM_Convert)
        ->Args({idx, 2})
        ->Unit(benchmark::kMillisecond)
        ->MinTime(3.0);

    benchmark::RegisterBenchmark(("CrossConvertStaged/" + name).c_str(),
                                 BM_ConvertStaged)
        ->Args({idx, 0})
        ->Unit(benchmark::kMillisecond)
        ->MinTime(3.0);

    benchmark::RegisterBenchmark(("RoundTripStaged/" + name).c_str(),
                                 BM_ConvertStaged)
        ->Args({idx, 1})
        ->Unit(benchmark::kMillisecond)
        ->MinTime(3.0);
  }

  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return all_ok ? 0 : 1;
}