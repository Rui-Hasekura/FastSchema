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

The position check uses the block entity's `block_position` minus `region.bounds.origin`, and looks up the palette index at that local position for the filter call.

```cpp
for (auto& be : r.block_entities) {
  const bool id_matches = be.id.empty() || id_set.contains(be.id);
  if (!id_matches) continue;

  filters::LocalPos lp;
  lp.x = be.block_position[0] - r.bounds.origin[0];
  lp.y = be.block_position[1] - r.bounds.origin[1];
  lp.z = be.block_position[2] - r.bounds.origin[2];

  if (lp.x < 0 || lp.x >= sx || lp.y < 0 || lp.y >= sy || lp.z < 0 ||
      lp.z >= sz) {
    continue;
  }

  std::uint64_t linear_idx = filters::LinearIndex(lp, r.bounds);
  std::uint16_t pal = r.block_indices[linear_idx];

  if (!v.filter()(lp, pal)) {
    continue;
  }
  // ...
}
```

## Rewriting

For each matching block entity, `FilterAndWriteFields` iterates the raw NBT compound and writes everything except the names in `fields_to_erase` to a new `NbtWriter`. The resulting bytes are copied into `arena` and replace `be.raw_nbt`.

The filter is a plain name comparison against the top-level fields of the compound body. Nested fields are not inspected.

```cpp
base::NbtWriter writer;
auto res = ir::internal::FilterAndWriteFields(be.raw_nbt, fields_to_erase, writer);
if (!res) return std::unexpected(res.error());

writer.WriteEndTag();

std::vector<std::byte> new_payload = std::move(writer).Finalize();

void* mem = arena.Allocate(new_payload.size());
std::memcpy(mem, new_payload.data(), new_payload.size());
be.raw_nbt = std::span<const std::byte>(
    static_cast<const std::byte*>(mem), new_payload.size());
```

## MarkEdited

`EraseNbt` does not call `MarkEdited`. It only rewrites `block_entities`; `block_indices` is not modified.

## Example

```cpp
namespace fl = fschema::filters;
namespace ed = fschema::editors;

fl::BoxFilter all{0, 0, 0, region.bounds.size[0] - 1,
                  region.bounds.size[1] - 1, region.bounds.size[2] - 1};
auto view_res = fl::MakeView(region, all, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}

std::vector<std::string_view> ids = {
    "minecraft:chest",
    "minecraft:barrel",
};
std::vector<std::string_view> fields = {"Lock", "CustomName"};

ed::EraseNbt(*view_res, ids, fields, arena);
```
