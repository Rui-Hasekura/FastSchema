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

#ifndef FSCHEMA_MEMORY_ARENA_H_
#define FSCHEMA_MEMORY_ARENA_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>
#include <vector>

namespace fschema::memory {

class Arena {
 public:
  static constexpr std::size_t kAlign = 64;
  static constexpr std::size_t kDefaultSlab = 1ULL << 26;
  static constexpr std::size_t kMaxSlab = 1ULL << 32;
  static constexpr std::size_t kPage = 4096;
  static constexpr std::size_t kLargePage = 2ULL * 1024 * 1024;

  Arena() = default;
  explicit Arena(std::size_t) {}
  ~Arena() { Release(); }

  Arena(Arena&& o) noexcept;
  Arena& operator=(Arena&& o) noexcept;
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  void Reset() noexcept;

  [[nodiscard]] void* Allocate(std::size_t bytes, std::size_t align = kAlign);

  template <typename T>
  [[nodiscard]] T* AllocateArray(std::size_t n, std::size_t align = kAlign) {
    return static_cast<T*>(
        Allocate(n * sizeof(T), std::max<std::size_t>(align, alignof(T))));
  }

  [[nodiscard]] std::size_t bytes_used() const noexcept;

  [[nodiscard]] std::size_t slab_count() const noexcept;

  [[nodiscard]] bool has_large_pages() const noexcept;

  void Release() noexcept;

 private:
  enum class SlabSource : std::uint8_t {
    kAligned,
    kLargePage,
  };

  struct Slab {
    std::byte* base;
    std::size_t size;
    std::size_t used;
    SlabSource source;
  };

  std::vector<Slab> slabs_;
  struct Cursor {
    std::byte* base;
    std::size_t size;
    std::size_t used;
  } cursor_{};

  [[nodiscard]] void* TryBumpCurrent(std::size_t bytes, std::size_t align);

  void GrowFor(std::size_t bytes, std::size_t align);

  void NewSlab(std::size_t size);

  static void FreeSlab(Slab& s) noexcept;
};

}  // namespace fschema::memory

#endif  // FSCHEMA_MEMORY_ARENA_H_