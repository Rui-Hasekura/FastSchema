# NbtFilter

`fschema/filters/nbt_filter.h` defines an NBT query DSL and `NbtFilter`.
`NbtFilter` matches a block by inspecting the raw NBT of the block entity
at that position. It is the only built-in filter that does not decide from
palette index alone.

## Query DSL

```cpp
enum class NbtPathNodeType : std::uint8_t {
  kKey,    // Navigate into a Compound by key.
  kIndex,  // Navigate into a List by index.
};

struct NbtPathNode {
  NbtPathNodeType type;
  std::string_view key;   // Used when type == kKey.
  std::size_t index = 0;  // Used when type == kIndex.
};

enum class NbtOp : std::uint8_t {
  kExists,    // Path exists.
  kEqString,  // Target is a String equal to str_val.
  kEqInt,     // Target is an integer equal to int_val.
  kGtInt,     // Target is an integer greater than int_val.
  kLtInt,     // Target is an integer less than int_val.
};

struct NbtQuery {
  std::vector<NbtPathNode> path;
  NbtOp op = NbtOp::kExists;
  std::string_view str_val;
  std::int64_t int_val = 0;
};
```

`path` is evaluated from the root of the block entity NBT. A `kKey` node requires the current value to be a Compound and looks up its child by key. A `kIndex` node requires the current value to be a List and advances to that element.

After the path is resolved, `op` is applied to the value. `kEqInt`, `kGtInt`, and `kLtInt` accept `Byte`, `Short`, `Int`, or `Long` and compare numerically. Other tag types return false for integer operations.

`key` and `str_val` must outlive the filter.

## NbtFilter

```cpp
class NbtFilter {
 public:
  NbtFilter(const ir::Region& r, const NbtQuery& query);

  [[nodiscard]] bool operator()(LocalPos p,
                                std::uint16_t /*pal*/) const noexcept;
};
```

The constructor evaluates `query` against every block entity in the region. Matching positions are converted to local coordinates and stored in a hash set. `operator()` checks set membership.

Notes:

- Cost is paid once, at construction. Per-block evaluation is a hash set lookup.

- `LocalPos` is region-local, computed from `block_entity.block_position` minus `region.bounds.origin`.

- Blocks without a block entity never match.

- The palette index is ignored.

- The filter reads `block_entities`, not `block_indices`. It does not require a materialized region.

## Example

```cpp
#include "fschema/filters/nbt_filter.h"
#include "fschema/filters/view.h"

// Any block entity whose Items[0].id is "minecraft:diamond".
fl::NbtQuery query{
    .path = {
        {fl::NbtPathNodeType::kKey, "Items", 0},
        {fl::NbtPathNodeType::kIndex, {}, 0},
        {fl::NbtPathNodeType::kKey, "id", 0},
    },
    .op = fl::NbtOp::kEqString,
    .str_val = "minecraft:diamond",
};

fl::NbtFilter has_diamond{region, query};

auto view_res = fl::MakeView(region, has_diamond, *schematic.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

view_res->ForEach([](fl::LocalPos p, std::uint16_t pal) {
  // Every selected block has a block entity at p matching the query.
});
```

Path existence check:

```cpp
fl::NbtQuery has_lock{
    .path = {{fl::NbtPathNodeType::kKey, "Lock", 0}},
    .op = fl::NbtOp::kExists,
};
```


