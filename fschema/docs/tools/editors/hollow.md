# Hollow

`fschema/editors/hollow.h` defines `Hollow`.

```cpp
template <filters::Filter F>
[[nodiscard]] ParseResult<void> Hollow(filters::BasicView<F> v,
                                       memory::Arena& arena);
```

Removes internal blocks, leaving a shell of thickness 1.

`Hollow` ignores the view's filter. The view is only a way to pass the region and to materialize it. The selection is built from the region itself.

## Interior selection

The shell is defined indirectly. A block is interior if it is non-air and has no air neighbor on any of its six faces:

```cpp
filters::SurfaceFilter surface(r);
filters::IsAirFilter is_air(r);
auto internal_filter = !surface && !is_air;
```

`SurfaceFilter` selects non-air blocks exposed to air on at least one face. Its complement, intersected with non-air, is the interior.

The interior positions are collected into a `std::vector<std::uint64_t>` before any writes. Writing air during the scan would change what `SurfaceFilter` sees for subsequent blocks, cascading the hollow inward.

## Preconditions

The region is materialized through `EnsureMaterialized` at the start of the call. The view's filter is not used.

## MarkEdited

`Hollow` calls `MarkEdited(r)` after writing air indices, when at least one block was cleared.

## Example

```cpp
#include "fschema/editors/hollow.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"

fl::BoxFilter all{0, 0, 0,
                  region.bounds.size[0] - 1,
                  region.bounds.size[1] - 1,
                  region.bounds.size[2] - 1};

auto view_res = fl::MakeView(region, all, *schema.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

auto res = ed::Hollow(*view_res, *schema.arena);
if (!res) {
  const fschema::ParseError& e = res.error();
  return;
}
```


