# Terrain shader precision

The **Terrain Compositor** entity's **Terrain Composition Gradient â†’ Configuration â†’ Height Precision** inspector setting selects GPU height storage and native height arithmetic for all terrain paths. The default is 32-bit. No level asset or imported source height data is rewritten when switching.

| Path | 32-bit setting | 16-bit setting |
|---|---|---|
| Retained Vue material height queries | RGBA32F height field, FP32 height/weight arithmetic | RGBA16F height field, native FP16 height/weight arithmetic |
| Heightmap stamp material queries | One FP32 sample per buffer word; FP32 interpolation/slopes/weights | Two binary16 samples per word; native FP16 interpolation/slopes/weights |
| Mesh height stamp material queries | FP32 samples and triangle interpolation | Binary16 samples and native FP16 triangle interpolation; same coverage/diagonal masks |
| Shared terrain geometry, including procedural/infinite terrain and composed stamps | R32_FLOAT heights in 8-byte interleaved height/normal records | R16_FLOAT heights in 4-byte records, including regular and CLOD streams; native half height interpolation |

CPU procedural generation, authoring samples, collision queries and ray-tracing acceleration inputs retain their existing precision. They are not GPU half-float computations. Retained Vue mesh topology and vertex assets are unchanged; their selected material height field follows the first row above. The selector does not convert arbitrary graph nodes or engine lighting to half.

Geometry LOD distances, grid resolution, sector indices, CLOD policy and missing-terrain semantics are unchanged. Both normalized height endpoints remain valid; -1 marks a missing vertex in either format. World/camera coordinates, cell addressing, mesh triangle selection and LOD-distance evaluation remain FP32. Selected normalized height interpolation uses native half before conversion to the FP32 world-position interface. Consequently, 16-bit mode intentionally exposes height quantization without changing LOD policy.

Switching precision clears incompatible geometry resources, drains outstanding transfer sources, rebuilds the matching input layout and invalidates the material cache. Ordinary camera motion retains the existing incremental/budgeted update path. Packed staging arrays are owned until their upload fences complete. Switching is an explicit rebuild and may briefly pause; it is not repeated per frame.

The geometry renderer is shared by a scene. If several terrain compositors request different precisions, shared geometry conservatively uses 32-bit. Set all compositors to the same setting to use 16-bit scene geometry. The inspector status shows the renderer's actual format and bytes per vertex and identifies a disagreement. Material-query publications retain their own selected precision.

Existing cache channel formats remain paired with their arithmetic:

| Other field | Storage | Shader arithmetic |
|---|---|---|
| Smooth normal fields | RGBA16F | FP16 normalization, instance transform and tangent frame |
| Graph clipmap color / roughness | RGBA16F | Half channel fields, validation and cache interpolation |
| Graph clipmap normal / metalness | RGBA16F | Half channel fields, rotation, normalization and interpolation |
| Graph clipmap geometric normal | RGBA16F | Half output, normalization and normal rotation |
| Graph clipmap factors / ownership tag / world height | RGBA32F | FP32; ownership tags require exact comparisons |

All repository-owned shader descriptors explicitly enable native 16-bit types. The gem's shader-build configuration also enables native half for Vulkan-generated material pipeline shaders; Windows DX12 already enables it in the SDK factory configuration. Existing backend arguments are retained.

## Verification

`tests/tg/VerifyNativeTerrainPrecision.py --engine <SDK> --output <folder>` compiles the actual production overlap resolvers through AZSLc and DXC and checks the DXIL arithmetic widths. It must find half arithmetic with no float arithmetic in the isolated FP16 resolver and the converse in the FP32 resolver.

The generated production DX12 shader containers are also inspected during validation. Normal-field, overlap, graph-forward and clipmap-update pixel shaders must contain native half operations. Float instructions elsewhere in those shaders are expected at the coordinate and engine lighting boundaries described above.

`tests/editor/verify_height_precision_inspector.py` exercises 32 â†’ 16 â†’ 32 through the real inspector property API, checks the resulting material option and texture formats, checks the live renderer format and 8 â†’ 4 â†’ 8 vertex stride, verifies visibility/camera/saved-level invariance, restores original in-memory bindings, and closes its dedicated editor without saving. It does not run a benchmark or change scene visibility to manufacture a test view.

FP16 has a smaller range and precision. Its visual effect is intentional when selected; no FPS improvement is asserted merely because shader types changed.

## Native module deployment

The TG integration wires `TGProject.Editor` to `Terrain.Editor` and the two compositor Gem editor targets so the usual TG build includes their native changes. Restart the editor after rebuilding. The renderer DLL must also be rebuilt after changes to its engine overrides; stale renderer code is ABI-incompatible. The editor loads the editor variants; runtime variants are built separately for game deployment and must not be added as editor module dependencies.

## Validated editor result (2026-10-24)

