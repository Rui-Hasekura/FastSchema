# FloatingCheck and SupportCheck

`fschema/inspectors/floating_check.h` and `support_check.h` define two
validation inspectors that scan a region for structural problems.
Both require a materialized region. Both read `block_indices` directly.

## FloatingCheck

```cpp
enum class FloatingCheckMode : std::uint8_t {
  kFloatingBlocks   = 1 << 0,
  kFloatingGravity  = 1 << 1,
  kFloatingEntities = 1 << 2,
};

[[nodiscard]] inline FloatingCheckResult FloatingCheck(
    const ir::Region& r,
    FloatingCheckMode mode = FloatingCheckMode::kFloatingBlocks |
                             FloatingCheckMode::kFloatingGravity |
                             FloatingCheckMode::kFloatingEntities);
```

The `|` and `&` operators are overloaded for `FloatingCheckMode`.

Modes:

- `kFloatingBlocks`: every non-air block whose six face neighbors are all air or out of bounds.

- `kFloatingGravity`: floating blocks that are gravity-affected, using 
  `IsGravityAffected`.

- `kFloatingEntities`: entities whose block position is air.

`kFloatingBlocks` and `kFloatingGravity` share the same scan. A floating gravity-affected block is reported if either bit is set. The`is_gravity_affected` flag on the entry is always filled in.

Result types:

```cpp
struct FloatingBlock {
  filters::LocalPos pos;
  std::string_view block_name;
  bool is_gravity_affected;
};

struct FloatingEntity {
  std::string_view id;
  std::array<double, 3> position;
};

struct FloatingCheckResult {
  std::vector<FloatingBlock> blocks;
  std::vector<FloatingEntity> entities;
};
```

`FloatingBlock::pos` is region-local. `FloatingEntity::position` is world-space.

```cpp
[[nodiscard]] inline bool IsGravityAffected(std::string_view name) noexcept;
```

Checks a static list of block names affected by gravity. The list is in the header, not in a data file.

```cpp
#include "fschema/inspectors/floating_check.h"

auto result = fi::FloatingCheck(
    region,
    fi::FloatingCheckMode::kFloatingGravity |
        fi::FloatingCheckMode::kFloatingEntities);

for (const auto& b : result.blocks) {
  // b.pos, b.block_name, b.is_gravity_affected
}
for (const auto& e : result.entities) {
  // e.id, e.position
}
```

## SupportCheck

```cpp
[[nodiscard]] inline std::vector<UnsupportedBlock> SupportCheck(
    const ir::Region& r);
```

Scans a region for blocks that require support and lack it.

The rules are in `kSupportRules`, a compile-time array of `SupportRule`:

```cpp
struct SupportRule {
  std::string_view block_name;
  SupportDir dir;
  std::string_view issue;
};
```

`SupportDir` covers:

- `kBelow`, `kAbove`, and the four horizontal directions.

- `kFacingInv`: support is opposite to the block's `facing` property. Used by wall torches, ladders, wall signs, tripwire hooks.

- `kAnyHorizontalOrAbove`: any of the four horizontal neighbors or the block above. Used by vines.

- `kFaceDependent`: depends on the `face` property (`floor`, `ceiling`, `wall`). Used by buttons and levers.

- `kHangingDependent`: depends on the `hanging` property. Used by lanterns.

A neighbor is support-capable if it is in bounds, is not air, and is not a fluid (`water`, `flowing_water`, `lava`, `flowing_lava`). Out-of-bounds neighbors are treated as air and do not support.

If a rule requires `facing`, `face`, or `hanging` and the property is absent, the block is reported with a fixed issue string such as `"missing facing property"`, not the rule's own issue string.

Result type:

```cpp
struct UnsupportedBlock {
  filters::LocalPos pos;
  std::string_view block_name;
  std::string_view issue;
};
```

`pos` is region-local. `block_name` points into the rule table, not into the region's palette. `issue` is either the rule's `issue` or one of the fixed property-missing strings.

```cpp
#include "fschema/inspectors/support_check.h"

auto unsupported = fi::SupportCheck(region);
for (const auto& b : unsupported) {
  // b.pos, b.block_name, b.issue
}
```

Adding a rule means editing `kSupportRules`. There is no registration mechanism.
