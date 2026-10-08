# Hollow

`fschema/editors/hollow.h` defines `Hollow`.

```cpp
template <filters::Filter F>
[[nodiscard]] ParseResult<void> Hollow(filters::BasicView<F> v,
                                       memory::Arena& arena);
```

Removes all internal blocks, leaving only a shell of thickness 1.

## How it works

The shell is defined indirectly. `Hollow` builds a composite filter for the interior and sets those blocks to air:

```cpp
filters::SurfaceFilter surface(r);
filters::IsAirFilter is_air(r);
auto internal_filter = !surface && !is_air;
```

`SurfaceFilter` selects non-air blocks exposed to air on at least one face. Its complement, restricted to non-air blocks, is the interior.

The view is then created over `internal_filter`, and the resulting linear indices are collected before any writes. This two-pass approach matters: writing air during the scan would change what `SurfaceFilter` sees for subsequent blocks, cascading the hollow effect inward.

```cpp
ir::Region& r = v.region();
auto mat_res = ir::EnsureMaterialized(r, arena);
if (!mat_res) return std::unexpected(mat_res.error());

filters::SurfaceFilter surface(r);
filters::IsAirFilter is_air(r);
auto internal_filter = !surface && !is_air;

auto internal_view_res = filters::MakeView(r, internal_filter, arena);
if (!internal_view_res) return std::unexpected(internal_view_res.error());

std::vector<std::uint64_t> to_delete;
internal_view_res->for_each_linear(
    [&](std::uint64_t linear_idx, std::uint16_t) {
      to_delete.push_back(linear_idx);
    });

if (to_delete.empty()) return {};

const std::uint16_t air_idx = ResolveAir(r);
std::uint16_t* data = r.block_indices.data();
for (std::uint64_t idx : to_delete) {
  data[idx] = air_idx;
}
```

The `internal_filter` itself uses the operator overloads from `composite_filter.h`. The `&&` and `!` are constrained on `Filter`, so they only participate when the operands are filters.

## Preconditions

The region is materialized via `EnsureMaterialized` at the start. The input view's filter is not used; `Hollow` builds its own selection.

## MarkEdited

`Hollow` calls `MarkEdited(r)` after writing the air indices.

## Example

```cpp
namespace fl = fschema::filters;
namespace ed = fschema::editors;

// Hollow the entire region.
fl::BoxFilter all{0, 0, 0, region.bounds.size[0] - 1,
                  region.bounds.size[1] - 1, region.bounds.size[2] - 1};

auto view_res = fl::MakeView(region, all, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}

ed::Hollow(*view_res, arena);
```

`Hollow` ignores the view's filter. The view is only a convenient way to pass the region and materialize it.
