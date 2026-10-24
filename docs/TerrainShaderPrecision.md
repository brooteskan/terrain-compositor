# Terrain shader precision

The **Terrain Compositor** entity's **Terrain Composition Gradient → Configuration → Height Precision** inspector setting selects both retained-terrain material height storage and arithmetic:

| Setting | Height texture | Height and overlap-weight arithmetic |
|---|---|---|
| 32-bit float (default) | RGBA32F | Native FP32 |
| 16-bit float | RGBA16F | Native FP16 |

This setting controls the retained-terrain appearance height query. It does not change mesh vertices, collision heights, the geometry LOD algorithm, terrain visibility, or camera settings. Switching regenerates immutable material/texture assets and publishes them after Asset Processor finishes. No editor restart is required after the updated native module has been loaded. Saving the level persists the inspector selection and material bindings.

The switch uses the `terrainOverlap.heightFp16` material property and the `o_terrainHeightFp16` shader option together. A 16-bit height texture must never be published with the 32-bit height resolver (or vice versa). The resolver has concrete half/float implementations; storing quantized values in a float does not count as FP16 arithmetic.

## Audit of 925be21

The previous commit reduced texture storage, but did not establish matching FP16 arithmetic. This correction covers:

| Data | Storage | Terrain-owned arithmetic |
|---|---|---|
| Retained terrain height query | Inspector selects RGBA16F/RGBA32F | Selected FP16/FP32 height scale/offset, coverage ramp, height differences, overlap weights |
| Retained terrain normal fields | RGBA16F | FP16 normalization, instance normal transform, tangent-frame reconstruction, accumulation |
| Graph clipmap color / roughness | RGBA16F | Half channel fields, validation, cache interpolation |
| Graph clipmap normal / metalness | RGBA16F | Half channel fields, validation, normal rotation, normalization and interpolation |
| Graph clipmap geometric normal | RGBA16F | Half output, normalization, dot/cross products and normal rotation |
| Graph clipmap factors / ownership tag / world height | RGBA32F | FP32; ownership tags require exact comparisons |

Texture coordinates, derivatives, world positions, cache addressing and the geometry LOD calculations remain FP32. These are coordinate/geometry computations, not texture-channel arithmetic. Engine PBR/lighting functions and arbitrary authored graph nodes retain their declared types; the material-channel interface performs explicit conversions at those boundaries. This is mixed precision, not a claim that every instruction in a complete terrain draw is FP16.

All repository-owned shader descriptors explicitly enable native 16-bit types. The gem's shader-build configuration also enables native half for Vulkan-generated material pipeline shaders; Windows DX12 already enables it in the SDK factory configuration. Existing backend arguments are retained.

## Verification

`tests/tg/VerifyNativeTerrainPrecision.py --engine <SDK> --output <folder>` compiles the actual production overlap resolvers through AZSLc and DXC and checks the DXIL arithmetic widths. It must find half arithmetic with no float arithmetic in the isolated FP16 resolver and the converse in the FP32 resolver.

The generated production DX12 shader containers are also inspected during validation. Normal-field, overlap, graph-forward and clipmap-update pixel shaders must contain native half operations. Float instructions elsewhere in those shaders are expected at the coordinate and engine lighting boundaries described above.

`tests/editor/verify_height_precision_inspector.py` exercises 32 → 16 → 32 through the real inspector property API, checks the resulting material option and texture formats, verifies visibility/camera/saved-level invariance, restores original in-memory bindings, and closes its dedicated editor without saving. It does not run a benchmark or change scene visibility to manufacture a test view.

FP16 has a smaller range and precision. Its visual effect is intentional when selected; no FPS improvement is asserted merely because shader types changed.

## Native module deployment

The TG integration wires `TGProject.Editor` to the two Gem editor targets so the usual TG build includes their native changes. Restart the editor after rebuilding. The editor loads the editor variants; runtime variants are built separately for game deployment and must not be added as editor module dependencies.

## Validated editor result (2026-10-24)

The inspector property API completed 32 → 16 → 32 for Terrain6 and Terrain7, with native material readiness confirmed at each step. The generated material option matched RGBA32F → RGBA16F → RGBA32F height textures. Camera, visibility and the saved level were unchanged; original in-memory settings/bindings were restored and the test editor exited without saving. See `TerrainShaderPrecisionVerification.json` for the recorded evidence. No performance improvement is claimed by this validation.

The existing compositor editor adapter incorrectly reported the runtime component's type as its reflection identity. This was corrected to the editor type so property-tree reads use the correct object layout.
