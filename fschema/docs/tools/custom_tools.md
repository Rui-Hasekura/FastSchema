# Custom Tools

Custom filters, editors, and inspectors use the same C++20 concepts as the
built-ins. There is no base class and nothing to register.

## Custom filter

A filter is any type that satisfies `fschema::filters::Filter`:

```cpp
template <class F>
concept Filter =
    std::regular_invocable<const F&, LocalPos, std::uint16_t> &&
    std::convertible_to<
        std::invoke_result_t<const F&, LocalPos, std::uint16_t>,
        bool>;
```

A lambda works if its `operator()` is const:

```cpp
auto diagonal = [](fl::LocalPos p, std::uint16_t /*idx*/) {
  return p.x == p.z;
};

auto view_res = fl::MakeView(region, diagonal, *schematic.arena);
```

A capturing lambda works as long as `operator()` is const. A `mutable` lambda does not satisfy the concept because its `operator()` is non-const. 
`RandomFilter` is non-const-callable and does not satisfy `Filter` either; see [Random](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/filters/random.md).

Two shapes of filter appear in the built-ins:

- LUT-only filters such as `BlockNameFilter`, `IsAirFilter`, and 
  `BlockStateFilter` read `region.palette` at construction and ignore 
  `block_indices` at evaluation. They work on an unmaterialized region.

- Region-reading filters such as `SurfaceFilter` and `NeighborFilter` read `region.block_indices` at evaluation. They require a materialized region.

A filter must not mutate the region. A filter that is correct only for a particular iteration order is fragile; the iteration order is not part of the contract.

Composite filters built with `&&`, `||`, and `!` are themselves filters. A custom filter composes with the built-ins without adaptation.

## Custom editor

An editor is a function that writes to a region. The usual shape:

```cpp
namespace fschema::editors {

template <filters::Filter F>
[[nodiscard]] ParseResult<void> Tint(filters::BasicView<F> v,
                                     std::string_view block_name) {
  ir::Region& r = v.region();

  const std::uint16_t target_idx = ResolveOrAppend(r, block_name);

  std::uint16_t* data = r.block_indices.data();
  v.ForEachLinear([&](std::uint64_t linear_idx, std::uint16_t /*pal*/) {
    data[linear_idx] = target_idx;
  });

  MarkEdited(r);
  return {};
}

}  // namespace fschema::editors
```

Rules:

- Any editor that writes to `block_indices` must call `MarkEdited(r)`. Without it, the encoder may take the lazy passthrough path and re-emit the original packed bytes.

- `ResolveOrAppend` and `ResolveAir` clear `palette_pristine` when they append a new palette entry. That alone is not enough; call `MarkEdited` regardless.

- `v.region()` returns `ir::Region&` even from a const view. The view holds the region by pointer; it does not own it.

- Use `ForEachLinear` when the callback only writes 
  `block_indices[linear_idx]`. Use `ForEach` when the callback also needs 
  `LocalPos`.

- If an editor both reads and writes the same region, collect the intended writes before applying any. Writing during the scan produces cascading updates. `Move` is the reference example.

- If an editor needs to materialize the region directly, call 
  `ir::EnsureMaterialized(r, arena)` and propagate the error.

Editors that do not call `MarkEdited`:

- Editors that return a new region and only read the input. `Extract`, `Mirror`, and `Rotate` follow this pattern.

- Editors that rewrite `block_entities` but not `block_indices`. `EraseNbt` is the reference example.

- Editors that resolve a palette entry but do not write block data. 
  `ReplacePaletteEntry` rewrites one palette entry and leaves 
  `palette_pristine` alone.

See [Palette Utilities](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/palette_utils.md) for `ResolveOrAppend`, `ResolveAir`, and `MarkEdited`.

## Custom inspector

An inspector reads a region and returns a result. It never writes to `block_indices` or `palette`, and it never calls `MarkEdited`.

Three shapes appear in the built-ins:

- Function template on `Filter F`, taking a `BasicView<F>`. Use this when the inspector needs a selection.

- Non-template function taking `const ir::Region&`. Use this when the inspector scans the whole region.

- Function taking the type-erased `View`. Use this when the concrete filter type is irrelevant.

```cpp
namespace fschema::inspectors {

template <filters::Filter F>
[[nodiscard]] inline std::vector<std::pair<std::string_view, std::size_t>>
CountByPalette(const filters::BasicView<F>& v) {
  const ir::Region& r = v.region();
  std::vector<std::size_t> counts(r.palette.size(), 0);
  v.ForEach([&](filters::LocalPos, std::uint16_t pal) {
    if (pal < counts.size()) ++counts[pal];
  });
  std::vector<std::pair<std::string_view, std::size_t>> out;
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (counts[i] > 0) out.emplace_back(r.palette[i].name, counts[i]);
  }
  return out;
}

}  // namespace fschema::inspectors
```

If the inspector reads `r.block_indices`, the region must be materialized. 
`MakeView` guarantees this for the view-based form. For the `const ir::Region&` form, call `ir::EnsureMaterialized` first.

Inspectors that only read `r.palette`, `r.entities`, or `r.block_entities` do not require materialization.

Results that contain `std::string_view` fields point into the region's owned byte buffer. Those views are valid only while the region, and transitively its `Schematic::owner`, is alive.


