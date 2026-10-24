# Retained terrain overlap optimization, 2026-10-24

The final fresh editor runs improved from **63.1 ms / 15.9 FPS** to
**51.4 ms / 19.5 FPS**, an **18.5% reduction in frame time**.
Initial optimized repeats measured 47.0-47.1 ms, so the observed optimized range is
47-51 ms. The captured adapter is **AMD Radeon(TM) Graphics**. These results describe this scene and device, rather than a guaranteed
speedup on other hardware. All captures use the saved VueLiveLink camera, both
Terrain6 and Terrain7 visible, 1203x656, MSAA 2x, and unchanged geometry LOD.

## Change

`terrainOverlap.count` now connects to the integer shader option
`o_terrainOverlapSourceCount`, alongside the existing precision option. Bounded,
unrolled loops let the pipeline compiler remove inactive sources. The shader no
longer retains four complete source-material structs across height selection;
it retains the queries and fetches material inputs for participating sources.

Contributor selection now precedes base-material evaluation. Outside an overlap,
the StandardPBR endpoint still runs. At intersections, the replacement channels
are evaluated only by the weighted contributor loop. Alpha, lighting channels,
specular occlusion, and opacity response remain present; emissive/clear-coat edits
retain the complete StandardPBR initialization path. Texture derivatives are
captured before varying branches and supplied through SampleGrad. Publication
checks enforce the existing baked UV0 contract for compound occlusion properties
as well as simple texture-UV properties.

This SDK patches **native specialization constants at pipeline creation** on DX12
and Vulkan. StableId 0 is normal on these backends and does not establish that a
generic shader is running. Precision was already specialized; contributor count
was previously a runtime SRG input. No explicit variant-list files are required.
The emitted DX12 option IDs are 52 for count and 51 for precision. The verification
tool compiles all 2/3/4-source x 16/32-bit combinations from cooked production HLSL.
Its offline fixed-constant binaries are validation artifacts, not captured GPU
machine code or substitutes for the actual editor pipeline.

The overlap weighting, height queries, normal reconstruction, ground transition,
texture addressing, and arithmetic precision policies are preserved. Geometry,
indices, CLOD, render distance, cache scheduling, and geometry LOD settings are
untouched. The existing precision selector continues selecting matching storage
and arithmetic; this change does not claim a new FP16 throughput advantage.

## Editor measurements

| Capture / case | Median ms | P95 ms | Approx. FPS |
|---|---:|---:|---:|
| overlap-before / baseline | 63.138 | 66.106 | 15.84 |
| overlap-before / baseline-repeat | 63.016 | 64.768 | 15.87 |
| overlap-after / optimized | 46.968 | 48.900 | 21.29 |
| overlap-after / optimized-width-zero | 45.344 | 47.640 | 22.05 |
| overlap-after / optimized-repeat | 47.145 | 49.880 | 21.21 |
| overlap-precision-final / optimized-32 | 47.217 | 49.276 | 21.18 |
| overlap-precision-final / optimized-16 | 50.465 | 52.377 | 19.82 |
| overlap-final / final-16 | 51.407 | 54.467 | 19.45 |
| overlap-final / final-16-repeat | 51.406 | 52.633 | 19.45 |

Each batch requested 180 frames and contains 179 valid native frame intervals.
The metric is **Engine Cpu Time**, including GPU/render waits. It is not isolated
CPU utilization or Forward shader GPU duration. GPU frame envelopes were usable
in these captures; the SDK still does not expose valid Forward pass timestamps.
No compiler was active during the reported batches. Timing varies between editor
sessions; the later fresh repeats are the conservative final result. The separate
`20261024-overlap-precision` functional run overlapped offline compilation and is
excluded from this timing table.

Main Forward and DepthPass each drew **2,336,622 primitives** before and after.
Shadow counts changed from 5,822,003 to 5,895,731 in the initial before/after
captures; these are recorded separately in the JSON and are not evidence of a
geometry LOD reduction. Every run restored camera, entity visibility, bindings,
and saved level contents. Precision tests also verified restoration of the
inspector setting to 16. Saved level SHA256:
`70f2db2744cd2d2fa2c5808c157ca463913756744d1bae0025108b859cfc7a5d`.

After masking only the variable statistics HUD, the initial before/after images
matched **99.998432%** of 701,718 pixels.
The final comparison matched **99.998432%**.
The initial comparison differed at nine terrain pixels by one 8-bit channel step
and at a two-pixel white editor marker. The camera, silhouettes, material look,
and visible blending remain consistent. Exact masks and error metrics are in the
JSON. Images remain alongside raw native captures under `user/TerrainSceneProfile`.

## Validation and reproduction

`TGProject.Editor` built successfully. The final PC asset cook processed 40 assets
with zero failed jobs and zero errors. It reported 154 warnings, including existing
parent-material JSON Color defaults and SDK preprocessor/path diagnostics; this is
not a warning-free SDK build. The Python regression suite passed 43 tests and 20 subtests; the sync/precision
suite passed 53 tests. Both precision modes ran in the editor, with every
published source checked for its selected height format and shader property.
The production blend arithmetic probe reports 114 half arithmetic instructions
and zero float arithmetic instructions in its FP16 kernel, and the inverse in its
FP32 kernel. World addressing and other intentionally wider operations are outside
that isolated weight probe.

Run the native tools from the terrain-compositor repository:

```powershell
python tests/tg/VerifyTerrainOverlapSpecialization.py --project D:/diversion/TG/TGProject --engine D:/O3DE-SDK-TG --output D:/brooteskan/work/overlap-specialization
python tests/tg/VerifyNativeTerrainPrecision.py --engine D:/O3DE-SDK-TG --output D:/brooteskan/work/overlap-native-precision
```

Use `tools/ProfileTerrainScene.py` with `TC_SCENE_PROFILE_CONFIG` pointing to a copy
of `tools/ProfileTerrainOverlap.example.json` with a fresh output directory.
`height_precision` cases change the inspector transiently, wait for the matching
publication revision, verify texture formats, and restore the original setting
without saving. All production changes live in terrain-compositor's canonical TG
integration and are applied to TG through `integrations/tg/SyncProject.py`.

Machine-readable evidence: [TerrainOverlapOptimization20261024.json](TerrainOverlapOptimization20261024.json).