The inspector property API completed 32 â†’ 16 â†’ 32 for Terrain6 and Terrain7, with native material readiness confirmed at each step. The generated material option matched RGBA32F â†’ RGBA16F â†’ RGBA32F height textures. Camera, visibility and the saved level were unchanged; original in-memory settings/bindings were restored and the test editor exited without saving. See `TerrainShaderPrecisionVerification.json` for the recorded evidence. No performance improvement is claimed by this validation.

The existing compositor editor adapter incorrectly reported the runtime component's type as its reflection identity. This was corrected to the editor type so property-tree reads use the correct object layout.

## Expanded-path checks

`tests/tg/VerifyAllTerrainHeightPrecision.py --root <repo> --engine <SDK> --output <folder>` compiles the production stamp sampling and shared geometry height kernels through AZSLc/DXC. The 16-bit stamp and geometry kernels must contain half arithmetic; their 32-bit counterparts contain none. FP32 coordinate operations remain in the mixed kernels.

`tests/tg/VerifyTerrainHalfPacking.py --root <repo> --compiler-env <VsDevCmd.bat> --output <folder>` compiles the actual C++ packer and compares all 65,536 binary16 patterns plus 100,000 deterministic binary32 patterns against Python's independent IEEE oracle, including subnormals, signed zero, missing-height sentinel, ties and overflow.

Native unit coverage in `TerrainHeightPrecisionTests.cpp` checks both image and mesh source publications and selected packed sizes. `TerrainRenderingTests.cpp` checks R16/R32 regular/CLOD layouts and packed normal offsets. This installed SDK does not export `AZ::AzFrameworkTestShared`, so the full native unit target cannot be generated here; this limitation does not affect the normal editor build, standalone packer check, shader checks or editor acceptance run.

`tests/tg/VerifyTerrainStampPublications.py --root <repo> --project <TG> --engine <SDK> --compiler-env <VsDevCmd.bat> --output <folder>` compiles the production `TerrainOverlap.cpp` and the actual image/mesh publication test bodies into a standalone executable using the configured project's compiler includes. These checks passed for both precisions without requiring the missing full unit-test framework.

The expanded editor acceptance run passed 32 → 16 → 32: live renderer R32_FLOAT → R16_FLOAT → R32_FLOAT, 8 → 4 → 8 bytes per height/normal vertex, matching retained material fields, and unchanged camera, visibility and saved-level contents. Asset processing completed with 49 successes, zero failures and zero errors. The existing RenderPass image-input index assertion and DynamicDrawContext warnings remain; they predate this expansion. No timing improvement is claimed.

## Current scene throughput profile (2026-10-24)

The saved VueLiveLink view is GPU-bound on the current AMD Radeon(TM) Graphics adapter. At 1203 × 656, MSAA 2x and the user's saved 16-bit setting, repeated baseline captures measure about 63 ms/frame (roughly 16 engine FPS). The CPU trace spends about 50 ms/frame waiting on the GPU fence; terrain CPU rendering control averages about 0.8 ms per invocation. The stationary clipmap diagnostic records no updates or pending texels.

| Temporary diagnostic | Median frame interval | Approximate engine FPS |
|---|---:|---:|
| Full scene | 63.0 ms | 15.9 |
| Overlap shader, blend width temporarily zero | 53.0 ms | 18.9 |
| Existing base shader without overlap code | 44.6 ms | 22.4 |
| Both retained Vue terrain meshes hidden | 26.8 ms | 37.3 |
| Procedural terrain area disabled | 50.5 ms | 19.8 |

These diagnostic changes alter rendering temporarily; they were all restored. The base shader and zero-width overlap case have identical non-overlap material properties and the same 2,336,622 main-forward primitives. Their 8.4 ms difference identifies additional shader/material overhead beyond the executed blend calculations. Compiler/register pressure or specialization are candidates, not proven hardware-counter findings. Disabling smooth normals saves roughly 2 ms; changing stochastic ground sampling, frame pacing or graph clipmaps provides no comparable recovery.

The retained meshes use `VueTerrainOverlap.azsli` directly and do not sample the graph material clipmaps. The next target is their overlap shader: specialize inactive paths and avoid evaluating material channels twice before replacing them in the blend. Retained-material caching is a further option. Preserve blending and geometry LOD, then validate optimized images and timings; disabling features is not the proposed fix.

All three successful runs restored visibility, material bindings, camera and settings; the saved level hash remained unchanged. The raw captures are under `user/TerrainSceneProfile/`. [Structured findings and case statistics](TerrainSceneProfile20261024.json) record the measurements and limitations. Some Forward timestamps are unavailable, and GPU frame envelopes become stale after terrain-area recreation; those invalid intervals are excluded from attribution.

`tools/ProfileTerrainScene.py`, configured with `TC_SCENE_PROFILE_CONFIG`, reproduces the current-view comparisons without saving. `tools/AnalyzeTerrainSceneProfile.py <capture-folder> ... --output <summary.json>` summarizes native frame intervals, preserves duplicate pass-name ordinals, and flags stale GPU frame envelopes. Use a fresh output folder for every run. Profiling captures add overhead; this is not a new FP16/FP32 comparison or a complete camera-flight benchmark.
