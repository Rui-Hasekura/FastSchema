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

#ifndef FSCHEMA_FILTERS_COMPOSITE_FILTER_H_
#define FSCHEMA_FILTERS_COMPOSITE_FILTER_H_

#include <utility>

#include "fschema/filters/concepts.h"
#include "fschema/filters/pos.h"

namespace fschema::filters {

/// Logical AND of two filters. Selects blocks that match BOTH filters.
template <Filter F1, Filter F2>
struct AndFilter {
  F1 f1;
  F2 f2;

  [[nodiscard]] constexpr bool operator()(LocalPos p,
                                          std::uint16_t idx) const noexcept {
    return f1(p, idx) && f2(p, idx);
  }
};

/// Logical OR of two filters. Selects blocks that match EITHER filter.
template <Filter F1, Filter F2>
struct OrFilter {
  F1 f1;
  F2 f2;

  [[nodiscard]] constexpr bool operator()(LocalPos p,
                                          std::uint16_t idx) const noexcept {
    return f1(p, idx) || f2(p, idx);
  }
};

/// Logical NOT of a filter. Selects blocks that DO NOT match the filter.
template <Filter F>
struct NotFilter {
  F f;

  [[nodiscard]] constexpr bool operator()(LocalPos p,
                                          std::uint16_t idx) const noexcept {
    return !f(p, idx);
  }
};

template <Filter F1, Filter F2>
[[nodiscard]] constexpr AndFilter<F1, F2> And(F1 f1, F2 f2) {
  return AndFilter<F1, F2>{std::move(f1), std::move(f2)};
}

template <Filter F1, Filter F2>
[[nodiscard]] constexpr OrFilter<F1, F2> Or(F1 f1, F2 f2) {
  return OrFilter<F1, F2>{std::move(f1), std::move(f2)};
}

template <Filter F>
[[nodiscard]] constexpr NotFilter<F> Not(F f) {
  return NotFilter<F>{std::move(f)};
}

/// Concept-constrained boolean operators for combining Filter types.
///
/// Overloading `&&`/`||` constructs composite filter nodes.
/// Evaluation still short-circuits.
template <Filter F1, Filter F2>
[[nodiscard]] constexpr AndFilter<F1, F2> operator&&(F1 f1, F2 f2) {
  return AndFilter<F1, F2>{std::move(f1), std::move(f2)};
}

template <Filter F1, Filter F2>
[[nodiscard]] constexpr OrFilter<F1, F2> operator||(F1 f1, F2 f2) {
  return OrFilter<F1, F2>{std::move(f1), std::move(f2)};
}

template <Filter F>
[[nodiscard]] constexpr NotFilter<F> operator!(F f) {
  return NotFilter<F>{std::move(f)};
}

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_COMPOSITE_FILTER_H_