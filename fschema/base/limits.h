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

#ifndef FSCHEMA_BASE_LIMITS_H_
#define FSCHEMA_BASE_LIMITS_H_

#include <cstdint>

namespace fschema::base {

/// Security and safety limits for decoding schematic files.
///
/// These limits prevent malicious or malformed inputs from causing excessive
/// memory allocations or stack overflows.
/// They act as hard guards during the parsing phase.
/// If a limit is exceeded, a `ParseError` (usually
/// `OversizedPayload` or `DepthLimitExceeded`) is returned immediately.
struct DecodeLimits {
  // Input / Memory Bounds

  // Max compressed file size: 1 GiB.
  std::size_t max_input_bytes = 1ULL << 30;
  // Max decompressed NBT payload: 4 GiB.
  std::size_t max_decompressed = 4ULL << 30;

  // NBT Tree Structure

  // Max nesting depth for compounds/lists (DoS / stack overflow guard).
  std::size_t max_nbt_depth = 64;
  // Max size per NBT String tag: 64 KiB (NBT spec limit).
  std::size_t max_string_bytes = 1ULL << 16;
  // Max elements per ByteArray/IntArray/LongArray tag: 256M.
  std::size_t max_array_elements = 1ULL << 28;

  // Max elements per NBT List tag in the raw NBT tree: 8M.
  // Note: Maybe it's not enough, need feedback.
  std::size_t max_nbt_list_elements = 1ULL << 23;

  // Litematic / Schematic Domain Limits

  /// Max number of regions in a single Litematic file.
  std::size_t max_regions = 64;

  /// Max blocks per region (256M). Prevents 64-bit volume overflow and limits
  /// the maximum memory footprint of materialized `block_indices`
  /// (256M * 2 bytes = 512 MiB per region).
  std::uint64_t max_volume_per_region = 1ULL << 28;

  /// Max entries in a block palette. Hard limit imposed by the `std::uint16_t`
  /// indexing used in the materialized `block_indices` array.
  std::size_t max_palette_size = 1ULL << 16;  // 65536 (2^16)

  /// Max entities per region.
  std::size_t max_entities = 1ULL << 18;  // 262144 (2^18)

  /// Max block entities (tile entities) per region.
  std::size_t max_tile_entities = 1ULL << 20;  // 1048576 (2^20)

  /// Max pending block/fluid ticks per region.
  std::size_t max_pending_ticks = 1ULL << 16;  // 65536 (2^16)
};

}  // namespace fschema::base

#endif  // FSCHEMA_BASE_LIMITS_H_