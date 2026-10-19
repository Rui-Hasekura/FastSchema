# Inspectors

Inspectors read a region and return statistics, metadata, or validation
results. None of them write to `block_indices` or `palette`, and none of
them call `MarkEdited`. Every inspector header lives under
`fschema/inspectors/`.

## Shapes

Three shapes appear across the built-ins:

- Function template on `Filter F`, taking a `const BasicView<F>&`. Use
  when the inspector needs a selection, not the whole region.
- Non-template function taking `const ir::Region&`. Use when the
  inspector scans the whole region.
- Non-template function taking a type-erased `filters::View`. Use when
  the concrete filter type is irrelevant, or when the inspector is
  called from a context where the filter has already been erased.

`MakeView` accepts any `Filter`; the converting constructor of
`BasicView` performs the erasure to `View`.

## Materialization

An inspector that reads `r.block_indices` requires a materialized region.
The view-based form gets this from `MakeView`. For the
`const ir::Region&` form, call `ir::EnsureMaterialized` first.

Inspectors that only read `r.palette`, `r.entities`, or
`r.block_entities` do not require materialization.

## Lifetime

Some inspectors return `std::string_view` fields that point into the
region's owned byte buffer. Those views are valid only as long as the
region, and transitively its `Schematic::owner`, is alive. Copy the string
data if the result must outlive the region.

## Pages

- [Listing](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/inspectors/listing.md):
  `Count`, `ListMaterial`, `ListEntity`, `ListTileEntity`.
- [Diff](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/inspectors/diff.md):
  `Diff`.
- [Floating and support](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/inspectors/floating_support.md):
  `FloatingCheck`, `SupportCheck`.
- [Version](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/inspectors/version.md):
  `FindIncompatibleBlocks`, `GetMinimumRequiredDataVersion`,
  `GetHighestSupportedDataVersion`.

See [Custom Tools](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/custom_tools.md) for writing a custom inspector.


