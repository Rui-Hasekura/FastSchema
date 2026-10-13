# Diff

`fschema/inspectors/diff.h` defines `Diff`, `DiffEntry`, and the sentinel
`kUnmappedPalette`.

```cpp
constexpr std::uint16_t kUnmappedPalette = 0xFFFF;

[[nodiscard]] inline ParseResult<std::vector<DiffEntry>> Diff(
    const ir::Region& r1,
    const ir::Region& r2);
```

Compares two regions and returns a list of blocks that differ.

## Preconditions

Both regions must have identical `BoundingBox`: same origin and same size along every axis. If they do not, `Diff` returns a `ParseError` with code `InvalidTagId` and message `"DiffInspector: Regions must have the same size and origin"`. The error code does not reflect the actual condition; treat the message as authoritative.

Both regions must be materialized.

## Matching

Blocks are matched by `(name, raw_properties)`, not by palette index. Palette reordering between the regions does not produce spurious differences.

Property encoding must match. A `kNbt` entry in one region and a `kString` entry with equivalent properties in the other do not match. Normalize properties first if a cross-encoding comparison is needed.

A block in `r2` whose palette entry has no match in `r1` is mapped to 
`kUnmappedPalette` and always reported as different.

## DiffEntry

```cpp
struct DiffEntry {
  filters::LocalPos pos;
  std::string_view from_block;
  std::string_view to_block;
};
```

`pos` is local to `r1` and, since the bounds match, also to `r2`. `from_block` and `to_block` are the palette entry names at that position in each region. If a position's palette index is out of range for its region, the name is `"unknown"`.

## Example

```cpp
#include "fschema/inspectors/diff.h"

auto diff = fi::Diff(before, after);
if (!diff) {
  const fschema::ParseError& e = diff.error();
  // Most likely a bounds mismatch.
  return;
}

for (const auto& e : *diff) {
  // e.pos, e.from_block, e.to_block
}
```

Neither region is modified.
