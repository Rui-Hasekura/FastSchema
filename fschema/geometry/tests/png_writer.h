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

#ifndef FSCHEMA_GEOMETRY_TESTS_PNG_WRITER_H_
#define FSCHEMA_GEOMETRY_TESTS_PNG_WRITER_H_

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "fschema/geometry/internal/png_encode.h"

namespace fschema::tests {

// Thin wrappers over the production PNG encoder for test fixtures.
inline std::vector<std::uint8_t> SerializePng(std::uint32_t width,
                                              std::uint32_t height,
                                              const std::uint8_t* rgba) {
  return fschema::geometry::internal::EncodePng(
      width,
      height,
      std::span<const std::uint8_t>(
          rgba, static_cast<std::size_t>(width) * height * 4));
}

inline void WritePngFile(const std::filesystem::path& path,
                         std::uint32_t width,
                         std::uint32_t height,
                         const std::uint8_t* rgba) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  const auto bytes = SerializePng(width, height, rgba);
  f.write(reinterpret_cast<const char*>(bytes.data()),
          static_cast<std::streamsize>(bytes.size()));
}

}  // namespace fschema::tests

#endif  // FSCHEMA_GEOMETRY_TESTS_PNG_WRITER_H_
