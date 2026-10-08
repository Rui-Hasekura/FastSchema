# Version Check

`fschema/inspectors/version_check.h` defines three functions and the `IncompatibleBlock` result type.

```cpp
struct IncompatibleBlock {
  filters::LocalPos pos;
  std::string_view block_name;
  std::string_view reason;
};

[[nodiscard]] inline std::vector<IncompatibleBlock> FindIncompatibleBlocks(
    const filters::View& v,
    std::int32_t target_version);

[[nodiscard]] inline std::int32_t GetMinimumRequiredDataVersion(
    const filters::View& v);

[[nodiscard]] inline std::int32_t GetHighestSupportedDataVersion(
    const filters::View& v);
```

All three take a type-erased `View`. Construct one via `MakeView` and let the converting constructor erase the filter.

## Version model

The functions use a block-to-DataVersion table generated into `block_versions.inc`. Each entry records:

- `added_java`: the DataVersion the block first appears in.
- `removed_java`: the DataVersion the block was removed in, or `INT32_MAX` if it was never removed.

The valid DataVersions themselves are listed in `data_versions.inc`.

## FindIncompatibleBlocks

Returns one entry per block position whose palette entry is incompatible with `target_version`.

A palette entry is incompatible if:

- `target_version < added_java`, with reason `"Added in a later version"`.
- `target_version >= removed_java`, with reason `"Removed in this version"`.

Blocks whose names are absent from the table are treated as compatible and are not reported. Only palette entries actually used by the selection are scanned.

`pos` is region-local.

## GetMinimumRequiredDataVersion

Returns the maximum `added_java` over all used palette entries. Blocks absent from the table do not contribute. Returns `0` for an empty palette or an empty selection.

## GetHighestSupportedDataVersion

Returns the highest valid DataVersion that does not remove any used block.

The computation takes the minimum `removed_java` over used palette entries and finds the largest entry in `data_versions.inc` that is strictly less than that minimum.

Special cases:

- If any used block has `removed_java == 99` or `100`, returns `0`. These are pre-1.13 sentinels.
- If no used block has a finite `removed_java`, returns `INT32_MAX`. Every valid DataVersion is supported.

## Example

```cpp
namespace fi = fschema::inspectors;

auto view_res = fschema::filters::MakeView(region, filter, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}

fschema::filters::View view = *view_res;

auto incompatible = fi::FindIncompatibleBlocks(view, 3465 /* 1.20.1 */);
for (const auto& b : incompatible) {
  // b.pos, b.block_name, b.reason
}

std::int32_t min_ver = fi::GetMinimumRequiredDataVersion(view);
std::int32_t max_ver = fi::GetHighestSupportedDataVersion(view);
```
