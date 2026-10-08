# Floating Check

`fschema/inspectors/floating_check.h` defines `FloatingCheck`, the mode bitmask, the result types, and the gravity-affected lookup.

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

## Modes

- `kFloatingBlocks`: report every non-air block whose six face neighbors are all air or out of bounds.
- `kFloatingGravity`: report floating blocks that are gravity-affected, using the `IsGravityAffected` table.
- `kFloatingEntities`: report entities whose block position is air.

`kFloatingBlocks` and `kFloatingGravity` share the same scan. A floating gravity-affected block is reported if either bit is set. The `is_gravity_affected` flag on the entry is always filled in, so callers can filter afterwards.

## Result

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

## IsGravityAffected

```cpp
[[nodiscard]] inline bool IsGravityAffected(std::string_view name) noexcept;
```

Checks a static list of block names that are affected by gravity. The list is in this header, not in a data file.

## Preconditions

The region must be materialized for the block scan. The entity scan reads `r.entities` only.

## Example

```cpp
namespace fi = fschema::inspectors;

// Only floating gravity blocks and floating entities.
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
