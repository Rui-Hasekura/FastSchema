# Support Check

`fschema/inspectors/support_check.h` defines `SupportCheck`, the direction enum, the rule table, and the property helpers.

```cpp
[[nodiscard]] inline std::vector<UnsupportedBlock> SupportCheck(
    const ir::Region& r);
```

Scans a region for blocks that require support and lack it.

## Rule table

`kSupportRules` is a compile-time array of `SupportRule`:

```cpp
struct SupportRule {
  std::string_view block_name;
  SupportDir dir;
  std::string_view issue;
};
```

Each rule names a block and the direction from which it needs a support-capable neighbor. The `issue` string is copied verbatim into the result entry.

`SupportDir` covers:

- `kBelow`, `kAbove`, and the four horizontal directions.
- `kFacingInv`: support is opposite to the block's `facing` property. Used by wall torches, ladders, wall signs, tripwire hooks.
- `kAnyHorizontalOrAbove`: any of the four horizontal neighbors or the block above. Used by vines.
- `kFaceDependent`: depends on the `face` property (`floor`, `ceiling`, `wall`). Used by buttons and levers.
- `kHangingDependent`: depends on the `hanging` property. Used by lanterns.

## Support capability

A neighbor is support-capable if `!IsAirLikeAt(...)`:

- Not air.
- Not a fluid (`water`, `flowing_water`, `lava`, `flowing_lava`).
- In bounds. Out-of-bounds neighbors are treated as air and therefore do not support.

## Missing properties

If a block's rule requires `facing`, `face`, or `hanging` and the property is absent, the block is reported with a fixed issue string such as `"missing facing property"` rather than the rule's own issue string.

## Result

```cpp
struct UnsupportedBlock {
  filters::LocalPos pos;
  std::string_view block_name;
  std::string_view issue;
};
```

`pos` is region-local. `block_name` points into the rule table, not into the region's palette. `issue` is either the rule's `issue` or one of the fixed property-missing strings.

## Preconditions

The region must be materialized. `SupportCheck` reads `block_indices` directly.

## Example

```cpp
namespace fi = fschema::inspectors;

auto unsupported = fi::SupportCheck(region);
for (const auto& b : unsupported) {
  // b.pos, b.block_name, b.issue
}
```

## Adding rules

`kSupportRules` is a fixed array in the header. Adding a new block type means editing this array; there is no registration mechanism.
