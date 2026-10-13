# Palette Utilities

`fschema/editors/palette_utils.h` defines the helpers used by editors and
the encoder when touching a region's palette or edit state.
`fschema/editors/replace_palette_entry.h` defines
`ReplacePaletteEntry`.

```cpp
[[nodiscard]] inline std::optional<std::uint16_t> FindPaletteIndex(
    const ir::Region& r, std::string_view name) noexcept;

[[nodiscard]] inline std::optional<std::uint16_t> FindAirIndex(
    const ir::Region& r) noexcept;

[[nodiscard]] inline std::uint16_t AppendPaletteEntry(ir::Region& r,
                                                      std::string_view name);

[[nodiscard]] inline std::uint16_t ResolveOrAppend(ir::Region& r,
                                                   std::string_view name);

[[nodiscard]] inline std::uint16_t ResolveAir(ir::Region& r);

inline void MarkEdited(ir::Region& r) noexcept;

[[nodiscard]] inline ParseResult<void> ReplacePaletteEntry(
    ir::Region& r,
    std::uint16_t target_idx,
    std::string_view new_name);
```

## Lookup

`FindPaletteIndex` returns the first palette index whose `BlockState::name` equals `name`, or `std::nullopt`. It is a linear scan over `palette`.

`FindAirIndex` returns a palette index suitable for writing air. `minecraft:air` is preferred. If it is absent, the first air variant (`cave_air`, `void_air`) is returned. If no air variant exists, `std::nullopt` is returned.

## AppendPaletteEntry

Appends a new `BlockState` with no properties to the palette and returns its index. `is_air_lut` is cleared.

The caller must ensure `palette.size() < 65536`; indices are `uint16_t` and `AppendPaletteEntry` does not check the limit.

## ResolveOrAppend

Returns the index of `name`, appending a new entry if it is absent. On append, `r.lazy_source.palette_pristine` is set to `false`. This invalidates the encoder's lazy passthrough path.

## ResolveAir

Like `ResolveOrAppend`, but for air. Prefers `minecraft:air`, falls back to any air variant, and appends `minecraft:air` if none exists. Sets `palette_pristine = false` on append.

## MarkEdited

```cpp
inline void MarkEdited(ir::Region& r) noexcept {
  r.edited = true;
  r.lazy_source.palette_pristine = false;
}
```

Marks the region as structurally modified.

Any editor that mutates `block_indices` must call `MarkEdited`. Without it, the encoder may take the lazy passthrough path and re-emit the original packed bytes.

`ResolveOrAppend` and `ResolveAir` clear `palette_pristine` when they append a new entry, but that is not a substitute. Call `MarkEdited` regardless.

## ReplacePaletteEntry

Replaces the `BlockState` at `palette[target_idx]` with a new name, clearing any existing properties:

```cpp
ir::BlockState& bs = r.palette[target_idx];
bs.name = new_name;
bs.raw_properties = {};
bs.prop_encoding = ir::PropertyEncoding::kNone;
r.is_air_lut.clear();
```

`block_indices` is not remapped. The change is visible immediately: every block that already used `target_idx` now refers to the new block state.

### Index 0 is protected

`target_idx == 0` returns `PaletteIndexOutOfRange`. Index 0 is the protected air slot in formats that require it. Any index `>= r.palette.size()` also returns `PaletteIndexOutOfRange`.

### palette_pristine is left untouched

`ReplacePaletteEntry` does not call `MarkEdited` and does not clear 
`palette_pristine`. The block indices are unchanged, so the encoder may still reuse the packed data from `lazy_source.raw_bytes`. Only the palette is rewritten, and the encoder reads the current palette at serialization time.

### Lifetime of new_name

`new_name` must outlive the region. `BlockState::name` is a `std::string_view`; the region does not copy it.

```cpp
#include "fschema/editors/replace_palette_entry.h"

auto res = ed::ReplacePaletteEntry(region, 3, "minecraft:deepslate_bricks");
if (!res) {
  const fschema::ParseError& e = res.error();
  return;
}
```
