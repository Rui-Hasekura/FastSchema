/*
 * Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FSCHEMA_TESTS_TESTDATA_UTIL_H_
#define FSCHEMA_TESTS_TESTDATA_UTIL_H_

#include <cstdlib>
#include <filesystem>
#include <string>

namespace fschema::test {

// Resolves FASTSCHEMA_TESTDATA_DIR for both CMake and Bazel.
//
// CMake:  FASTSCHEMA_TESTDATA_DIR is an absolute path → use directly.
// Bazel: FASTSCHEMA_TESTDATA_DIR is a workspace-relative path →
//        resolve against the runfiles root ($TEST_SRCDIR/$TEST_WORKSPACE
//        for cc_test, $RUNFILES_DIR/$TEST_WORKSPACE for cc_binary).
[[nodiscard]] inline std::filesystem::path GetTestDataDir() {
  namespace fs = std::filesystem;

  const char* dir = FASTSCHEMA_TESTDATA_DIR;

  // Bazel cc_test — TEST_SRCDIR is set automatically.
  if (const char* srcdir = std::getenv("TEST_SRCDIR")) {
    fs::path base(srcdir);
    if (const char* ws = std::getenv("TEST_WORKSPACE")) base /= ws;
    return base / dir;
  }

  // Bazel cc_binary (bazel run) — RUNFILES_DIR is set.
  if (const char* runfiles = std::getenv("RUNFILES_DIR")) {
    fs::path base(runfiles);
    if (const char* ws = std::getenv("TEST_WORKSPACE")) base /= ws;
    // TEST_WORKSPACE may not be set for cc_binary; fall back to module name.
    else
      base /= "FastSchema";
    return base / dir;
  }

  // CMake — absolute path baked in at compile time.
  return fs::path(dir);
}

}  // namespace fschema::test

#endif  // FSCHEMA_TESTS_TESTDATA_UTIL_H_