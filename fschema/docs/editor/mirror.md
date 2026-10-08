# Mirror & Rotate

`fschema/editors/mirror.h` and `fschema/editors/rotate.h` define `Mirror` and `Rotate`.

Both return a new `ir::Region`; neither modifies the source region's block data. Both optionally remap block-state properties to match the transformed geometry.

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

## Mirror

Reflects the selection along `axis`.

The selection is first extracted via `Extract`, so the input is a compact region whose bounds are exactly the selection bounds. Then the block indices are swapped in place along the mirror axis.

If the region is flat or empty along the mirror axis (`axis_dim <= 1`), the operation is a no-op and the extracted region is returned as-is.

The caller guarantees the structure does not contain blocks that become invalid when mirrored. The implementation does not check for unsupported blocks.

```cpp
namespace fl = fschema::filters;
namespace ed = fschema::editors;

fl::BoxFilter box{0, 0, 0, 9, 9, 9};
auto view_res = fl::MakeView(region, box, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}

auto mirrored = ed::Mirror(*view_res, fl::Axis::X, arena);
if (!mirrored) {
  // Handle ParseError.
  return;
}

// With state remapping:
auto mirrored_states = ed::Mirror(
    *view_res, fl::Axis::X, arena, ed::StateTransformMode::kTransformStates);
```

## Rotate

Rotates the selection by `steps * 90` degrees clockwise around `axis`.

`steps` is normalized to `[0, 3]` via `((steps % 4) + 4) % 4`. If the normalized value is `0`, the function returns `Extract(v, arena)` directly.

Rotation is applied iteratively: `steps` passes of a single 90-degree transform, each allocating a new buffer and swapping the bounds along the two non-axis dimensions. This avoids compound index math and bounds-swap errors.

The caller guarantees the structure does not contain blocks that become invalid when rotated.

```cpp
auto rotated = ed::Rotate(*view_res, fl::Axis::Y, 1, arena);
if (!rotated) {
  // Handle ParseError.
  return;
}
```

## State remapping

When `mode == StateTransformMode::kTransformStates`, the palette is rewritten after the geometric transform.

`TransformPalette` walks every `BlockState` with non-empty `raw_properties` and rewrites directional properties:

- `facing`: six directions (`north`, `south`, `east`, `west`, `up`, `down`).
- `axis`: `x`, `y`, `z`.

Two property encodings are handled: `kString` (`k1=v1,k2=v2`) and `kNbt` (raw NBT Compound). Unrecognized keys or values are left alone. If no value changed, the original span is kept and no arena allocation happens.

The mapping must match the geometric transform exactly, otherwise facing values are wrong after the swap. See `editors/internal/state_transform.h` for the direction tables.

## MarkEdited

Neither function calls `MarkEdited`. The source region is only read via `Extract`, and the output is a newly constructed region with `is_materialized = true`.
