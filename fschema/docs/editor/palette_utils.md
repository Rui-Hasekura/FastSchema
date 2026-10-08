# Palette Utilities

`fschema/editors/palette_utils.h` defines the helpers that editors and the encoder share when touching a region's palette or edit state.

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
```

## FindPaletteIndex

Returns the first palette index whose `BlockState::name` equals `name`, or `std::nullopt`. Linear scan over `palette`.

## FindAirIndex

Returns a palette index suitable for writing air.

`minecraft:air` is preferred. If it is absent, the first air variant (`cave_air`, `void_air`) is returned as a fallback. If no air variant exists, returns `std::nullopt`.

## AppendPaletteEntry

Appends a new `BlockState` with no properties to the palette and returns its index.

The caller must ensure `palette.size() < 65536`, since indices are `std::uint16_t`. `AppendPaletteEntry` does not check this.

## ResolveOrAppend

Returns the index of `name`, appending a new entry if it is absent.

When a new entry is appended, `r.lazy_source.palette_pristine` is set to `false`. This invalidates the encoder's lazy passthrough path, forcing a full repack of the block indices during serialization.

## ResolveAir

Like `ResolveOrAppend`, but for air. Prefers `minecraft:air`, falls back to any air variant, and appends `minecraft:air` if none exists.

Sets `palette_pristine = false` when it appends.

## MarkEdited

```cpp
inline void MarkEdited(ir::Region& r) noexcept {
  r.edited = true;
  r.lazy_source.palette_pristine = false;
}
```

Marks the region as structurally modified.

This must be called by any editor that mutates `block_indices`. Without it, the encoder may take the lazy passthrough path and re-emit the original packed bytes, discarding the changes.

`ResolveOrAppend` and `ResolveAir` also clear `palette_pristine` when they append, but `MarkEdited` is still the documented contract for editors that write to `block_indices`.
