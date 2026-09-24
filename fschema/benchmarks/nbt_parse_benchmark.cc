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
#include <span>
#include <string>
#include <vector>

#include "fschema/base/decompression.h"
#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/base/nbt_parse.h"
#include "fschema/tests/testdata_util.h"
#include "hwy/targets.h"

namespace fb = fschema::base;

#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

[[nodiscard]] std::string ToString(fschema::ParseError::Code code) {
  using C = fschema::ParseError::Code;
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

struct TestFile {
  std::string filename;
  std::vector<std::byte> bytes;
};

[[nodiscard]] std::vector<std::byte> ReadFileRaw(
    const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f) {
    return {};
  }
  auto size = f.tellg();
  if (size <= 0) {
    return {};
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  f.seekg(0);
  f.read(reinterpret_cast<char*>(bytes.data()), size);
  return bytes;
}

[[nodiscard]] std::vector<TestFile> LoadTestFiles() {
  std::vector<TestFile> out;
  const std::filesystem::path dir = fschema::test::GetTestDataDir();

  if (!std::filesystem::exists(dir)) {
    std::cerr << "  [warn] Samples directory not found: " << dir << "\n";
    return out;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    auto path = entry.path();
    std::string filename = path.filename().string();

    if (path.extension() == ".litematic") {
      continue;
    }

    auto r = fb::DecompressGzipFile(path);
    if (r) {
      out.push_back({filename, std::move(*r)});
    } else {
      // Not gzip-compressed or decompression failed — fall back to raw bytes.
      auto raw_data = ReadFileRaw(path);
      if (raw_data.empty()) {
        std::cerr << "  [warn] Read error or empty file: " << filename << "\n";
        continue;
      }
      out.push_back({filename, std::move(raw_data)});
    }
  }
  return out;
}

static std::vector<TestFile> files = LoadTestFiles();

void PrintErrorFn(const fschema::ParseError& e) {
  std::cerr << "  [ParseError] " << ToString(e.code) << " at path=\"" << e.path
            << "\" offset=" << e.offset << "\n";
}

static void BM_PureNbtParse(benchmark::State& st) {
  const auto& tf = files[st.range(0)];
  const auto bytes_size = tf.bytes.size();
  std::span<const std::byte> byte_span(tf.bytes);
  fb::DecodeLimits limits;

  for (auto _ : st) {
    fb::ByteReader reader(byte_span, limits);
    auto result = fb::ParseNbt(reader);
    if (!result) {
      PrintErrorFn(result.error());
      st.SkipWithError("parse failed");
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
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  std::cout << "Highway supported: 0x" << std::hex << hwy::SupportedTargets()
            << std::dec << "\n";

  bool all_ok = true;
  for (std::size_t i = 0; i < files.size(); ++i) {
    const auto& tf = files[i];
    std::cout << "\n══════ Verify: " << tf.filename << " ════\n";

    std::span<const std::byte> byte_span(tf.bytes);
    fb::DecodeLimits limits;
    fb::ByteReader reader(byte_span, limits);

    auto r = fb::ParseNbt(reader);
    if (!r) {
      PrintErrorFn(r.error());
      all_ok = false;
      continue;
    }

    std::cout << "  Bytes Consumed: " << reader.pos() << " / "
              << tf.bytes.size() << "\n";
    if (reader.pos() != tf.bytes.size()) {
      std::cerr << "  [warn] Reader did not consume all bytes!\n";
      all_ok = false;
    }
  }
  std::cout << "Verify: " << (all_ok ? "PASS" : "FAIL") << "\n\n";

  ::benchmark::Initialize(&argc, argv);

  for (std::size_t i = 0; i < files.size(); ++i) {
    benchmark::RegisterBenchmark(("PureNbtParse/" + files[i].filename).c_str(),
                                 BM_PureNbtParse)
        ->Arg(i)
        ->Unit(benchmark::kMicrosecond)
        ->MinTime(2.0);
  }

  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return all_ok ? 0 : 1;
}