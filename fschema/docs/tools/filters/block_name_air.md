# BlockNameFilter and IsAirFilter

`fschema/filters/block_name_filter.h` defines two palette-index filters.
Both ignore `LocalPos` and decide from the palette index alone. The palette
is scanned once at construction; evaluation is a constant-time lookup per
block.

## BlockNameFilter

```cpp
class BlockNameFilter {
 public:
  BlockNameFilter(std::string_view name, const ir::Region& r);

  [[nodiscard]] bool operator()(LocalPos /*p*/,
                                std::uint16_t palette_idx) const noexcept;

  [[nodiscard]] std::string_view name() const noexcept;
  [[nodiscard]] bool any_matched() const noexcept;
};
```

Matches every block whose palette entry name equals `name`. Properties are not considered: two palette entries with the same name and different properties both match.

`any_matched()` reports whether at least one palette entry matched. Use it to skip iteration when nothing in the region can match.

`name` is stored as a `std::string_view` and must outlive the filter.

## IsAirFilter

```cpp
class IsAirFilter {
 public:
  explicit IsAirFilter(const ir::Region& r);

  [[nodiscard]] bool operator()(LocalPos /*p*/,
                                std::uint16_t palette_idx) const noexcept;
};
```

Matches `minecraft:air`, `minecraft:cave_air`, and `minecraft:void_air`. The set of names is defined by `fschema::base::IsAirVariant`.

## Example

```cpp
#include "fschema/filters/block_name_filter.h"
#include "fschema/filters/view.h"

fl::BlockNameFilter stone{"minecraft:stone", region};
fl::IsAirFilter is_air{region};

if (stone.any_matched()) {
  auto view_res = fl::MakeView(region, stone, *schematic.arena);
  if (!view_res) {
    const fschema::ParseError& e = view_res.error();
    return;
  }
  view_res->ForEach([](fl::LocalPos p, std::uint16_t pal) {
    // Called for every stone block.
  });
}

// Solid blocks: not air.
auto solid = !is_air;
```

Both filters read `region.palette` at construction and do not touch 
`block_indices`. They work on an unmaterialized region, but the palette must be populated.
