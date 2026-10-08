# List Material

`fschema/inspectors/list_material.h` defines `ListMaterial` and `MaterialEntry`.

```cpp
struct MaterialEntry {
  std::uint16_t palette_idx;
  std::string_view name;
  std::string properties;  // decoded "k=v,k=v", empty if none
  std::uint64_t count;
};

[[nodiscard]] inline std::vector<MaterialEntry> ListMaterial(
    const ir::Region& r,
    bool use_sort = false);
```

Enumerates every palette entry with its usage count.

## Counts

Counts are produced by a dense array indexed by palette index. Indices outside `palette.size()` are ignored.

If the region is not materialized, `r.block_indices` is empty and every count is zero. Call `EnsureMaterialized` first when counts matter.

## Properties

`properties` is a decoded string form:

- `kNbt`: converted to `k=v,k=v` via `ir::internal::NbtPropsToString`.
- `kString`: copied as-is from the raw span.
- `kNone`, or an empty raw span: empty string.

## Sorting

With `use_sort = false`, entries are returned in palette order.

With `use_sort = true`, entries are sorted by `count` in descending order. Ties keep their relative palette order.

## Complexity

O(volume + K) time and O(K) space, where K is `palette.size()`. With `use_sort`, an additional O(K log K).

## Example

```cpp
namespace fi = fschema::inspectors;

auto materials = fi::ListMaterial(region, /*use_sort=*/true);
for (const auto& m : materials) {
  // m.palette_idx, m.name, m.properties, m.count
}
```
