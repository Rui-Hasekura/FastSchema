# Fill & Delete

`fschema/editors/fill.h` defines `Fill` and `Delete`.

Both write a single palette index to every selected block. `Fill` writes a named block; `Delete` writes air.

```cpp
template <filters::Filter F>
[[nodiscard]] ParseResult<void> Fill(filters::BasicView<F> v,
                                     std::string_view block_name);

template <filters::Filter F>
[[nodiscard]] ParseResult<void> Delete(filters::BasicView<F> v);
```

## Fill

Sets every selected block to `block_name`.

The palette index is resolved through `ResolveOrAppend`. If `block_name` is not already in the palette, a new entry is appended and `palette_pristine` is cleared.

The view is taken by value. `BasicView` is cheap to copy, but note that it holds a pointer to the region, not ownership.

```cpp
namespace fl = fschema::filters;
namespace ed = fschema::editors;

fl::BoxFilter box{0, 0, 0, 9, 9, 9};
auto view_res = fl::MakeView(region, box, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}
ed::Fill(*view_res, "minecraft:stone");
```

`Fill` calls `for_each_linear`, so it iterates the region in YZX order and writes by linear index. It does not reconstruct `LocalPos`.

## Delete

Sets every selected block to the air palette index.

The index comes from `ResolveAir`, which looks for `minecraft:air` first, then any air variant (`cave_air`, `void_air`), and appends `minecraft:air` if none exists.

```cpp
ed::Delete(*view_res);
```

## MarkEdited

Both functions call `MarkEdited` on the underlying region after writing. Without it, the encoder may take the lazy passthrough path and re-emit the original packed bytes, discarding the changes.

`ResolveOrAppend` and `ResolveAir` also clear `palette_pristine` when they append a new entry. That is enough to force repacking even if `MarkEdited` were somehow skipped, but `MarkEdited` is still called and still the documented contract.
