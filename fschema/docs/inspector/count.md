# Count

`fschema/inspectors/count.h` defines `Count`.

```cpp
template <filters::Filter F>
[[nodiscard]] std::size_t Count(const filters::BasicView<F>& v,
                                std::string_view name);
```

Counts the selected blocks whose palette entry name equals `name`.

`name` is matched against `BlockState::name`. Properties are not considered; two palette entries with the same name and different properties both match.

Palette entries are matched against `name` once per call; per-block counting is a constant-time lookup.

```cpp
namespace fl = fschema::filters;
namespace fi = fschema::inspectors;

fl::IsAirFilter is_air{region};
auto solid = !is_air;

auto view_res = fl::MakeView(region, solid, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}

std::size_t stone_count = fi::Count(*view_res, "minecraft:stone");
```

The view must be materialized. Construct it via `MakeView`.

`Count` only counts; it does not return positions. For positions, iterate the view with `for_each` and compare names yourself.
