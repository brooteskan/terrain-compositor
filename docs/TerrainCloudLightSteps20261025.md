# Cloud light-step sweep — 2026-10-25

Compared 16, 8, 4 and 2 effective light steps in the user's existing editor at the exact prior cloud-and-terrain camera and 1267×761 viewport, MSAA 2×, FP16. New terrain shader/default detail-normal policy stayed unchanged. Atmosphere, aerial atmosphere and both volumetric clouds were active and verified for every measurement. Camera and viewport equality were asserted before starting and for every capture.

Two 240-frame captures per setting, ordered 16→8→4→2→2→4→8→16, with six seconds of settling and sixty native pass samples per case. GPU figures are the mean of the two capture medians. The same profiling instrumentation was used throughout. These instrumented times should not be equated with the uninstrumented editor HUD (roughly 210 ms at the original settings).

| Light steps | GPU frame ms | Cloud pass ms | Frame reduction | Max RGB difference /255 | Cloud-region mean absolute RGB difference /255 |
|---|---:|---:|---:|---:|---:|
| 16 | 282.31 | 245.88 | 0.0% | 0 | 0.000 |
| 8 | 243.17 | 206.72 | 13.9% | 11 | 0.517 |
| 4 | 222.64 | 186.27 | 21.1% | 25 | 1.902 |
| 2 | 217.55 | 181.39 | 22.9% | 40 | 2.709 |

At this fixed camera, 8 steps is the conservative candidate: a repeatable 39.14 ms (13.9%) frame reduction with a smaller shading change. Four steps saves 59.67 ms but changes cloud interior lighting more visibly. Two steps saves only another 5.09 ms over four while producing the largest shading change. Even the two-step cloud pass remains about 181 ms, so reducing secondary light samples alone does not approach a 30 fps budget; main view-march cost is a useful next experiment. These judgments are limited to these static captures, not camera-motion validation. All cloud settings were restored; no lower setting was adopted.

View steps remained 192. Solar-disc samples, receiver-shadow light steps (32), sun settings, density grids, terrain bindings, precision, pacing and all other effective lighting values remained unchanged. The light-step control also derives sky/ground ambient sample counts as max(2, lightSteps/2), so this is a combined secondary-lighting quality sweep, not a sun-only sample reduction. At 16/8/4/2 light steps, ambient marches use 8/4/2/2 samples each.

DXGI local process resident usage observed across all cases: 1768.71 MiB. R32 cloud density payload remained 4,711,056 bytes (4.493 MiB). Per-case RHI pool used/resident figures are in the JSON. No grids were resized and no new textures or cache were allocated. Process memory includes retained allocations from the existing editor; it is not an independent fresh-process comparison.

Appearance measurements compare each capture against light-16.png in RGB byte space, excluding the top-right HUD and bottom-left axis area for this viewport. Overall mask has 856,687 pixels. The fixed cloud region is x=100..759, y=120..609, also subject to that mask; it is not a semantic cloud segmentation. Pixel metrics do not establish temporal stability or correctness against a high-sample reference. The two captures at each setting matched exactly within the comparison mask, including the restored 16-step reference. Full repeat metrics are in the JSON.

Native-scale cloud crops: [comparison sheet](../tools/CloudLightSteps20261025/cloud-crops.png). Full captures and raw results: `D:/diversion/TG/TGProject/user/TerrainSceneProfile/20261025-cloud-light-steps`. The original 16-step setting is the reference, not a claim of physically converged lighting.

Restoration passed for the original 16 light steps and effective lighting, atmosphere/cloud component states, aerial setting, profiling flag, material bindings, camera, visibility, height precision and saved-level hash. Terrain shader hash is unchanged; optimized=true, detailNormals=false, contributor key=0 remain default. Original terrain fallback remains available. User editor stays open. No source shader edits, asset rebuilds or regression tests were needed for this reversible settings experiment.
