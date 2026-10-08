# BlockNameFilter & IsAirFilter

`fschema/filters/block_name_filter.h` defines `BlockNameFilter` and `IsAirFilter`.

Both are palette-index filters: they ignore `LocalPos` and decide from the palette index alone. Each precomputes a boolean LUT of size `palette.size()` at construction, so the inner loop is a single array lookup.

## BlockNameFilter

```cpp
class BlockNameFilter {
 public:
  BlockNameFilter(std::string_view name, const ir::Region& r);

  [[nodiscard]] bool operator()(LocalPos /*p=*/,
                                std::uint16_t palette_idx) const noexcept;

  [[nodiscard]] std::string_view name() const noexcept;
  [[nodiscard]] bool any_matched() const noexcept;

 private:
  std::string_view name_;
  std::vector<std::uint8_t> matched_;  // size = palette.size()
};
```

Matches every block whose palette entry name equals `name`. The name is compared against `BlockState::name`; properties are not considered.

`name()` returns the stored name. `any_matched()` reports whether at least one palette entry matched; use it to skip iteration early when nothing in the region can match.

The stored `name_` is a `std::string_view`. It must outlive the filter, which in practice means the filter must not outlive the string it was constructed from.

## IsAirFilter

```cpp
class IsAirFilter {
 public:
  explicit IsAirFilter(const ir::Region& r);

  [[nodiscard]] bool operator()(LocalPos /*p=*/,
                                std::uint16_t palette_idx) const noexcept;

 private:
  std::vector<std::uint8_t> matched_;
};
```

Matches `minecraft:air`, `minecraft:cave_air`, and `minecraft:void_air`. The set of names is defined by `base::IsAirVariant`.

## Example

```cpp
namespace fl = fschema::filters;

fl::BlockNameFilter stone{"minecraft:stone", region};
fl::IsAirFilter is_air{region};

if (stone.any_matched()) {
  auto view_res = fl::MakeView(region, stone, arena);
  if (!view_res) {
    // Handle ParseError.
    return;
  }
  view_res->for_each([](fl::LocalPos p, std::uint16_t palette_idx) {
    // ...
  });
}

// Air is the common building block for composite filters.
auto solid = !is_air;
```
