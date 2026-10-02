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

#ifndef FSCHEMA_TESTS_TESTDATA_UTIL_H_
#define FSCHEMA_TESTS_TESTDATA_UTIL_H_

#include <cstdlib>
#include <filesystem>

#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace fschema::test {

[[nodiscard]] inline std::filesystem::path GetTestDataDir() {
  namespace fs = std::filesystem;

  const char* dir = FASTSCHEMA_TESTDATA_DIR;

  if (const char* srcdir = std::getenv("TEST_SRCDIR")) {
    fs::path base(srcdir);
    if (const char* ws = std::getenv("TEST_WORKSPACE")) base /= ws;
    return base / dir;
  }

  if (const char* runfiles = std::getenv("RUNFILES_DIR")) {
    fs::path base(runfiles);
    if (const char* ws = std::getenv("TEST_WORKSPACE"))
      base /= ws;
    else
      base /= "FastSchema";
    return base / dir;
  }

  return fs::path(dir);
}

}  // namespace fschema::test

#endif  // FSCHEMA_TESTS_TESTDATA_UTIL_H_