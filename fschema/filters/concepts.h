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

#ifndef FSCHEMA_FILTERS_CONCEPTS_H_
#define FSCHEMA_FILTERS_CONCEPTS_H_

#include <concepts>
#include <cstdint>

#include "fschema/filters/pos.h"

namespace fschema::filters {


// Concept defining the required interface for a block selection filter.
///
/// A type `F` satisfies `Filter`
/// if it is callable with a `LocalPos` and a palette index,
/// returning a type convertible to `bool`.
///
/// Filters may use local position and/or palette index.
///
/// Note: Filters must be const-callable.
/// `BasicView::for_each` passes the filter by const reference.
template <class F>
concept Filter =
    std::regular_invocable<const F&, LocalPos, std::uint16_t> &&
    std::convertible_to<std::invoke_result_t<const F&, LocalPos, std::uint16_t>,
                        bool>;

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_CONCEPTS_H_