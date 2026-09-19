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

#ifndef FSCHEMA_PARSER_NBT_NOINIT_ALLOCATOR_INL_H_
#define FSCHEMA_PARSER_NBT_NOINIT_ALLOCATOR_INL_H_

#include "parser/nbt/noinit_allocator.h"

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

namespace fschema::parser::nbt {

  template <typename T>
  template <typename U>
  NoInitAllocator<T>::NoInitAllocator(const NoInitAllocator<U>&) noexcept {}

  template <typename T>
  [[nodiscard]] T* NoInitAllocator<T>::allocate(std::size_t n) {
    if (n == 0) return nullptr;
    if (n > static_cast<std::size_t>(-1) / sizeof(T)) {
      throw std::bad_alloc{};
    }
    // Force kAllocAlign alignment.
    return static_cast<T*>(::operator new(n * sizeof(T),
      std::align_val_t(kAllocAlign)));
  }

  template <typename T>
  void NoInitAllocator<T>::deallocate(T* p, std::size_t /*n*/) noexcept {
    // Match the alignment value used in allocate.
    ::operator delete(p, std::align_val_t(kAllocAlign));
  }

  // Skip construction to avoid zero-initialization during resize.
  template <typename T>
  template <typename U>
  void NoInitAllocator<T>::construct(U*) noexcept {
    static_assert(std::is_trivially_default_constructible_v<U>,
      "NoInitAllocator: only for trivially constructible types");
  }

  template <typename T>
  template <typename U, typename... Args>
  void NoInitAllocator<T>::construct(U*, Args&&... /*args*/) noexcept {
    static_assert(std::is_trivially_default_constructible_v<U>,
      "NoInitAllocator: only for trivially constructible types");
  }

  template <typename T>
  template <typename U>
  void NoInitAllocator<T>::destroy(U*) noexcept {
    static_assert(std::is_trivially_destructible_v<U>,
      "NoInitAllocator: only for trivially destructible types");
  }

  template <typename T, typename U>
  [[nodiscard]] bool operator==(
    const NoInitAllocator<T>&, const NoInitAllocator<U>&) noexcept {
    return true;
  }

} // namespace fschema::parser::nbt

#endif // FSCHEMA_PARSER_NBT_NOINIT_ALLOCATOR_INL_H_