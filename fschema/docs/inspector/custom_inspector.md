# Custom Inspector

A custom inspector is a function that reads a region and returns a result.
There is no base class. The contract is about what the function does not
do: it never writes to `block_indices` or `palette`, and it never calls
`MarkEdited`.

## Shape

Two forms appear in the built-ins:

- Function template on `Filter F`, taking a `BasicView<F>`. Used when the
  inspector needs a selection, not just the whole region. `Count` takes
  this form.
- Non-template function taking `const ir::Region&`. Used when the
  inspector scans the whole region and does not care about a filter.
  `ListMaterial`, `ListEntity`, `ListTileEntity`, and `SupportCheck` take
  this form. `Diff` takes two regions.

A function taking a type-erased `View` is a third form, used when the
inspector only needs to iterate and the concrete filter type is
irrelevant:

```cpp
using View = BasicView<std::function<bool(LocalPos, std::uint16_t)>>;
```

`FindIncompatibleBlocks`, `GetMinimumRequiredDataVersion`, and
`GetHighestSupportedDataVersion` take a `View`. `MakeView` accepts any
`Filter`, and `BasicView`'s converting constructor performs the erasure.

## Example: template on Filter

Counts non-air blocks per palette name:

```cpp
namespace fschema::inspectors {

struct MaterialCount {
  std::string_view name;
  std::size_t count;
};

template <filters::Filter F>
[[nodiscard]] inline std::vector<MaterialCount> CountByName(
    const filters::BasicView<F>& v) {
  const ir::Region& r = v.region();
  std::vector<std::size_t> counts(r.palette.size(), 0);
  v.for_each([&](filters::LocalPos, std::uint16_t pal) {
    if (pal < counts.size()) ++counts[pal];
  });
  std::vector<MaterialCount> out;
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (counts[i] > 0) {
      out.push_back({r.palette[i].name, counts[i]});
    }
  }
  return out;
}

}  // namespace fschema::inspectors
```

`v.region()` is used through a const reference here even though it returns
a non-const `ir::Region&`. Const-binding is enough because the inspector
does not write.

## Example: const Region&

Scans the whole region without a filter:

```cpp
namespace fschema::inspectors {

struct NonAirCount {
  std::uint64_t total;
  std::uint64_t blocks;
};

[[nodiscard]] inline NonAirCount CountNonAir(const ir::Region& r) {
  NonAirCount result{0, 0};
  if (r.palette.empty()) return result;
  for (std::uint16_t idx : r.block_indices) {
    if (idx < r.palette.size()) ++result.total;
    if (idx < r.palette.size() &&
        !base::IsAirVariant(r.palette[idx].name)) {
      ++result.blocks;
    }
  }
  return result;
}

}  // namespace fschema::inspectors
```

This form assumes `block_indices` is populated. See the materialization
note below.

## Return values

Returning `std::vector<Entry>` is the common shape for per-position or
per-palette results. Aggregates such as a single count, or a struct with
several counters, are fine too. `Diff` returns
`ParseResult<std::vector<DiffEntry>>` because it has a precondition that
can fail.

## Materialization

If the inspector reads `r.block_indices`, the region must be materialized
first. `MakeView` guarantees this for the view-based form. For the
`const ir::Region&` form, the caller is responsible:

```cpp
auto mat_res = ir::EnsureMaterialized(region, arena);
if (!mat_res) {
  // Handle ParseError.
  return;
}
auto result = MyInspector(region);
```

Inspectors that only read `r.palette`, `r.entities`, or `r.block_entities`
do not need the region to be materialized. `ListEntity` and
`ListTileEntity` fall into this category; `ListMaterial` and
`SupportCheck` do not.

## Lifetime

The entries returned by `ListEntity`, `ListTileEntity`, and `ListMaterial`
contain `std::string_view` fields that point into the region's owned byte
buffer. Those views are valid only as long as the region (and, transitively,
its `Schema::owner`) is alive. Copy the string data if the result needs to
outlive the region.
