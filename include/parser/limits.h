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

#ifndef FSCHEMA_PARSER_LIMITS_H_
#define FSCHEMA_PARSER_LIMITS_H_

#include <cstddef>
#include <cstdint>

namespace fschema::parser {

  struct DecodeLimits {
    // Input
    std::size_t max_input_bytes = 1ULL << 30;       // 1 GiB After compression
    std::size_t max_decompressed = 4ULL << 30;      // 4 GiB After decompression

    // NBT Tree
    std::size_t max_nbt_depth = 64;
    std::size_t max_string_bytes = 1ULL << 16;      // 64 KiB per string
    std::size_t max_array_elements = 1ULL << 28;    // 256M per array tag

    // Litematic
    std::size_t max_regions = 64;
    std::uint64_t max_volume_per_region = 1ULL << 28; // 256M blocks
    std::size_t max_palette_size = 1ULL << 16;        // 65536 (2^16)
    std::size_t max_entities = 1ULL << 18;            // 262144 (2^18)
    std::size_t max_tile_entities = 1ULL << 20;       // 1048576 (2^20)
    std::size_t max_pending_ticks = 1ULL << 16;       // 65536 (2^16)
  };

} // namespace fschema::parser

#endif // FSCHEMA_PARSER_LIMITS_H_