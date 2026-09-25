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

#ifndef FSCHEMA_MEMORY_NOINIT_ALLOCATOR_H_
#define FSCHEMA_MEMORY_NOINIT_ALLOCATOR_H_

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

#include "fschema/memory/arena.h"

namespace fschema::memory {

template <typename T>
class NoInitVector {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = T*;
  using const_iterator = const T*;

  NoInitVector() noexcept = default;

  explicit NoInitVector(size_type count) { allocate_direct(count); }

  NoInitVector(size_type count, Arena* arena) {
    if (arena) {
      allocate_from_arena(count, arena);
    } else {
      allocate_direct(count);
    }
  }

  NoInitVector(size_type count, Arena& arena) : NoInitVector(count, &arena) {}

  NoInitVector(NoInitVector&& o) noexcept
      : data_(o.data_), size_(o.size_), arena_(o.arena_) {
    o.data_ = nullptr;
    o.size_ = 0;
    o.arena_ = nullptr;
  }

  NoInitVector& operator=(NoInitVector&& o) noexcept {
    if (this != &o) {
      release();
      data_ = o.data_;
      size_ = o.size_;
      arena_ = o.arena_;
      o.data_ = nullptr;
      o.size_ = 0;
      o.arena_ = nullptr;
    }
    return *this;
  }

  NoInitVector(const NoInitVector&) = delete;
  NoInitVector& operator=(const NoInitVector&) = delete;

  ~NoInitVector() { release(); }

  void resize_uninitialized(size_type count, Arena* arena) {
    release();
    if (arena) {
      allocate_from_arena(count, arena);
    } else {
      allocate_direct(count);
    }
  }

  [[nodiscard]] pointer data() noexcept { return data_; }
  [[nodiscard]] const_pointer data() const noexcept { return data_; }
  [[nodiscard]] size_type size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  reference operator[](size_type i) { return data_[i]; }
  const_reference operator[](size_type i) const { return data_[i]; }

  iterator begin() noexcept { return data_; }
  iterator end() noexcept { return data_ + size_; }
  const_iterator begin() const noexcept { return data_; }
  const_iterator end() const noexcept { return data_ + size_; }

 private:
  static constexpr std::size_t kAlign = (alignof(T) < 64) ? 64 : alignof(T);

  void allocate_from_arena(size_type count, Arena* arena) {
    if (count == 0) return;
    arena_ = arena;
    size_ = count;
    data_ = arena_->AllocateArray<T>(count);
  }

  void allocate_direct(size_type count) {
    if (count == 0) return;
    size_ = count;
    data_ = static_cast<T*>(
        ::operator new(count * sizeof(T), std::align_val_t(kAlign)));
  }

  void release() noexcept {
    if (!arena_ && data_) {
      ::operator delete(data_, std::align_val_t(kAlign));
    }
    data_ = nullptr;
    size_ = 0;
    arena_ = nullptr;
  }

  T* data_ = nullptr;
  size_type size_ = 0;
  Arena* arena_ = nullptr;
};

}  // namespace fschema::memory

#endif  // FSCHEMA_MEMORY_NOINIT_ALLOCATOR_H_