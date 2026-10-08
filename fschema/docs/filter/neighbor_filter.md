# NeighborFilter

`fschema/filters/neighbor_filter.h` defines `NeighborFilter` and the `MakeNeighborFilter` factory.

```cpp
template <Filter F>
class NeighborFilter {
 public:
  NeighborFilter(const ir::Region& r, F f) noexcept;

  [[nodiscard]] bool operator()(LocalPos p,
                                std::uint16_t /*idx=*/) const noexcept;
};
```

Selects blocks that have at least one adjacent block (out of the 6 face neighbors) matching the provided filter `F`.

`F` is applied to the neighbor's `LocalPos` and palette index. Neighbors outside the region bounds are not passed to `F`; a block on the region border only checks the neighbors that exist.

`NeighborFilter` itself satisfies `Filter`.

## Preconditions

The region must be materialized. `BasicView::for_each` reads `block_indices` directly, so construct the view via `MakeView` as usual.

## Factory

```cpp
template <Filter F>
[[nodiscard]] NeighborFilter<F> MakeNeighborFilter(const ir::Region& r,
                                                   F f) noexcept;
```

Template argument deduction helper. Prefer this over writing the template parameters manually.

## Example

```cpp
namespace fl = fschema::filters;

fl::IsAirFilter is_air{region};

// Select solid blocks that touch air on at least one face.
auto exposed = fl::MakeNeighborFilter(region, is_air);

auto view_res = fl::MakeView(region, exposed, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}
view_res->for_each([](fl::LocalPos p, std::uint16_t palette_idx) {
  // ...
});
```

`NeighborFilter` checks the six face neighbors directly via the YZX strides of the materialized array; it does not reconstruct `LocalPos` for neighbor lookups beyond what `F` needs.
