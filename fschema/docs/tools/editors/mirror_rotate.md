# Mirror and Rotate

`fschema/editors/mirror.h` and `rotate.h` define `Mirror` and `Rotate`.
Both return a new `ir::Region` and do not modify the source region's block
data. Both optionally rewrite directional block-state properties to match
the transformed geometry.

```cpp
enum class StateTransformMode : std::uint8_t {
  kGeometricOnly = 0,
  kTransformStates = 1,
};

template <filters::Filter F>
[[nodiscard]] ParseResult<ir::Region> Mirror(
    const filters::BasicView<F>& v,
    filters::Axis axis,
    memory::Arena& arena,
    StateTransformMode mode = StateTransformMode::kGeometricOnly);

template <filters::Filter F>
[[nodiscard]] ParseResult<ir::Region> Rotate(
    const filters::BasicView<F>& v,
    filters::Axis axis,
    int steps,
    memory::Arena& arena,
    StateTransformMode mode = StateTransformMode::kGeometricOnly);
```

Neither function calls `MarkEdited`. The source region is only read through `Extract`; the output region is newly constructed with `is_materialized = true`.

## Mirror

Reflects the selection along `axis`. The selection is first extracted via `Extract`, so the input to the reflection step is a compact region whose bounds are exactly the selection bounds. Indices are then swapped in place along the axis.

If the region is flat or empty along the mirror axis (`axis_dim <= 1`), the operation is a no-op and the extracted region is returned as-is.

The caller is responsible for ensuring the structure does not contain blocks that become invalid when mirrored. `Mirror` does not check for unsupported blocks.

```cpp
#include "fschema/editors/mirror.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"

fl::BoxFilter box{0, 0, 0, 9, 9, 9};
auto view_res = fl::MakeView(region, box, *schematic.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

auto mirrored = ed::Mirror(*view_res, fl::Axis::kX, *schematic.arena);
if (!mirrored) {
  const fschema::ParseError& e = mirrored.error();
  return;
}
```

## Rotate

Rotates the selection by `steps * 90` degrees clockwise around `axis`.

`steps` is normalized to `[0, 3]`. A value of `0` returns `Extract(v, arena)` directly. Otherwise, the selection is extracted and indices are mapped to their rotated positions. The output bounds are updated for the new orientation.

The caller is responsible for ensuring the structure does not contain blocks that become invalid when rotated.

```cpp
auto rotated = ed::Rotate(*view_res, fl::Axis::kY, 1, *schematic.arena);
if (!rotated) {
  const fschema::ParseError& e = rotated.error();
  return;
}
```

## State remapping

With `mode == StateTransformMode::kTransformStates`, `TransformPalette` runs after the geometric transform and rewrites directional properties on every `BlockState` with non-empty `raw_properties`.

Recognized keys:

- `facing`: `north`, `south`, `east`, `west`, `up`, `down`.

- `axis`: `x`, `y`, `z`.

Both property encodings are handled: `PropertyEncoding::kString` (`k1=v1,k2=v2`) and `PropertyEncoding::kNbt` (raw NBT Compound). Unrecognized keys and values are left alone. When no value changed, the original span is kept and no arena allocation happens.

The mapping in `editors/internal/state_transform.h` must match the geometric transform exactly. A mismatch produces facing values that are wrong after the swap.

```cpp
auto mirrored_states = ed::Mirror(
    *view_res, fl::Axis::kX, *schematic.arena,
    ed::StateTransformMode::kTransformStates);
```


