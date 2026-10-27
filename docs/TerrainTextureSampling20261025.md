# Terrain texture sampling and bandwidth — 25 October 2026

The user accepted the observed visual changes during the experiment. Only the repeatably faster normal-detail simplification was adopted. The faster shader remains the default, with **Detail Normal Maps off**. Smooth geometric-normal fields remain unchanged. Authored textures and bindings are retained, and both earlier appearances can be restored with material controls.

## Final clean implementation

| Matched Terrain6/7 view | GPU frame median |
|---|---:|
| Original shader fallback | 39.20 ms |
| Previous faster shader, detail maps on | 27.63 ms |
| New default, detail maps off | **24.76 ms** |

The new default saves **2.88 ms (10.4%)** versus the previous faster shader. The two new-default medians were 24.74 and 24.77 ms; previous-fast controls were 27.60 and 27.67 ms. This is about 40 GPU frames/second under these disabled-atmosphere/cloud test settings, rather than a promise for the full atmosphere-enabled scene or a game build. CPU frame times, enabled-pipeline timestamps and per-case data are in the accompanying JSON.

Under **Terrain Shader**:

- **Optimized Shader** stays enabled by default.
- **Detail Normal Maps** defaults off. Enable it to restore the previous faster shader's detail-normal appearance.
- Disable **Optimized Shader** to restore the original shader and authored normal settings, independently of the new detail switch.

The generator and material type expose the quality option. A small Lua material functor drives the SDK's normal-texture option for ordinary shading, and overlap sampling applies the same policy to its contributors. The SDK normal property group is locally declared with its texture bindings preserved and its original UseTexture functor replaced by the policy, so exactly one functor owns `o_normal_useTexture`. The existing `normal.useTexture` authoring setting remains respected for ordinary shading. Both FP16 and FP32 overlap functions preserve their old normal-map path when the original fallback is selected; this camera's runtime comparison used FP16. Existing owned material publications inherit the new default without clearing images, changing the saved scene or republishing.

## Appearance

Across 701,014 compared RGB pixels, the adopted setting changed **120,016 pixels**, almost entirely by one 8-bit RGB level. The maximum channel difference was **2 / 255**; only **60 pixels** differed by more than one level. The fixed HUD and gizmo mask is excluded. This is a quality trade accepted by the user, not a claim of mathematical equivalence. Geometry, smooth normal fields, overlap selection/blending, color textures, mip policy and ground patch blending remain unchanged.

Re-enabling detail normals matched the previous faster shader exactly. The original fallback matched its pre-change capture exactly. The new default also matched the earlier normal-removal diagnostic exactly. These checks cover this camera and scene; future terrain with deliberately strong normal textures should enable Detail Normal Maps when that authored detail is wanted.

## What the sampling investigation established

The existing ground shader already shares patch coordinates/gradients between aligned color and normal sampling. Owned height-query fields already use one R16F channel and early footprint rejection. Specular and AO maps are absent in this publication and use scalar values. Those are existing optimizations, not new gains.

The active baked color/detail-normal maps are 800×800. Cooked color products total 442,271 bytes each; cooked detail-normal products total 867,271 bytes each. The local SDK Albedo preset uses BC1 and Normals uses BC5s. The generated geometric fields are explicitly RGBA16F, 512×512 for Terrain6 and 128×128 for Terrain7, with unused alpha equal to one. Height fields are explicitly R16F, 256×256. The texture/source/cooked-product audit is in the JSON.

Crucially, the source detail-normal R/G channels contain only 127 or 128: Terrain6 has four XY pairs and Terrain7 two. They are almost flat. Replacing them with a tiny constant 4×4 normal image preserved nearly all appearance, but did not reproduce the speed gain from disabling the path. This points to the normal-map path's sampling instructions, frame/reconstruction math and compiled shader configuration as a group, rather than asset size/bandwidth alone. No hardware bandwidth counter or live driver ISA capture was taken, so the gain is not attributed to a measured number of memory transactions or to one specific instruction.

Initial captures from the clean-process comparison:

| Variant | GPU ms | Changed pixels | Maximum RGB difference |
|---|---:|---:|---:|
| Previous faster default | 27.56 | 0 | 0 |
| 4×4 constant detail maps | 27.50 | 421 | 1 |
| Detail normal maps removed | 24.93 | 120,016 | 2 |
| Half-size XY geometric-normal fields | 28.93 | 16,264 | 2 |
| Half-size octahedral geometric-normal fields | 28.86 | 17,993 | 4 |
| Coarser material mip probe | 28.87 | 177,175 | 15 |

