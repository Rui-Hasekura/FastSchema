# ReplacePaletteEntry

`fschema/editors/replace_palette_entry.h` defines `ReplacePaletteEntry`.

```cpp
[[nodiscard]] inline ParseResult<void> ReplacePaletteEntry(
    ir::Region& r,
    std::uint16_t target_idx,
    std::string_view new_name);
```

Replaces the `BlockState` at `palette[target_idx]` with a new name, clearing any existing properties.

```cpp
ir::BlockState& bs = r.palette[target_idx];
bs.name = new_name;
bs.raw_properties = {};
bs.prop_encoding = ir::PropertyEncoding::kNone;
```

Because `block_indices` is not remapped, the change is visible immediately: every block that already used `target_idx` now refers to the new block state.

## Index 0 is protected

`target_idx == 0` returns a `PaletteIndexOutOfRange` error. Index 0 is the protected air slot in formats that require it.

```cpp
if (target_idx == 0) [[unlikely]] {
  return std::unexpected(
      ParseError{ParseError::Code::PaletteIndexOutOfRange,
                 "ReplacePaletteEntry: index 0 is protected (air slot)",
                 0});
}
```

Any index `>= r.palette.size()` also returns `PaletteIndexOutOfRange`.

## palette_pristine is left untouched

`ReplacePaletteEntry` does not call `MarkEdited` and does not clear `palette_pristine`.

This is deliberate. The block indices are unchanged, so the encoder may still reuse the packed data from `lazy_source.raw_bytes`. Only the palette is rewritten, and the encoder reads the current palette at serialization time.

If the same `target_idx` is used with different properties in different regions, that is not this function's concern; palette entries are per-region.

## Lifetime of new_name

`new_name` must outlive the region. `BlockState::name` is a `std::string_view`, and the region does not copy it.

## Example

```cpp
namespace ed = fschema::editors;

// Replace palette entry 3 with a different block.
auto res = ed::ReplacePaletteEntry(region, 3, "minecraft:deepslate_bricks");
if (!res) {
  // Handle ParseError, e.g. index out of range.
}
```
