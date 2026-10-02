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

#ifndef FSCHEMA_BASE_HASH_H_
#define FSCHEMA_BASE_HASH_H_

#include <cstdint>

#define XXH_IMPLEMENTATION
#define XXH_INLINE_ALL
#include "fschema/base/port.h"
#include "fschema/third_party/xxhash/xxhash.h"

namespace fschema::base {

FSCHEMA_ALWAYS_INLINE uint64_t FastHash64(const void* data,
                                          size_t len,
                                          uint64_t seed = 0) {
  return XXH64(data, len, seed);
}

FSCHEMA_ALWAYS_INLINE uint64_t FastHash3_64(const void* data,
                                            size_t len,
                                            uint64_t seed = 0) {
  return XXH3_64bits_withSeed(data, len, seed);
}

}  // namespace fschema::base

#endif  // FSCHEMA_BASE_HASH_H_