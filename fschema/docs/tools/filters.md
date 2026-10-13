# Filters

A filter is a callable that satisfies `fschema::filters::Filter`. It
receives a `LocalPos` and a palette index and returns whether the block at
that position belongs to the selection.
The concept is defined in `fschema/filters/concepts.h`:

```cpp
template <class F>
concept Filter =
    std::regular_invocable<const F&, LocalPos, std::uint16_t> &&
    std::convertible_to<
        std::invoke_result_t<const F&, LocalPos, std::uint16_t>,
        bool>;
```

Filters must be const-callable. `BasicView::ForEach` and `ForEachLinear` pass the filter by const reference.

See [Views](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/views.md) for the view API. Individual filters are in the `filters/` subdirectory.

## Composite filters

`fschema/filters/composite_filter.h` defines `AndFilter`, `OrFilter`, and 
`NotFilter`.

| Type                | Selects blocks that      |
| ------------------- | ------------------------ |
| `AndFilter<F1, F2>` | match both `F1` and `F2` |
| `OrFilter<F1, F2>`  | match `F1` or `F2`       |
| `NotFilter<F>`      | do not match `F`         |

```cpp
template <Filter F1, Filter F2>
[[nodiscard]] constexpr AndFilter<F1, F2> And(F1 f1, F2 f2);

template <Filter F1, Filter F2>
[[nodiscard]] constexpr OrFilter<F1, F2> Or(F1 f1, F2 f2);

template <Filter F>
[[nodiscard]] constexpr NotFilter<F> Not(F f);
```

The operators `&&`, `||`, and `!` are overloaded for `Filter` operands and  
do the same. Both forms store the filters by value.

```cpp
fl::BoxFilter box{0, 0, 0, 9, 9, 9};
fl::IsAirFilter is_air{region};
fl::BlockNameFilter log{"minecraft:oak_log", region};

// Inside the box, not air, and an oak log.
auto selection = box && !is_air && log;
```

`AndFilter::operator()` evaluates `f1(p, idx) && f2(p, idx)`. `OrFilter` 
evaluates `f1(p, idx) || f2(p, idx)`. Short-circuit behavior follows from the underlying operators. Composites are plain template instantiations; there is no virtual dispatch.

The operators are constrained on `Filter`, so they only participate when both operands are filters.

## Type erasure

A composite has a concrete type. To store it where the filter type is unknown, convert it to the type-erased `View`:

```cpp
filters::View erased =
    *filters::MakeView(region, box && !is_air, *schema.arena);
```

`View` is `BasicView<std::function<bool(LocalPos, std::uint16_t)>>`; see [Views](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/views.md).

## Custom filters

Any type that satisfies `Filter` works. See [Custom Tools](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/custom_tools.md) for the full contract.


