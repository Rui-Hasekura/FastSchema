# EraseNbt

`fschema/editors/erase_nbt.h` defines `EraseNbt`.

```cpp
template <filters::Filter F>
[[nodiscard]] ParseResult<void> EraseNbt(
    filters::BasicView<F> v,
    std::span<const std::string_view> target_ids,
    std::span<const std::string_view> fields_to_erase,
    memory::Arena& arena);
```

Removes specific top-level fields from the NBT data of selected block entities.

## Matching

A block entity is processed if both conditions hold:

1. Its `id` matches `target_ids`. An empty `id` is treated as a match, so block entities with missing IDs are included.

2. Its block position, converted to local coordinates, is within the region bounds and satisfies the view's filter.

The position check subtracts `region.bounds.origin` from the block entity's `block_position`, looks up the palette index at that local position, and calls the filter with `(local_pos, palette_idx)`.

## Rewriting

For each matching block entity, `ir::internal::FilterAndWriteFields` iterates the raw NBT compound and writes every field except the names in 
`fields_to_erase` to a new `base::NbtWriter`. The writer output is closed with a trailing `End` tag, finalized to a byte vector, and copied into `arena`. `be.raw_nbt` is replaced with the arena-owned span.

The filter compares top-level field names only. Nested fields are not inspected.

## MarkEdited

`EraseNbt` does not call `MarkEdited`. It only rewrites `block_entities`; 
`block_indices` and `palette` are unchanged. The encoder reads 
`block_entities` at serialization time, so there is no stale-data path.

## Example

```cpp
#include "fschema/editors/erase_nbt.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"

fl::BoxFilter all{0, 0, 0,
                  region.bounds.size[0] - 1,
                  region.bounds.size[1] - 1,
                  region.bounds.size[2] - 1};
auto view_res = fl::MakeView(region, all, *schematic.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

std::vector<std::string_view> ids = {
    "minecraft:chest",
    "minecraft:barrel",
};
std::vector<std::string_view> fields = {"Lock", "CustomName"};

auto res = ed::EraseNbt(*view_res, ids, fields, *schematic.arena);
if (!res) {
  const fschema::ParseError& e = res.error();
  return;
}
```

`target_ids` and `fields_to_erase` are read during the call. The strings they point into only need to outlive the call, not the region.
