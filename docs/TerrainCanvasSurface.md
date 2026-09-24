# Procedural terrain surfaces in Material Canvas

## Scope and renderer contract

The implemented portion of issue #15 is a terrain-wide surface graph over six
channels supported by Terrain's forward renderer. The goal is procedural terrain authoring
in Canvas without adding a second material renderer or rebuilding clipmap content
when a graph changes.

The graph receives the composed base color, world-space shading normal, perceptual
roughness, metalness, dielectric specular factor and diffuse ambient occlusion.
It runs after direct or clipmap sampling and existing distance fades, before decals
and lighting. Each disconnected Terrain Output socket inherits its incoming value;
a connected constant explicitly replaces it. World position and geometric normal
are also available for procedural masks. Final values are checked for finite values,
factors are clamped, and normals are normalized with an incoming-normal fallback.
Working RGB is ACEScg; valid overbright color is preserved.

The existing AO fade policy is retained. Roughness conversion/specular AA now uses
the final shading normal, including an authored procedural normal. Previously the
detail path calculated roughness before assigning its composed normal. This ordering
correction can change results when the optional specular-AA shader option is enabled.

The fixed TerrainMaterialSrg v1 layout remains intact. The new material-type property
`terrain.contractVersion = 2` identifies the surface contract; legacy tint contracts
remain accepted. Existing strength, color and single texture properties remain valid.
New arbitrary named properties and texture arrays are outside this implementation.

There are no new emissive, clear-coat, transparency, POM, displacement, collision or
per-detail-material graph features. Procedural height affects shading normals only.
The existing terrain parallax properties remain material-blend height controls.

## Implementation map

Paths below are relative to the repository root.

| Location | Change |
| --- | --- |
| `integrations/o3de/Assets/Shaders/Terrain/TerrainSurface.azsli` | Shared context/channel structures, finite-value and normal validation. |
| `integrations/o3de/Assets/Shaders/Terrain/TerrainForward.azsli` | Resolves incoming channels from either existing sampling path, calls the surface graph, then feeds the existing lighting code. Retains the legacy tint fallback. |
| `integrations/o3de/Assets/ShaderLib/TerrainSurface.azsli` and `ShaderLib/TerrainCompositor/TerrainSurface.azsli` | Include forwarders for the shared shader contract. |
| `integrations/o3de-material-canvas/Assets/MaterialCanvas/Terrain/Nodes/output.materialgraphnode` | Six inherited surface sockets; original UUID and hidden `inTint` compatibility connection retained. |
| Same `Nodes` directory: `surface_inputs` | Incoming terrain surface channels. New graphs use stock Canvas geometry and math. |
| `integrations/o3de-material-canvas/Assets/MaterialCanvas/Procedural/Nodes` | Reusable procedural operations and a normal-from-height node with explicit world position and base normal inputs. |
| `integrations/o3de-material-canvas/Assets/MaterialCanvas/Compatibility/Nodes` | Retained legacy UUIDs, fixed math defaults and implicit terrain normal adapter. |
| `integrations/o3de-material-canvas/Assets/ShaderLib/MaterialCanvas/Procedural` | Independent noise and surface-gradient normal helpers; old TerrainCanvas paths remain forwarders. |
| `integrations/o3de-material-canvas/Assets/MaterialCanvas/Terrain/Templates` | Generates `TC_CanvasSurface`, the forward entry wrapper and v2 material type while preserving generated filenames. |
| `integrations/o3de-material-canvas/EnginePatches/MaterialGraphCompiler.cpp.patch`, `EngineOverrides.cmake`, `CMakeLists.txt` | Hash-checked native compiler override: contextual values for disconnected sockets, plus an opt-in source-generation-only automation setting. No engine source edits. |
| `integrations/o3de-material-canvas/Code/Source/TerrainTintMaterialComponent.cpp` | Renames the UI to Terrain Material without changing serialization or bus identity. |
| `integrations/o3de/EngineOverrides/TerrainRenderer/TerrainTintMaterial.inl` | Recognizes the surface contract and reports surface selection status, preserving existing resource validation and handoff. |
| `integrations/o3de/Code/Source/Components/ProceduralGroundGradientComponent.cpp` | Suppresses legacy tint ownership for either Canvas contract. |
| `integrations/o3de-material-canvas/Assets/MaterialCanvas/Terrain/Examples` | Native-generated pass-through, wet terrain, procedural ground, slope/elevation, procedural normal and six-channel examples. Existing tint examples also regenerated. |
| `integrations/o3de-material-canvas/Tools/MigrateTintGraphs.py` | Optional backed-up, idempotent migration from hidden tint multiplication to a visible Base Color expression. |
| `integrations/o3de-material-canvas/Tests` | Hardware tint/surface tests and graph migration tests. |
| `integrations/o3de-material-canvas/Tools/CompileExamples.py`, `ValidateSurfaceEditor.py` | Fresh native generation reports and live direct/clipmap scene checks. |

The pinned compiler input comes from O3DE commit
`061180bf24f1666eb30315b35da292eb14f4659c`. A hash mismatch fails configuration
instead of silently patching another compiler revision. The override is generated
in the build directory and linked into MaterialCanvas; use that rebuilt executable.

## Authoring and compatibility