Later controls drifted upward: the clean run's previous-fast baseline was 27.56, 27.77 and 28.94 ms; the no-normal control was 24.93 then 26.10 ms, while tiny maps were 27.50 then 28.72 ms. The final six-case implementation comparison provides the cleaner adoption measurement above. Compact fields and mip changes did not demonstrate a useful gain against bracketing controls. They were not shipped.

### Compact geometric-normal representations

Two experimental RG16F layouts reduced geometric-field payload from **2,228,224 bytes (2.1250 MiB)** to **1,114,112 bytes (1.0625 MiB)**, a 50% reduction. One stores exact original half-float XY and reconstructs positive Z; the other stores octahedral coordinates. The preparer verifies the current fields' positive-Z hemisphere. These are frozen-data prototypes, not a general replacement for arbitrary fields/transforms. The custom preset preserved linear signed RG16F and no mipmaps; cooked products were 1,055,896 and 72,856 bytes per representation.

Each field/representation was compared against 100,000 CPU bilinear samples. Terrain6 XY reconstruction had mean/p99/max angular error 0.016°/0.147°/1.32°; octahedral reconstruction was 0.021°/0.338°/4.83°. Hardware captures showed subtle but nonzero changes. Neither saved measurable GPU time or process resident memory in this sequence. Original fields stayed available for fallback, so smaller candidate payloads do not establish a net resident-memory saving.

### Ground and mip diagnostics

Selecting coarser material mips changed 177,175 pixels (maximum difference 15) without a useful speed gain. Disabling three-patch stochastic blending for ground and ground contact changed 429,896 pixels (maximum difference 164), while its first GPU result was 27.58 ms against a nearby 27.59 ms default. These were diagnostics, not appearance-preserving optimizations.

Changing the ground material also exposed stale cached ground appearance after the component binding was restored. Every capture after the first ground-single case in that initial run was excluded from optimization/appearance comparisons. The complete clean-process run and final policy validation used fresh editors and never switched ground bindings. Their fallback/default controls matched the pre-existing view exactly. This is not a claim that the ground cache-invalidation issue was fixed; no ground change was adopted.

## Memory

No new runtime textures, page cache or compressed geometric fields ship with this change. Original detail-normal assets remain bound/available for fallback, so their file payload is not claimed as a resident-memory saving.

In the final process, initial enabled-default rendering followed by the original-fallback control reported **1,813,868,544 bytes** of DXGI local usage. First enabling the previous detailed fast path raised that to **1,821,208,576 bytes**, a **7 MiB** increase, and it stayed resident when details were disabled again. The initial new-default allocation is therefore about 7 MiB below the previous fast configuration in this run; the normal-map assets themselves were not removed. These are process-level compiled-resource/residency observations on the integrated GPU, not dedicated VRAM, standalone per-shader allocations or a guaranteed universal saving. Memory high-water marks must not be interpreted as allocation rates or as proof that later cases require the entire retained pool.

## Controls and validation

Ryzen 7 5700G integrated graphics, 32 GiB RAM, Terrain6/7, 1203×656 viewport, 2× MSAA, FP16 heights. Camera position (1025.073242, 488.140839, 43.456932), rotation (-13.292666, 4.552838, 161.428360). Sky atmosphere, HDRI sky, cloud volumes and aerial atmosphere were disabled; lighting remained. Cases used 240 frame samples, 12 seconds initial warmup, 6 seconds settling, pass captures and 60 enabled-pipeline timestamp queries. No power or clock settings were changed.

All three editor runs completed their logical restoration checks. Clean and final visual checks separately verified that cached-ground contamination was absent. Camera, bindings, visibility, precision, pacing and atmosphere/cloud controls were restored. No level was saved; its SHA-256 remains `faec5fd3f9b69643b918af3a917623776d31bf7cc962485cfba712313a36a922`. Task-owned test editors were closed. Temporary materials, compact fields, tiny-map controls and their image preset were archived outside the active asset tree and removed from the project. The faster default and original fallback remain installed.

All **15 focused overlap tests passed**, including generated defaults, preserved normal image inputs, single functor ownership and delivery of the Lua/group assets. Shader/material asset jobs completed before final captures, canonical delivery matches the installed consumer, and diff formatting passed. This is not a full current regression-suite pass. The known nonfatal baseline image-index startup assertion remains outside this change.

[Raw measurements and restoration records](TerrainTextureSampling20261025.json). Experimental source, fields, materials and machine-specific reproduction scripts are retained under `tools/TextureSamplingPrototype/`. [Earlier faster-default rollout](TerrainShaderDefault20261024.md).
