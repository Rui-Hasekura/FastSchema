# Custom Filter

A custom filter is any type that satisfies `fschema::filters::Filter`. There
is nothing to inherit from and nothing to register. The concept is defined
in `fschema/filters/concepts.h`:

```cpp
template <class F>
concept Filter =
    std::regular_invocable<const F&, LocalPos, std::uint16_t> &&
    std::convertible_to<std::invoke_result_t<const F&, LocalPos, std::uint16_t>,
                        bool>;
```

In practice: `const F&` must be callable with a `LocalPos` and a palette
index, and the result must be convertible to `bool`.

## Lambda

A stateless lambda works directly:

```cpp
namespace fl = fschema::filters;

// Select blocks on the diagonal x == z.
auto diagonal = [](fl::LocalPos p, std::uint16_t /*idx*/) {
  return p.x == p.z;
};

auto view_res = fl::MakeView(region, diagonal, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}
view_res->for_each([](fl::LocalPos p, std::uint16_t palette_idx) {
  // ...
});
```

A lambda with captures also works as long as its `operator()` is const.
Capturing by value gives a const-callable closure; capturing by reference
also does, provided the referenced object outlives the view.

## Class

The same shape the built-in filters use:

```cpp
class TopHalf {
 public:
  explicit TopHalf(std::int32_t half_y) noexcept : half_y_(half_y) {}

  [[nodiscard]] bool operator()(fl::LocalPos p,
                                std::uint16_t /*idx*/) const noexcept {
    return p.y >= half_y_;
  }

 private:
  std::int32_t half_y_;
};
```

`operator()` must be `const`. `BasicView::for_each` passes the filter by
const reference, so a non-const `operator()` does not satisfy the concept
and `MakeView` will not accept it.

A `mutable` lambda has a non-const `operator()` and therefore does not
satisfy `Filter` either. `RandomFilter` is a filter-shaped type in this
situation: its `operator()` advances PRNG state and is non-const, so it
cannot be passed to `MakeView` directly. See `random_filter.md` for the
workaround.

## What a filter can rely on

`LocalPos` passed to `operator()` is a valid position inside the region
bounds when it comes from `BasicView::for_each`. Out-of-bounds coordinates
are not delivered.

The palette index is always within range for the same reason. A filter that
computes a LUT over `palette` at construction does not need to range-check
the index in `operator()`.

## What a filter should not rely on

`BasicView::for_each` invokes the filter in YZX order, but nothing in the
API contract requires that. A filter that is correct only for a particular
iteration order is fragile; keep any order dependence in the editor or
inspector that consumes the view, not in the filter.

The filter must not mutate the region. `operator()` receives `LocalPos` by
value and a `std::uint16_t` by value; the region is reachable only through
whatever the filter itself captured. Filters that close over `ir::Region&`
and write to it are possible to write but are not filters in the intended
sense, and the results of composing them with `&&` and `||` are hard to
reason about.

## Construction and materialization

Two shapes of filter exist in the built-ins:

- LUT-only filters such as `BlockNameFilter`, `IsAirFilter`, and
  `BlockStateFilter` read `region.palette` at construction and ignore
  `block_indices` at evaluation time. These work on an unmaterialized
  region.
- Region-reading filters such as `SurfaceFilter` and `NeighborFilter`
  read `region.block_indices` at evaluation time. These require the region
  to be materialized.

If a custom filter precomputes from `palette` only, it can be constructed
before `MakeView`. If it needs `block_indices`, construct it after the
region is materialized, which `MakeView` guarantees.

## Composition

Composite filters built with `&&`, `||`, and `!` are themselves filters, so
a custom filter composes with the built-ins without any additional
adaptation. The operators are constrained on `Filter`, which means they
only participate in overload resolution when both operands are filters:

```cpp
fl::BoxFilter box{0, 0, 0, 9, 9, 9};
fl::IsAirFilter is_air{region};
TopHalf top{5};

auto selection = box && top && !is_air;
```
