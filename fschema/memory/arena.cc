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

#include "fschema/memory/arena.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#elif defined(__linux__)
#  include <sys/mman.h>
#  include <unistd.h>
#endif

namespace {

static std::size_t NextPow2(std::size_t x) noexcept {
  --x;
  for (int i = 1; i < static_cast<int>(sizeof(std::size_t) * 8); i <<= 1)
    x |= x >> i;
  return ( x + 1 );
}

#if defined(_WIN32)
[[nodiscard]] static bool EnableLockMemoryPrivilege() noexcept {
  static bool checked = false;
  static bool ok = false;
  if (checked) return ok;
  checked = true;

  HANDLE hToken;
  if (!::OpenProcessToken(::GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &hToken)) {
    return ok = false;
  }

  LUID luid;
  if (!::LookupPrivilegeValueA(nullptr, "SeLockMemoryPrivilege", &luid)) {
    ::CloseHandle(hToken);
    return ok = false;
  }

  TOKEN_PRIVILEGES tp{};
  tp.PrivilegeCount = 1;
  tp.Privileges[0].Luid = luid;
  tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

  BOOL adj = ::AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
  DWORD err = ::GetLastError();
  ::CloseHandle(hToken);

  return ok = (adj && err == ERROR_SUCCESS);
}

[[nodiscard]] static void* TryLargePageAlloc(std::size_t size) noexcept {
  if (!EnableLockMemoryPrivilege()) return nullptr;

  std::size_t page_min = ::GetLargePageMinimum();
  if (page_min == 0) return nullptr;

  std::size_t large_size = (size + page_min - 1) & ~(page_min - 1);

  void* p = ::VirtualAlloc(nullptr,
                           large_size,
                           MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                           PAGE_READWRITE);

  return p;
}
#endif  // _WIN32

}  // namespace

namespace fschema::memory {

Arena::Arena(Arena&& o) noexcept
    : slabs_(std::move(o.slabs_)), cursor_(o.cursor_) {
  o.cursor_ = {};
}

Arena& Arena::operator=(Arena&& o) noexcept {
  Release();
  slabs_ = std::move(o.slabs_);
  cursor_ = o.cursor_;
  o.cursor_ = {};
  return *this;
}

void Arena::Reset() noexcept {
  for (auto& s : slabs_) s.used = 0;
  if (!slabs_.empty()) {
    cursor_ = {slabs_[0].base, slabs_[0].size, 0};
  } else {
    cursor_ = {};
  }
}

[[nodiscard]] void* Arena::Allocate(std::size_t bytes, std::size_t align) {
  if (bytes == 0) return nullptr;
  if (align > kAlign) align = kAlign;
  align = std::max<std::size_t>(align, 1);
  align = (align & (align - 1)) ? NextPow2(align) : align;

  void* p = TryBumpCurrent(bytes, align);
  if (p) return p;

  for (auto& s : slabs_) {
    Cursor c{s.base, s.size, s.used};
    std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(c.base + c.used);
    std::uintptr_t aligned = (raw + align - 1) & ~(align - 1);
    std::size_t pad = aligned - raw;
    if (c.used + pad + bytes <= c.size) {
      c.used += pad + bytes;
      s.used = c.used;
      cursor_ = c;
      return reinterpret_cast<void*>(aligned);
    }
  }

  GrowFor(bytes, align);
  return TryBumpCurrent(bytes, align);
}

[[nodiscard]] std::size_t Arena::bytes_used() const noexcept {
  std::size_t total = 0;
  for (const auto& s : slabs_) total += s.used;
  return total;
}

[[nodiscard]] std::size_t Arena::slab_count() const noexcept {
  return slabs_.size();
}

[[nodiscard]] bool Arena::has_large_pages() const noexcept {
  for (const auto& s : slabs_)
    if (s.source == SlabSource::kLargePage) return true;
  return false;
}

void Arena::Release() noexcept {
  for (auto& s : slabs_) FreeSlab(s);
  slabs_.clear();
  cursor_ = {};
}

[[nodiscard]] void* Arena::TryBumpCurrent(std::size_t bytes,
                                          std::size_t align) {
  if (!cursor_.base) return nullptr;
  std::uintptr_t p =
      reinterpret_cast<std::uintptr_t>(cursor_.base + cursor_.used);
  std::uintptr_t aligned = (p + align - 1) & ~(align - 1);
  std::size_t pad = aligned - p;
  if (cursor_.used + pad + bytes > cursor_.size) return nullptr;
  cursor_.used += pad + bytes;
  return reinterpret_cast<void*>(aligned);
}

void Arena::GrowFor(std::size_t bytes, std::size_t align) {
  std::size_t need = bytes + align;
  std::size_t next = slabs_.empty()
                         ? kDefaultSlab
                         : std::min(slabs_.back().size * 2, kMaxSlab);
  if (next < need) next = need;
  NewSlab(next);
}

void Arena::NewSlab(std::size_t size) {
  size = (size + kPage - 1) & ~(kPage - 1);

  SlabSource source = SlabSource::kAligned;
  void* p = nullptr;

#if defined(_WIN32)
  p = TryLargePageAlloc(size);
  if (p) {
    source = SlabSource::kLargePage;
  } else {
    p = ::_aligned_malloc(size, kPage);
    source = SlabSource::kAligned;
  }
#elif defined(__linux__)
  p = ::aligned_alloc(kPage, size);
  if (p) {
    ::madvise(p, size, MADV_HUGEPAGE);
    source = SlabSource::kAligned;
  }
#else
  p = ::aligned_alloc(kPage, size);
  source = SlabSource::kAligned;
#endif

  if (!p) throw std::bad_alloc{};

  slabs_.push_back({static_cast<std::byte*>(p), size, 0, source});
  cursor_ = {static_cast<std::byte*>(p), size, 0};
}

void Arena::FreeSlab(Slab& s) noexcept {
#if defined(_WIN32)
  if (s.source == SlabSource::kLargePage) {
    ::VirtualFree(s.base, 0, MEM_RELEASE);
  } else {
    ::_aligned_free(s.base);
  }
#else
  std::free(s.base);
#endif
}

}  // namespace fschema::memory