Start with `surface_passthrough.materialgraph` or `wet_terrain.materialgraph`.
Connect constants, stock world geometry inputs, incoming surface channels and math to Terrain
Output. Compile, wait for Asset Processor, and select the corresponding `.material`
in the Terrain Material component. Its status is `Canvas terrain surface active`.
The stock Canvas mesh preview is not a terrain preview; inspect the actual terrain.

Unconnected inherited sockets are intentionally not editable literals. Use a Constant
node to replace a channel. The reusable normal node takes explicit world position
and base world-space shading normal, scalar height in world meters, and dimensionless
strength. Examples filter small noise features using Pixel Footprint.
Effects authored after composition remain visible beyond the detail fade distance.

Old graph UUIDs, material filenames and material properties are preserved. Old
`inTint` connections continue to multiply incoming color exactly once. Migration is
optional and never modifies `.material` files. See the
[Canvas authoring guide](../integrations/o3de-material-canvas/README.md) for commands.

## Validation record (2026-09-23)

This is the original surface-extension record. The subsequent node-library,
migration, compiler-maintenance and ordinary-mesh checks are recorded in
[the follow-up validation report](TerrainCanvasFollowupValidation.md).

The implementation was built in TG against the pinned engine. MaterialCanvas,
TerrainCompositorCanvas, TerrainCompositor and Terrain Editor/runtime targets built
successfully. Existing MSBuild shared-intermediate-directory warnings remain.

The native compiler generated all ten checked-in example graphs. Asset Processor's
shader, material-type and material jobs completed with zero errors for those examples.
The corrected graphs were processed by the actual AZSL/DX12 pipeline, not merely a
standalone HLSL test. Generated outputs were copied back to the primary checkout.
The user's TG procedural tint graph and material were preserved; the clean upstream
graph was compiled separately for the compatibility test.

All three CTest suites passed:

- Legacy tint parity: 1,179,648 GPU pixel pairs across strengths and derivative
  footprints; maximum error 0.
- Surface hardware tests: native disconnected output and all six explicit channels;
  analytic world-space bump normal; zero-strength and degenerate derivatives;
  invalid-value fallback, factor bounds and overbright color preservation.
  Maximum error 0.00000107288.
- Migration: three cases covering connection/constant preservation and idempotence.

The surface test uses complete derivative quads when selecting which channel to
read back. Divergent per-pixel output selection is not a valid derivative test.

Headless generation can use
`--regset=/O3DE/TerrainCanvas/SourceGenerationOnly=true`. This skips the native
asset-status wait, which can hang after successful Asset Processor jobs; it does
not skip shader generation or claim shader compilation success. Inspect fresh
generation hashes and Asset Processor results separately. The pinned tool also has
an intermittent shutdown access violation already observed during the tint work:
the corrected all-example run generated successfully but exited with `0xC0000005`;
separate source-generation runs exited cleanly. This is not reported as a clean
all-example tool-process exit.

Build, native generation, asset-job and GPU test evidence is retained locally under
`build/issue15`. Live scene captures/results are under TG's
`TGProject/user/TerrainSurfaceValidation`.

The saved TG DefaultLevel passed the live Editor validator in both direct and
clipmap modes. It selected pass-through, wet terrain, procedural ground,
slope/elevation and procedural normal materials at `(16,16,32)` and
`(16,-180,180)` meters. Incompatible material candidates retained the active
surface in both modes. The selected surface also activated in game mode.
All temporary property/material changes were restored without saving the level.

A graph-reload check changed wet terrain's color expression to blue, compiled
through native Canvas, required a newer successful Asset Processor shader job,
and observed the changed surface in the running Editor. Restoring and recompiling
the original graph restored the exact image below the diagnostic overlay. Both
compiler processes exited with code 0. No Editor restart was needed.

A separate stationary-camera pass-through comparison disabled legacy tint and
captured baseline, Canvas pass-through and restored baseline at each distance in
each sampling mode. Comparisons include all RGB pixels below row 160, excluding
the diagnostic overlay, at 1562 by 839 resolution:

| Sampling | Distance | Maximum 8-bit channel difference | Changed pixels |
| --- | --- | ---: | ---: |
| Direct | Near | 0 | 0 |
| Direct | Far | 0 | 0 |
| Clipmap | Near | 1 | 2 |
| Clipmap | Far | 0 | 0 |

These results held against both the initial and restored baseline. The initial
traversal had a larger near-view difference while the view settled; the focused
stationary comparison waits 600 frames after moving and 360 after each selection.
Use `TC_CANVAS_SURFACE_PARITY_ONLY=1` to run this comparison separately. Its
captures are in the validation directory's `parity` subdirectory.

The main scene run collected 36 successful GPU timestamp captures. Whole Forward
pass samples ranged from 2.380 to 3.525 ms across materials/modes. These samples
contain other scene rendering and do not establish isolated graph overhead.

## Verification boundaries

The hardware tests verify channel math, not every terrain composition configuration.
Concurrent scenes, nonzero admitted height gaps, mapped detail-material mixtures, collision
probes and incompatible shader-layout reloads require a broader regression scene
matrix. Existing selection, publication and collision algorithms were not redesigned.
GPU timestamps from the validator include the rest of the scene; they are diagnostic
samples, not an isolated or statistically controlled graph-cost benchmark.
