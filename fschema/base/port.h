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

#ifndef FSCHEMA_BASE_PORT_H_
#define FSCHEMA_BASE_PORT_H_

#if defined(_MSC_VER)
#  define FSCHEMA_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#  define FSCHEMA_ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#  define FSCHEMA_ALWAYS_INLINE inline
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define FSCHEMA_HOT [[gnu::hot]]
#else
#  define FSCHEMA_HOT
#endif

#if defined(_MSC_VER)
#  define FSCHEMA_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#  define FSCHEMA_RESTRICT __restrict__
#else
#  define FSCHEMA_RESTRICT
#endif

#endif  // FSCHEMA_BASE_PORT_H_