# List Entity & Tile Entity

`fschema/inspectors/list_entity.h` and `fschema/inspectors/list_tile_entity.h` define `ListEntity` and `ListTileEntity`.

```cpp
struct EntityEntry {
  std::string_view id;
  std::array<double, 3> position;  // world-space
  std::array<double, 3> motion;
  std::array<float, 2> rotation;   // yaw, pitch
};

[[nodiscard]] inline std::vector<EntityEntry> ListEntity(
    const ir::Region& r,
    bool use_sort = false);

struct TileEntityEntry {
  std::string_view id;
  std::array<std::int32_t, 3> block_position;  // world-space
};

[[nodiscard]] inline std::vector<TileEntityEntry> ListTileEntity(
    const ir::Region& r,
    bool use_sort = false);
```

Both functions copy the common fields out of the region's entity vectors.

## ListEntity

Produces one `EntityEntry` per `ir::Entity`. `position` is world-space, already including the region origin; the internal representation stores positions relative to the region, and `ListEntity` does not undo that.

## ListTileEntity

Produces one `TileEntityEntry` per `ir::BlockEntity`. `block_position` is world-space.

## Sorting

With `use_sort = false`, entries keep their storage order.

With `use_sort = true`, entries are sorted by `id` ascending. This groups same-type entities together, which is convenient for printing or diffing.

The sort is a plain comparison of `std::string_view`; it is not locale-aware.

## Example

```cpp
namespace fi = fschema::inspectors;

auto entities = fi::ListEntity(region, /*use_sort=*/true);
for (const auto& e : entities) {
  // e.id, e.position, e.motion, e.rotation
}

auto tile_entities = fi::ListTileEntity(region, /*use_sort=*/true);
for (const auto& te : tile_entities) {
  // te.id, te.block_position
}
```

Neither function reads `block_indices`, so neither requires the region to be materialized.
