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

#ifndef FSCHEMA_GEOMETRY_TESTS_GEOMETRY_TEST_UTIL_H_
#define FSCHEMA_GEOMETRY_TESTS_GEOMETRY_TEST_UTIL_H_

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "fschema/geometry/internal/cull.h"
#include "fschema/geometry/tests/png_writer.h"
#include "fschema/geometry/types.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"

namespace fschema::tests {

// Write `contents` to `path`, creating parent directories as needed.
// Fails the calling test if the file cannot be opened or written.
inline void WriteFile(const std::filesystem::path& path,
                      std::string_view contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(f.good()) << "failed to open " << path;
  f.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  ASSERT_TRUE(f.good()) << "failed to write " << path;
}

// RAII temp directory rooted at the OS temp dir, unique per instance.
// Removed on destruction.
class TempDir {
 public:
  TempDir() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> dist(0, 100'000'000);
    path_ = std::filesystem::temp_directory_path() /
            ("fschema_geometry_test_" + std::to_string(dist(gen)));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  [[nodiscard]] const std::filesystem::path& Get() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

// Minimal synthetic resource pack containing a single "stone" block:
//   blockstates/stone.json     -> cube_all model
//   models/block/cube_all.json -> parent cube, textures all = block/stone
//   models/block/cube.json     -> 6-face cube, texture resolved via #all
//   textures/block/stone.png   -> 16x16 RGBA, every pixel (0xAA,0xBB,0xCC,0xDD)
struct ResourcePackFixture {
  TempDir root;

  ResourcePackFixture() {
    const auto base = root.Get();
    WriteFile(base / "assets" / "minecraft" / "blockstates" / "stone.json",
              R"({"variants": {"": {"model": "block/cube_all"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "cube_all.json",
        R"({"parent": "block/cube", "textures": {"all": "block/stone"}})");
    WriteFile(base / "assets" / "minecraft" / "models" / "block" / "cube.json",
              R"({"textures": {"particle": "block/stone"},)"
              R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
              R"( "faces": {)"
              R"(   "north": {"texture": "#all", "uv": [0,0,16,16]},)"
              R"(   "south": {"texture": "#all", "uv": [0,0,16,16]},)"
              R"(   "east":  {"texture": "#all", "uv": [0,0,16,16]},)"
              R"(   "west":  {"texture": "#all", "uv": [0,0,16,16]},)"
              R"(   "up":    {"texture": "#all", "uv": [0,0,16,16]},)"
              R"(   "down":  {"texture": "#all", "uv": [0,0,16,16]})"
              R"( }}]})");
    std::vector<std::uint8_t> rgba(16 * 16 * 4);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
      rgba[i + 0] = 0xAA;
      rgba[i + 1] = 0xBB;
      rgba[i + 2] = 0xCC;
      rgba[i + 3] = 0xDD;
    }
    WritePngFile(
        base / "assets" / "minecraft" / "textures" / "block" / "stone.png",
        16,
        16,
        rgba.data());
  }
};

// Synthetic materialized ir::Region of the given size, filled entirely
// with stone. Palette = [air, stone]; every block index points to slot 1.
struct StoneRegion {
  std::unique_ptr<memory::Arena> arena = std::make_unique<memory::Arena>();
  ir::Region region{};

  StoneRegion(std::int32_t sx, std::int32_t sy, std::int32_t sz) {
    region.bounds =
        ir::BoundingBox{.origin = {0, 0, 0}, .size = {sx, sy, sz}};
    region.palette = {
        ir::BlockState{.name = "minecraft:air",
                       .prop_encoding = ir::PropertyEncoding::kNone},
        ir::BlockState{.name = "minecraft:stone",
                       .prop_encoding = ir::PropertyEncoding::kNone},
    };
    region.is_air_lut.assign(2, 0);
    region.is_air_lut[0] = 1;
    const std::size_t vol = static_cast<std::size_t>(sx) * sy * sz;
    region.block_indices =
        memory::UnInitBuffer<std::uint16_t>(vol, arena.get());
    std::uint16_t* data =
        reinterpret_cast<std::uint16_t*>(region.block_indices.data());
    for (std::size_t i = 0; i < vol; ++i) data[i] = 1;
    region.is_materialized = true;
    region.lazy_source.encoding = ir::BlockDataEncoding::kNone;
  }
};

// Build a BlockGroupTable from raw group bytes (0=air, 1=opaque,
// 2+=transparent group). Avoids constructing a full ir::Region for
// low-level cull / AO tests.
inline geometry::internal::BlockGroupTable BuildGroupTable(
    const std::vector<std::uint8_t>& groups) {
  geometry::internal::BlockGroupTable t;
  t.bytes = groups;
  return t;
}

}  // namespace fschema::tests

#endif  // FSCHEMA_GEOMETRY_TESTS_GEOMETRY_TEST_UTIL_H_
