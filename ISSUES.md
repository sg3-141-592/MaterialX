# Hydra/Cycles Preview Integration: Findings and Potential Upstream Issues

This document records issues found while adding the Hydra preview backend to
`MaterialXGraphEditor`, using the bundled Blender/Cycles `hdCycles` Hydra
delegate. It separates problems in this integration from missing or surprising
behaviour in `hdCycles`, Cycles, USD, or Hydra.

The native `Cycles` backend and `GLSL` backend are used as references.

## TL;DR for Maintainers

1. `hdCycles` cannot create `UsdUVTexture` (or `mapping`) nodes unless the Cycles
   node type is force-registered first; `NodeType::find()` is used before lazy
   registration has happened.
2. `hdCycles` has no `UsdTransform2d` mapping, although the token is defined.
3. `hdCycles` does not advertise an `albedo` AOV, so `HdxTaskController` drops
   the request.
4. `HdxTaskController` always requests `elementId` for non-Storm delegates, but
   hdCycles does not implement it, producing repeated warnings.
5. The `UsdUVTexture` V-axis convention differs from the native MaterialX OSL
   image lookup; the graph editor compensates with a V flip.
6. An empty `params.denoise_device` triggers a `DeviceInfo` assertion.

The incomplete shaderball UV set was a graph-editor Hydra stage bug, not an
upstream issue.

## Current Status

Local commits:

- `0eb2bd4b Match Hydra texture UVs to the native preview`
- `f063e95d Preview image textures in the Hydra Cycles backend`
- `0170d48c Add an albedo pass to the Hydra Cycles backend`

The Hydra preview now:

- renders all meshes and partitions from the `GeometryHandler`;
- applies `texcoord` / `place2d` texture transforms;
- matches native Cycles for textured and untextured UV-gradient test cases;
- shows the full greysphere calibration ring in both `Combined` and `Albedo`;
- supports an `Albedo` render pass on `Hydra (Cycles)`.

## Fixed Locally: Incomplete Mesh/Partition Set (Not Upstream)

### Symptom

Textured `Hydra (Cycles)` previews showed only part of the calibration ring, or
a gray sphere with `place2d` applied. Native `Cycles` and `GLSL` showed the
full ring.

### Cause

The shaderball GLB contains two meshes:

- mesh 0: 36,228 vertices
- mesh 1: 9,140 vertices

The Hydra stage initially authored only `geometryHandler->getMeshes()[0]` and
`partition(0)`. Native Cycles merges every mesh and partition, so Hydra had an
incomplete geometry *and* an incomplete UV set. The second mesh contains UVs
needed by the `place2d` transform.

### Fix

`HydraRenderPipeline::Impl::buildStage()` now accumulates all meshes and
partitions, applies the combined bounding-box normalization, and writes one
merged `st` primvar. This is an integration bug, not a USD/Hydra/Cycles issue.

## Potential hdCycles Issues

These are candidates for an upstream issue or patch against the Blender/Cycles
Hydra delegate. The local workarounds are in
`scripts/patches/cycles-hydra-usd.patch`.

### 1. Image Texture Node Types Are Not Registered On Demand

**File:** `src/hydra/material.cpp`, `HdCyclesMaterial::PopulateShaderGraph()`

**Symptom**

A USD material network containing `UsdUVTexture` fails with:

```text
Runtime Error: in PopulateShaderGraph at line ... of .../src/hydra/material.cpp
-- Could not create node '/Model/Material/Texture_diffuseColor'
```

The same happens for Cycles' `mapping` node, so a hypothetical
`UsdTransform2d` support path also fails.

**Cause**

Cycles registers node types lazily. `NodeType::find("image_texture")` and
`NodeType::find("mapping")` return null unless the corresponding
`get_node_type()` has already run. hdCycles uses `NodeType::find()` directly in
`PopulateShaderGraph()`, so a material network is the first user of
`image_texture`, and the lookup fails.

**Local workaround**

Before falling back to an error, force registration for the types used by USD
material networks:

```cpp
const NodeType *nodeType = NodeType::find(cyclesType);
if (!nodeType) {
  if (cyclesType == "image_texture") {
    ImageTextureNode::get_node_type();
  }
  else if (cyclesType == "mapping") {
    MappingNode::get_node_type();
  }
  nodeType = NodeType::find(cyclesType);
}
```

**Suggested upstream fix**

Call the relevant `get_node_type()` from the node-type lookup path, or expose a
small registered-type helper that hdCycles can use instead of `NodeType::find()`.

This is the reason a plain `UsdUVTexture` network does not work with hdCycles
out of the box.

### 2. `UsdTransform2d` Is Not Mapped

**File:** `src/hydra/material.cpp`, `UsdToCycles::findUsd()`

`hdCycles` defines the `UsdTransform2d` token but has no `UsdToCycles` mapping
for it. A `UsdTransform2d` node in a material network therefore cannot be
created.

A working mapping needs to account for:

- `in` -> Cycles `mapping.Vector`
- `translation` -> Cycles `mapping.Location`
- `scale` -> Cycles `mapping.Scale`
- `result` -> Cycles `mapping.Vector` (the output names differ)
- `float2` translation/scale values need to be accepted for the Cycles
  `float3` sockets.

The graph editor currently avoids this by baking `place2d` into per-texture UV
primvars before authoring the USD material.

**Suggested upstream fix**

