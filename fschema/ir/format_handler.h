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

#ifndef FSCHEMA_IR_FORMAT_HANDLER_H_
#define FSCHEMA_IR_FORMAT_HANDLER_H_

#include <expected>
#include <memory>
#include <string_view>
#include <vector>

#include "absl/synchronization/mutex.h"

#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/ir/types.h"

namespace fschema::ir::format {

struct EncodeOptions {
  std::int32_t target_version = 0;

  // Strategy for handling multiple regions
  // (applies when IR contains multiple regions and target is Schem format)
  enum class MultiRegionStrategy : std::uint8_t {
    kStrictSingle = 0,      // FAIL, delegate to downstream
    kExtractFirst = 1,      // Extract the first region
    kExtractByName = 2,     // Extract the region with the specified name
    kMergeBoundingBox = 3,  // Merge AABBs and pad with air
  };
  MultiRegionStrategy multi_region = MultiRegionStrategy::kStrictSingle;
  // Active when strategy is set to kExtractByName
  std::string_view extract_region_name;

  // Filler block (active when strategy is kMergeBoundingBox)
  std::string_view fill_block = "minecraft:air";

  // Whether to compress the output
  bool compress = false;
};

class FormatHandler {
 public:
  virtual ~FormatHandler() = default;
  [[nodiscard]] virtual std::string_view id() const noexcept = 0;
  [[nodiscard]] virtual SourceFormat format() const noexcept = 0;

  [[nodiscard]] virtual ParseResult<Schema> Decode(
      std::unique_ptr<std::vector<std::byte>> bytes,
      const base::DecodeLimits& limits) const = 0;

  [[nodiscard]] virtual ParseResult<std::vector<std::byte>> Encode(
      const Schema& ir,
      const EncodeOptions& options) const = 0;
};

class FormatRegistry {
 public:
  [[nodiscard]] static FormatRegistry& Instance() noexcept;
  void Register(std::unique_ptr<FormatHandler> handler);
  [[nodiscard]] const FormatHandler* Find(SourceFormat fmt) const noexcept;

  ~FormatRegistry();

 private:
  FormatRegistry();
  struct Impl;
  std::unique_ptr<Impl> impl_;
  absl::Mutex register_mutex_;
};

// High-level Convenience APIs
[[nodiscard]] ParseResult<Schema> DecodeFromFormat(
    SourceFormat fmt,
    std::unique_ptr<std::vector<std::byte>> bytes,
    const base::DecodeLimits& limits = {});

[[nodiscard]] ParseResult<std::vector<std::byte>> EncodeToFormat(
    SourceFormat fmt,
    const Schema& ir,
    const EncodeOptions& options = {});

[[nodiscard]] ParseResult<std::vector<std::byte>> Convert(
    SourceFormat from,
    SourceFormat to,
    std::unique_ptr<std::vector<std::byte>> bytes,
    const EncodeOptions& options = {},
    const base::DecodeLimits& limits = {});

}  // namespace fschema::ir::format

#endif  // FSCHEMA_IR_FORMAT_HANDLER_H_