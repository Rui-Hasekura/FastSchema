# Composite Filters

`fschema/filters/composite_filter.h` defines `AndFilter`, `OrFilter`, and
`NotFilter`. They combine filters that satisfy `Filter`, and they
themselves satisfy `Filter`, so composites can be nested.

| Type                | Selects blocks that      |
| ------------------- | ------------------------ |
| `AndFilter<F1, F2>` | match both `F1` and `F2` |
| `OrFilter<F1, F2>`  | match `F1` or `F2`       |
| `NotFilter<F>`      | do not match `F`         |

## Construction

Either use the factory functions:

```cpp
template <Filter F1, Filter F2>
[[nodiscard]] constexpr AndFilter<F1, F2> And(F1 f1, F2 f2);

template <Filter F1, Filter F2>
[[nodiscard]] constexpr OrFilter<F1, F2> Or(F1 f1, F2 f2);

template <Filter F>
[[nodiscard]] constexpr NotFilter<F> Not(F f);
```

Or use the operator overloads, which take the same arguments:

```cpp
auto f_and = f1 && f2;
auto f_or  = f1 || f2;
auto f_not = !f;
```

Both forms store the filters by value. The operators are constrained on
`Filter`, so they only participate when both operands are filters.

## Evaluation

`AndFilter::operator()` evaluates `f1(p, idx) && f2(p, idx)`, and
`OrFilter::operator()` evaluates `f1(p, idx) || f2(p, idx)`. Short-circuit
behavior follows from the underlying `&&` and `||`.

A composite is a plain template instantiation: no virtual dispatch, and
`(f1 && f2)(p, idx)` reduces to two direct calls.

## Example

```cpp
namespace fl = fschema::filters;

fl::BoxFilter box{0, 0, 0, 9, 9, 9};                    // local bounds
fl::IsAirFilter is_air{region};
fl::BlockNameFilter log{"minecraft:oak_log", region};

// Inside the box, not air, and an oak log.
auto selection = box && !is_air && log;

auto view_res = fl::MakeView(region, selection, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}
view_res->for_each([](fl::LocalPos p, std::uint16_t palette_idx) {
  // ...
});
```

If the composite must be stored where its concrete type is unknown, the
converting constructor of `BasicView` accepts any filter `G` that `F` can
be constructed from:

```cpp
filters::View erased = *filters::MakeView(region, box && !is_air, arena);
```

`View` is `BasicView<std::function<bool(LocalPos, std::uint16_t)>>`; see
`view.md`.