Add a `UsdToCyclesTransform2d` mapping and a `float2` -> `float3` conversion
for the relevant sockets. `UsdTransform2d` is the standard USD way to transform
`st` coordinates, so other Hydra delegates will eventually need the same support.

### 3. No `albedo` AOV

**File:** `src/hydra/render_delegate.cpp`,
`HdCyclesDelegate::GetDefaultAovDescriptor()`

`HdCyclesDelegate::GetDefaultAovDescriptor()` advertises color, depth, normal,
and ID AOVs, but not `albedo`. `HdxTaskController::SetRenderOutputs()` drops any
AOV whose descriptor format is `HdFormatInvalid`, so a graph editor request for
an `albedo` pass silently disappears.

**Local workaround**

Advertise `albedo` as `HdFormatFloat32Vec4` and map it to
`PASS_DIFFUSE_COLOR` in `src/hydra/session.cpp`:

```cpp
{ TfToken("albedo"), PASS_DIFFUSE_COLOR },
```

`Hydra` has no standard `HdAovTokens->albedo`, so the graph editor and hdCycles
use the token string `"albedo"`.

**Suggested upstream fix**

Add an albedo/diffuse-color AOV descriptor and pass mapping, or document the
supported custom AOV naming convention.

### 4. `elementId` AOV Warning for Non-Storm Delegates

**File:** `src/hydra/session.cpp`, `HdCyclesSession::SyncAovBindings()`

`HdxTaskController` always requests an `elementId` AOV for non-Storm delegates,
but hdCycles does not implement it. This produces repeated warnings:

```text
Warning: in SyncAovBindings at line ... of .../src/hydra/session.cpp
-- Unknown pass elementId
```

**Local workaround**

Ignore `HdAovTokens->elementId` silently in `SyncAovBindings()`.

**Suggested upstream fix**

Either implement the AOV, or explicitly mark it as an expected unsupported AOV
so delegates do not need private suppression.

### 5. Texture V-Axis Convention Differs From Native MaterialX OSL

**Files:** `src/hydra/material.cpp` (`UsdUVTexture` mapping),
native Cycles OSL image path in `CyclesRenderPipeline`

**Observation**

For the same mesh UVs, a raw UV-gradient image rendered through:

- native MaterialX -> Cycles OSL image lookup, and
- hdCycles -> `UsdUVTexture` image lookup

does not match unless the V coordinate is flipped before texture lookup. The
graph editor compensates with:

```cpp
v = 1.0f - v;
```

This is separate from the missing mesh/partition bug: the flip is still needed
after the geometry is merged.

**Suggested upstream investigation**

Determine whether this is an intentional OSL-vs-SVM convention or an hdCycles
bug. If it is unexpected, the fix should live in one of:

- hdCycles' `UsdUVTexture` handling, or
- the native MaterialX OSL image transform helper (`mx_transform_uv`), or
- the Hydra `st`/texture-coordinate plumbing.

A minimal repro is a `UsdUVTexture` network and a UV-gradient image, rendered
with native Cycles OSL and with hdCycles.

### 6. Empty Denoise Device Triggers an Assertion

**File:** `src/hydra/render_delegate.cpp`, `GetSessionParams()`

The default empty `params.denoise_device` triggers an assertion in `DeviceInfo`
equality checks. The local workaround is:

```cpp
params.denoise_device = params.device;
```

**Suggested upstream fix**

Default the denoise device to the render device, or tolerate an empty denoise
device in `DeviceInfo` comparisons.

## Minimal Reproduction Cases

### hdCycles image texture / lazy node registration

Material:

- a `standard_surface` whose `base_color` is an `image` node;
- the image has a valid file and UV coordinates.

Run with Hydra Cycles. Without the registration workaround the log contains:

```text
Could not create node '/Model/Material/Texture_<name>'
```

### hdCycles UV convention / `place2d`

Textures:

- `standard_surface_greysphere_calibration.mtlx` (image + `place2d`);
- a UV-gradient image (`u` in R, `v` in G) with and without `place2d`.

Renders to compare:

- `--renderBackend "Cycles" --renderPass albedo`
- `--renderBackend "Hydra (Cycles)" --renderPass albedo`

The calibration ring and the gradient pattern must match after the mesh merge
and V flip.

### hdCycles albedo AOV

```bash
./build/bin/MaterialXGraphEditor \
  --material resources/Materials/Examples/StandardSurface/standard_surface_greysphere_calibration.mtlx \
  --mesh resources/Geometry/shaderball.glb \
  --renderBackend "Hydra (Cycles)" \
  --renderPass albedo \
  --samples 8 \
  --captureFilename /tmp/albedo.png
```

Without the albedo descriptor/pass patch, the pass is dropped by
`HdxTaskController`.

## Not Upstream

- The missing second shaderball mesh/partition was a graph-editor Hydra stage
  bug, not a USD/Hydra/Cycles defect.
- The temporary yellow albedo seen during development was likely the texture sampling
  an incomplete mesh/UV set; after merging the meshes and applying `place2d`,
  the calibration albedo is neutral gray with the correct colored ring.

## Validation

Validated locally:

- native `Cycles`, `GLSL`, `Hydra (Cycles)`, and `Hydra (GL)` backends build;
- `Hydra (Cycles)` combined and albedo display the full calibration ring;
- UV-gradient output matches native Cycles for `place2d` and no-`place2d`
  materials;
- `Hydra (GL)` falls back to the non-textured material path without warnings;
- native `Cycles` and `GLSL` are unchanged.
