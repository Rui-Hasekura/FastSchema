# Count, ListMaterial, ListEntity, ListTileEntity

These four inspectors enumerate or count data. None of them modify the
region.

```cpp
template <filters::Filter F>
[[nodiscard]] std::size_t Count(const filters::BasicView<F>& v,
                                std::string_view name);

[[nodiscard]] inline std::vector<MaterialEntry> ListMaterial(
    const ir::Region& r, bool use_sort = false);

[[nodiscard]] inline std::vector<EntityEntry> ListEntity(
    const ir::Region& r, bool use_sort = false);

[[nodiscard]] inline std::vector<TileEntityEntry> ListTileEntity(
    const ir::Region& r, bool use_sort = false);
```

## Count

Counts the selected blocks whose palette entry name equals `name`. The name is matched against `BlockState::name`; properties are not considered. Two palette entries with the same name and different properties both match.

The palette is scanned once to build a LUT. Per-block counting is a constant-time lookup.

Requires a materialized region. The view-based form gets this from `MakeView`.

```cpp
#include "fschema/filters/block_name_filter.h"
#include "fschema/filters/view.h"
#include "fschema/inspectors/count.h"

fl::IsAirFilter is_air{region};
auto solid = !is_air;

auto view_res = fl::MakeView(region, solid, *schema.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

const std::size_t stone_count =
    fi::Count(*view_res, "minecraft:stone");
```

`Count` does not return positions. Iterate the view and compare names when  
positions are needed.

## ListMaterial

Enumerates every palette entry with its usage count.

```cpp
struct MaterialEntry {
  std::uint16_t palette_idx;
  std::string_view name;
  std::string properties;  // decoded, empty if none
  std::uint64_t count;
};
```

Counts are per palette index. Indices outside `palette.size()` are ignored. If the region is not materialized, `r.block_indices` is empty and every count is zero.

`properties` is a decoded string form:

- `kNbt`: converted to `k=v,k=v` via `ir::internal::NbtPropsToString`.

- `kString`: copied as-is from the raw span.

- `kNone`, or empty raw span: empty string.

With `use_sort = false`, entries are in palette order. With `use_sort = true`, entries are sorted by `count` descending; ties keep palette order.

```cpp
#include "fschema/inspectors/list_material.h"

auto materials = fi::ListMaterial(region, /*use_sort=*/true);
for (const auto& m : materials) {
  // m.palette_idx, m.name, m.properties, m.count
}
```

## ListEntity

Produces one `EntityEntry` per `ir::Entity`.

```cpp
struct EntityEntry {
  std::string_view id;
  std::array<double, 3> position;  // world-space
  std::array<double, 3> motion;
  std::array<float, 2> rotation;   // yaw, pitch
};
```

`position` is world-space and already includes the region origin.

Does not read `block_indices`, so no materialization is required.

## ListTileEntity

Produces one `TileEntityEntry` per `ir::BlockEntity`.

```cpp
struct TileEntityEntry {
  std::string_view id;
  std::array<std::int32_t, 3> block_position;  // world-space
};
```

`block_position` is world-space. Does not read `block_indices`, so no materialization is required.

## Sorting

For the three list inspectors, `use_sort = false` keeps storage order. `use_sort = true` sorts by `id` ascending. The sort is a plain `std::string_view` comparison; it is not locale-aware.