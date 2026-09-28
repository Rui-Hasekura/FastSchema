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

#include "fschema/ir/format_handler.h"

#include <array>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "fschema/base/compressor.h"

namespace fschema::ir::format {

struct FormatRegistry::Impl {
  std::array<FormatHandler*, 256> table{};
  std::vector<std::unique_ptr<FormatHandler>> handlers;
};

FormatRegistry::FormatRegistry() : impl_(std::make_unique<Impl>()) {}
FormatRegistry::~FormatRegistry() = default;

FormatRegistry& FormatRegistry::Instance() noexcept {
  static FormatRegistry instance;
  return instance;
}

void FormatRegistry::Register(std::unique_ptr<FormatHandler> handler) {
  if (!handler) return;
  const SourceFormat fmt = handler->format();
  impl_->table[static_cast<std::size_t>(fmt)] = handler.get();
  impl_->handlers.push_back(std::move(handler));
}

const FormatHandler* FormatRegistry::Find(SourceFormat fmt) const noexcept {
  return impl_->table[static_cast<std::size_t>(fmt)];
}

// High-level Convenience API Implementation
ParseResult<Schema> DecodeFromFormat(
    SourceFormat fmt,
    std::unique_ptr<std::vector<std::byte>> bytes,
    const base::DecodeLimits& limits) {
  const FormatHandler* h = FormatRegistry::Instance().Find(fmt);
  if (!h) {
    return std::unexpected(ParseError{
        ParseError::Code::UnsupportedVersion, "FormatHandlerNotFound", 0});
  }
  if (!bytes || bytes->empty()) {
    return std::unexpected(
        ParseError{ParseError::Code::Truncated, "EmptyInputBuffer", 0});
  }

  if (bytes->size() >= 2 && (*bytes)[0] == std::byte{0x1F} &&
      (*bytes)[1] == std::byte{0x8B}) {
    auto decompressed =
        base::DecompressGzip(std::span<const std::byte>(*bytes));
    if (!decompressed) {
      return std::unexpected(
          ParseError{ParseError::Code::Truncated, "AutoDecompressFailed", 0});
    }
    *bytes = std::move(*decompressed);
  }

  return h->Decode(std::move(bytes), limits);
}

ParseResult<std::vector<std::byte>> EncodeToFormat(
    SourceFormat fmt,
    const Schema& ir,
    const EncodeOptions& options) {
  const FormatHandler* h = FormatRegistry::Instance().Find(fmt);
  if (!h) {
    return std::unexpected(ParseError{
        ParseError::Code::UnsupportedVersion, "FormatHandlerNotFound", 0});
  }

  auto result = h->Encode(ir, options);
  if (!result) return std::unexpected(result.error());

  if (options.compress) {
    auto compressed = base::CompressGzip(std::span<const std::byte>(*result));
    if (!compressed) {
      return std::unexpected(
          ParseError{ParseError::Code::Truncated, "CompressFailed", 0});
    }
    return *compressed;
  }
  return result;
}

ParseResult<std::vector<std::byte>> Convert(
    SourceFormat from,
    SourceFormat to,
    std::unique_ptr<std::vector<std::byte>> bytes,
    const EncodeOptions& options,
    const base::DecodeLimits& limits) {
  // 1. Decode from source format to IR
  auto ir_result = DecodeFromFormat(from, std::move(bytes), limits);
  if (!ir_result) {
    return std::unexpected(ir_result.error());
  }

  // 2. (Optional) Pre-encode validations could go here if needed,
  //    but handler's Encode should cover version guards.

  // 3. Encode from IR to target format
  return EncodeToFormat(to, *ir_result, options);
}
}  // namespace fschema::ir::format