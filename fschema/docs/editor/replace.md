# Replace & ReplaceWith

`fschema/editors/replace.h` defines `ReplaceWith` and `Replace`.

Both write new palette indices to selected blocks. `ReplaceWith` works by block name; `Replace` works by an arbitrary palette-index mapping.

```cpp
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

## ReplaceWith

Replaces every selected block whose palette entry name equals `from` with `to`.

The constructor-side work builds a `std::vector<std::uint8_t>` LUT of size `palette.size()` marking which entries match `from`. The inner loop is a palette-index lookup, not a string comparison.

If no palette entry matches `from`, `any_from` is false and the function returns immediately without iterating or calling `MarkEdited`.

`to` is resolved through `ResolveOrAppend`, so a new palette entry is appended if needed.

```cpp
namespace fl = fschema::filters;
namespace ed = fschema::editors;

fl::BlockNameFilter stone{"minecraft:stone", region};
auto view_res = fl::MakeView(region, stone, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}
ed::ReplaceWith(*view_res, "minecraft:stone", "minecraft:cobblestone");
```

## Replace

Replaces each selected block's palette index with `mapper(pal)`.

The mapper is a callable taking `std::uint16_t` and returning `std::uint16_t`. The concept constraint is enforced at compile time; the returned value must be convertible to `std::uint16_t`.

If `mapper(pal)` returns the same value, the block is left alone. There is no LUT and no early-out; the mapper is called for every selected block, including blocks it does not change.

Use this for matching that name equality cannot express: property comparisons, index ranges, or any custom mapping.

```cpp
// Replace every block in the region with air, except block index 7.
ed::Replace(*view_res, [](std::uint16_t pal) -> std::uint16_t {
  return pal == 7 ? 7 : 0;
});
```

## MarkEdited

Both functions call `MarkEdited` on the underlying region after writing.

`ReplaceWith` skips the call when no palette entry matches `from`. `Replace` always calls it, even if the mapper returned the same value for every block.
