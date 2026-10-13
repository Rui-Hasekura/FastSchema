# Fill, Delete, Replace, ReplaceWith

`fschema/editors/fill.h` defines `Fill` and `Delete`.
`fschema/editors/replace.h` defines `ReplaceWith` and `Replace`.
All four write a single palette index to every selected block.

```cpp
template <filters::Filter F>
[[nodiscard]] ParseResult<void> Fill(filters::BasicView<F> v,
                                     std::string_view block_name);

template <filters::Filter F>
[[nodiscard]] ParseResult<void> Delete(filters::BasicView<F> v);

template <filters::Filter F>
[[nodiscard]] ParseResult<void> ReplaceWith(filters::BasicView<F> v,
                                            std::string_view from,
                                            std::string_view to);

template <filters::Filter F, class Mapper>
  requires std::regular_invocable<const Mapper&, std::uint16_t> &&
           std::convertible_to<
               std::invoke_result_t<const Mapper&, std::uint16_t>,
               std::uint16_t>
[[nodiscard]] ParseResult<void> Replace(filters::BasicView<F> v,
                                        Mapper mapper);
```

Each writes by linear index through `ForEachLinear` and calls `MarkEdited` on success.

## Fill

Sets every selected block to `block_name`. The palette index is resolved through `ResolveOrAppend`; if the name is absent, a new entry is appended and `palette_pristine` is cleared.

```cpp
#include "fschema/editors/fill.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"

fl::BoxFilter box{0, 0, 0, 9, 9, 9};
auto view_res = fl::MakeView(region, box, *schema.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

auto res = ed::Fill(*view_res, "minecraft:stone");
if (!res) {
  const fschema::ParseError& e = res.error();
  return;
}
```

## Delete

Sets every selected block to the air palette index. The index comes from 
`ResolveAir`, which prefers `minecraft:air`, then any air variant, and appends `minecraft:air` if none exists.

```cpp
auto res = ed::Delete(*view_res);
if (!res) {
  const fschema::ParseError& e = res.error();
  return;
}
```

## ReplaceWith

Replaces every selected block whose palette entry name equals `from` with `to`.

The palette is scanned once at the start to build a name-index LUT. Per-block matching is by palette index. `to` is resolved through `ResolveOrAppend`, so it may append a new entry.

If no palette entry matches `from`, the function returns without iterating and without calling `MarkEdited`.

```cpp
#include "fschema/filters/block_name_filter.h"

fl::BlockNameFilter stone{"minecraft:stone", region};
auto view_res = fl::MakeView(region, stone, *schema.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

auto res = ed::ReplaceWith(*view_res, "minecraft:stone",
                           "minecraft:cobblestone");
```

## Replace

Calls `mapper(pal)` for every selected block and writes the result. If `mapper(pal)` returns the same value, the block is left alone. The mapper runs for every selected block, including blocks it does not change.

Use `Replace` when matching cannot be expressed by name equality: property  
comparisons, index ranges, or any custom mapping.

```cpp
// Replace every selected block with air except palette index 7.
auto res = ed::Replace(*view_res, [](std::uint16_t pal) -> std::uint16_t {
  return pal == 7 ? 7 : 0;
});
```

`Replace` always calls `MarkEdited`, even if the mapper returned the same value for every block.
