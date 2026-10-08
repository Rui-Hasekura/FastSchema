# NbtFilter

`fschema/filters/nbt_filter.h` defines the NBT query DSL and `NbtFilter`.

`NbtFilter` matches a block by inspecting the raw NBT of the block entity at that position. It is the only filter in `fschema` that does not decide from palette index alone.

## Query DSL

```cpp
enum class NbtPathNodeType : std::uint8_t {
  kKey,    // Navigate into a Compound by key
  kIndex,  // Navigate into a List by index
};

struct NbtPathNode {
  NbtPathNodeType type;
  std::string_view key;   // used if type == kKey
  std::size_t index = 0;  // used if type == kIndex
};

enum class NbtOp : std::uint8_t {
  kExists,    // Path exists
  kEqString,  // Target is a String equal to str_val
  kEqInt,     // Target is an integer equal to int_val
  kGtInt,     // Target is an integer greater than int_val
  kLtInt,     // Target is an integer less than int_val
};

struct NbtQuery {
  std::vector<NbtPathNode> path;
  NbtOp op = NbtOp::kExists;
  std::string_view str_val;
  std::int64_t int_val = 0;
};
```

`path` is evaluated from the root of the block entity NBT. A `kKey` node requires the current value to be a Compound and looks up its child with that key. A `kIndex` node requires the current value to be a List and advances to that element.

After the path is resolved, `op` is applied to the resolved value. `kEqInt`, `kGtInt`, and `kLtInt` accept any of `Byte`, `Short`, `Int`, or `Long` and compare numerically. Other tag types for integer ops return false.

`key` and `str_val` must outlive the filter.

## NbtFilter

```cpp
class NbtFilter {
 public:
  NbtFilter(const ir::Region& r, const NbtQuery& query);

  [[nodiscard]] bool operator()(LocalPos p,
                                std::uint16_t /*palette_idx=*/) const noexcept;

 private:
  absl::flat_hash_set<LocalPos, LocalPosHash> matched_positions_;
};
```

The constructor evaluates `query` against every block entity in the region. Matching positions are converted to local coordinates and stored in a set. `operator()` then just checks membership.

Consequences worth knowing:

- Cost is paid once, at construction. Per-block evaluation is a hash set lookup.
- `LocalPos` here is region-local, computed from `block_entity.block_position` minus `region.bounds.origin`.
- Blocks without a block entity never match.
- `palette_idx` is ignored.
- The filter needs a materialized region only to compute local positions; it reads `block_entities` and does not touch `block_indices`.

## Example

```cpp
namespace fl = fschema::filters;

// Any block entity that has Items[0].id == "minecraft:diamond".
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

auto view_res = fl::MakeView(region, has_diamond, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}
view_res->for_each([](fl::LocalPos p, std::uint16_t palette_idx) {
  // ...
});

// Just check existence of a path.
fl::NbtQuery has_lock{
    .path = {{fl::NbtPathNodeType::kKey, "Lock", 0}},
    .op = fl::NbtOp::kExists,
};
```
