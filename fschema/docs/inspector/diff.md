# Diff

`fschema/inspectors/diff.h` defines `Diff`, `DiffEntry`, and the sentinel `kUnmappedPalette`.

```cpp
constexpr std::uint16_t kUnmappedPalette = 0xFFFF;

[[nodiscard]] inline ParseResult<std::vector<DiffEntry>> Diff(
    const ir::Region& r1,
    const ir::Region& r2);
```

Compares two regions and returns a list of blocks that differ.

## Preconditions

Both regions must have exactly the same `BoundingBox`: same origin and same size along every axis. If they do not, `Diff` returns a `ParseError` with code `InvalidTagId` and the message `"DiffInspector: Regions must have the same size and origin"`. The chosen error code does not reflect the actual condition; treat the message as authoritative.

## Matching

Blocks are matched by `(name, raw_properties)`, not by raw palette index. Palette reordering between the two regions does not produce spurious differences.

Consequences worth knowing:

- Property encoding must match. A `kNbt` entry in one region and a `kString` entry with equivalent properties in the other do not match. Normalize properties first if cross-encoding comparison is needed.
- A block in `r2` whose palette entry has no match in `r1` is mapped to `kUnmappedPalette` and always reported as different.

## DiffEntry

```cpp
struct DiffEntry {
  filters::LocalPos pos;
  std::string_view name1;
  std::string_view name2;
};
```

`pos` is local to `r1` (and therefore to `r2`, since the bounds match). `name1` and `name2` are the palette entry names at that position in each region. If a position's palette index is out of range for its region, the name is `"unknown"`.

## Example

```cpp
namespace fi = fschema::inspectors;

auto diff = fi::Diff(before, after);
if (!diff) {
  // Handle ParseError, most likely a bounds mismatch.
  return;
}

for (const auto& e : *diff) {
  // e.pos, e.name1, e.name2
}
```

Neither region is modified.
