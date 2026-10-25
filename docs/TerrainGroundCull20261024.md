# Terrain ground culling — 2026-10-24

This report records the culling phase before the subsequent [FP16 material arithmetic audit](TerrainFp16Audit20261024.md).

Retain the signed-distance expression requested by the user. The scene remains GPU limited; this change reduces discarded terrain work but does not demonstrate a frame-time improvement.

## Retained implementation

`VueCullGround` supplies `worldPosition.z - groundCutoff` to `SV_CullDistance0`. The rasterizer rejects a triangle only when every vertex is strictly below the plane. Crossing and coplanar triangles keep the original per-sample pixel discard. This replaces the temporary comparison/select expression without adding a branch, epsilon, or shifted cutoff. World-coordinate arithmetic stays float; height storage and height arithmetic precision selection are unchanged.

The terrain shader definitions are now owned and synchronized by terrain-compositor, with an explicit dependency include for reliable editor shader rebuilding. The original depth/shadow selection, MSAA interpolation, material/overlap blending, normal evaluation, vertex positions, indices, CLOD equations and geometry LOD settings remain intact.

## Editor comparison

Same open Editor process, camera, 1203×656 viewport and 2× MSAA. No level reload or save. Native performance captures use 240 frames (239 frame intervals); instrumentation is common to all rows. These are instrumented editor times, not an isolated GPU benchmark.

| Version | Case | Median frame ms | P95 ms | Approx. FPS |
|---|---|---:|---:|---:|
| control | control | 54.967 | 57.332 | 18.19 |
| control | control-repeat | 54.736 | 56.545 | 18.27 |
| comparison/select | ground-cull | 55.254 | 57.207 | 18.10 |
| comparison/select | ground-cull-repeat | 55.561 | 57.879 | 18.00 |
| signed distance | simple-cull | 55.115 | 57.499 | 18.14 |
| signed distance | simple-cull-repeat | 55.064 | 57.836 | 18.16 |
| signed distance | simple-cull-32 | 55.293 | 58.086 | 18.09 |
| signed distance | simple-cull-16-restored | 55.463 | 57.460 | 18.03 |

The signed-distance version is simpler, but its small timing difference from the comparison/select version is within run-to-run variation. Shader compilers may optimize these forms similarly; no claim about final AMD machine instructions is made.

The culler reduced discarded work: the comparison/select capture lowered forward pixel shader invocations from 1,364,609 to 1,312,882 and rendered primitives from 1,748,410 to 1,742,208. These pixels would otherwise reach the existing early ground discard, which explains why removing them does not remove much full material shading. Input geometry remains 2,426,734 primitives in forward/depth.

## Validation and preservation

The final signed-distance shader ran in both precision modes. R32 height publications used DXGI format 2; R16 publications used format 10. The final run restored 16-bit mode, material bindings, visibility, camera and original pacing. The saved level SHA-256 remained `70f2db2744cd2d2fa2c5808c157ca463913756744d1bae0025108b859cfc7a5d`.

The current-view image is identical outside the performance HUD and a 13×10 pixel transform-gizmo highlight region. See the JSON report for exact masks, counters, timings, camera and restoration evidence.

The reproducible hardware raster test passed 112 cases with zero changed pixels, including perspective, coplanar/below/crossing triangles, large world height and 1×/2× MSAA. It compiles the actual production cull helper into an isolated D3D11 fixture; full DX12 O3DE shader cooking and editor runs are separate checks. From a Visual Studio x64 developer prompt:

```text
python tests/tg/VerifyTerrainGroundCull.py --output <output-directory>
```

Native shader checks passed all 2/3/4-source × 16/32-bit production overlap combinations and confirmed native half/float height arithmetic. TGProject.Editor profile built successfully; the asset processor completed the live shader cook with zero failed editor jobs. Python regressions passed: 43 tests plus 20 subtests, and 53 unittest cases.

## Profiling findings and rejected experiments

The CPU trace spent about 42.9 ms waiting on the GPU fence, while scene preparation was about 1.0 ms. Diagnostic ablations in one run measured 55.1 ms normally, 30.5 ms with retained terrains hidden and 41.9 ms with the native terrain area disabled. Those ablations change occlusion and are not additive isolated pass costs. The native ground contributes 2,285,580 input primitives versus 141,166 retained terrain primitives; this is workload evidence, not proof that vertex arithmetic alone dominates. Forward-pass GPU timestamps are unavailable in this SDK capture path.

No active native height/surface stamp contributors were registered in the current compositor status; retained Vue Terrain6/7 and the procedural ground were active. Cache updates were idle in the stationary diagnostic capture. Final timing runs disabled cache diagnostics to avoid console-warning overhead. Earlier captures after terrain-area recreation had stale GPU envelopes and were excluded from the primary comparison.

Identity-LOD shortcuts, zero-contribution specular BRDF guards and overlap-footprint prechecks did not produce a repeatable frame-time improvement and were reverted. Hardware plane clipping with position-only depth paths failed the GPU coverage comparison at certain edges and was also reverted. Only whole-triangle culling with the original pixel paths remains.
