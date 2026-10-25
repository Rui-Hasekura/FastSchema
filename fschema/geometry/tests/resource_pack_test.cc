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

#include "fschema/geometry/resource_pack.h"

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "fschema/base/error.h"
#include "fschema/geometry/tests/geometry_test_util.h"

namespace {

using fschema::ParseError;
using fschema::ToString;
using fschema::geometry::ResourcePack;
using fschema::tests::TempDir;
using fschema::tests::WriteFile;

// Minimal synthetic pack: just the directory layout Load requires, with
// one trivial blockstate file so subsequent Resolve / LoadTexturePixels
// tests (in their own test files) can find at least one entry.
struct SyntheticPack {
  TempDir root;
  std::filesystem::path stone_blockstate;
  std::filesystem::path cube_all_model;

  SyntheticPack() {
    stone_blockstate =
        root.Get() / "assets" / "minecraft" / "blockstates" / "stone.json";
    cube_all_model = root.Get() / "assets" / "minecraft" / "models" / "block" /
                     "cube_all.json";

    WriteFile(stone_blockstate,
              R"({"variants": {"": {"model": "block/cube_all"}}})");
    WriteFile(
        cube_all_model,
        R"({"parent": "block/cube", "textures": {"all": "block/stone"}})");
  }
};

TEST(ResourcePackTest, LoadFailsOnNonexistentPath) {
  auto r = ResourcePack::Load("/this/path/does/not/exist");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kResourcePackNotFound);
  EXPECT_FALSE(r.error().path.empty());
}

TEST(ResourcePackTest, LoadFailsOnMissingAssetsMinecraft) {
  TempDir empty;
  auto r = ResourcePack::Load(empty.Get().generic_string());
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kResourcePackNotFound);
  EXPECT_NE(r.error().path.find("assets/minecraft"), std::string::npos);
}

TEST(ResourcePackTest, LoadSucceedsOnValidDirectory) {
  SyntheticPack sp;
  auto r = ResourcePack::Load(sp.root.Get().generic_string());
  ASSERT_TRUE(r.has_value())
      << "expected success; got error: " << ToString(r.error());
  EXPECT_NE((*r).get(), nullptr);
}

TEST(ResourcePackTest, LoadRejectsNonDirectoryPath) {
  TempDir root;
  auto zip_path = root.Get() / "fake.zip";
  WriteFile(zip_path, "PK\x03\x04");
  auto r = ResourcePack::Load(zip_path.generic_string());
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ParseError::Code::kResourcePackNotFound);
}

}  // namespace
