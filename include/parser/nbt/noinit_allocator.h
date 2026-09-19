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

#ifndef FSCHEMA_PARSER_NBT_NOINIT_ALLOCATOR_H_
#define FSCHEMA_PARSER_NBT_NOINIT_ALLOCATOR_H_

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

namespace fschema::parser::nbt {

  // ONLY use with trivially copyable scalar types (e.g., uint32_t).
  // DO NOT READ BEFORE WRITING!
  // ONLY read after fully overwriting the allocated range.
  template <typename T>
  struct NoInitAllocator {
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;

    static constexpr std::size_t kAllocAlign =
      (alignof(T) < 64) ? std::size_t{ 64 } : alignof(T);

    NoInitAllocator() = default;
    template <typename U>
    NoInitAllocator(const NoInitAllocator<U>&) noexcept;

    template <typename U>
    struct rebind {
      using other = NoInitAllocator<U>;
    };

    [[nodiscard]] T* allocate(std::size_t n);
    void deallocate(T* p, std::size_t /*n*/) noexcept;

    // Skip construction to avoid zero-initialization during resize.
    template <typename U>
    void construct(U*) noexcept;
    template <typename U, typename... Args>
    void construct(U*, Args&&... /*args*/) noexcept;

    template <typename U>
    void destroy(U*) noexcept;
  };

  template <typename T, typename U>
  [[nodiscard]] bool operator==(
    const NoInitAllocator<T>&, const NoInitAllocator<U>&) noexcept;

} // namespace fschema::parser::nbt

#include "parser/nbt/noinit_allocator-inl.h"

#endif // FSCHEMA_PARSER_NBT_NOINIT_ALLOCATOR_H_