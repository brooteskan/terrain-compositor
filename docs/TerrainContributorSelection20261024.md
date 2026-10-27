# Terrain6/7 contributor selection and discard controls — 2026-10-24

Fixed camera from `20261024-overlap-queries-final`; 1203×656, MSAA 2×, FP16, both terrains visible. Sky atmosphere, HDRI sky background, two cloud volumes and aerial atmosphere disabled; sun/IBL lighting preserved. Hardware: user-reported Ryzen 7 5700G integrated Radeon GPU and 32 GiB system RAM. These editor results exclude atmosphere/cloud cost and do not establish a game-build frame rate.

## Measured GPU frame time

Each row is the median of 240 frame measurements. Repeated captures are listed separately. Enabled-pipeline timestamp samples independently agree with the frame envelope.

| Run | Case | GPU ms | CPU ms | Process GPU allocation MiB |
|---|---|---:|---:|---:|
| controls | complete | 42.330 | 42.918 | 1707.84 |
| controls | discard-after | 41.688 | 42.202 | 1707.84 |
| controls | discard-before | 30.992 | 31.598 | 1830.84 |
| controls | complete-repeat | 42.444 | 43.024 | 1830.84 |
| controls | discard-before-repeat | 30.829 | 31.156 | 1830.84 |
| controls | discard-after-repeat | 41.653 | 42.148 | 1830.84 |
| controls | complete-final | 42.309 | 42.808 | 1830.84 |
| selection | complete | 42.584 | 43.091 | 1705.84 |
| selection | selection | 30.558 | 30.871 | 1828.84 |
| selection | complete-repeat | 42.450 | 43.068 | 1828.84 |
| selection | selection-repeat | 30.623 | 30.945 | 1828.84 |
| selection | coverage | 30.678 | 31.068 | 1828.84 |
| lookup_control | complete | 41.712 | 42.512 | 1709.84 |
| lookup_control | lookup-only | 30.026 | 30.532 | 1832.84 |
| lookup_control | selection | 30.076 | 30.571 | 1832.84 |
| lookup_control | complete-repeat | 41.540 | 42.157 | 1832.84 |
| lookup_control | lookup-only-repeat | 30.011 | 30.438 | 1832.84 |
| lookup_control | selection-repeat | 29.934 | 30.374 | 1832.84 |
| restored | complete | 41.494 | 42.106 | 1709.84 |
| restored | complete-repeat | 43.032 | 43.515 | 1709.84 |

## Main finding: rejection is not yet the measured cause

The additional lookup-only control performs the same tile lookup after full original shading, retaining a data-dependent emissive branch whose condition is false for the published material. It rejects no terrain work and preserves the captured image. It nevertheless runs at 30.026 and 30.011 ms, compared with 41.712 and 41.540 ms for complete, while selection in the same run is about 30.1 ms. Thus this prototype does NOT demonstrate an incremental speed gain from contributor rejection. The initial apparent 28% selection gain is confounded by the table-using shader path. Shader compiler/resource behavior or GPU operating-state effects remain hypotheses; register/scratch/occupancy attribution is not established. The repeat of complete after table allocation remains slow, so a one-time memory warm-up alone does not explain the difference.

## What the controls mean

Late discard executes the original height/footprint queries and weight resolver, then discards every pixel with two positive retained contributors before overlap material sampling. Early discard uses a conservative tile proof to discard guaranteed-blend tiles before those queries; uncertain tiles still use the exact original resolver and late discard. It is a practical partial early classifier, not an ideal zero-cost classifier for all blend pixels. Both leave earlier geometry-normal smoothing intact.

Both discard controls remove exactly the same 1,976 pixels in the captured comparison area. They are intentionally incomplete renders, so their timings are diagnostic rather than an appearance-preserving optimization.

Selection mode returns the original ordinary-material path immediately for tiles proven to have no blend; uncertain and blend tiles retain the original overlap shader. Both selection captures match the complete render exactly over 701,014 RGB pixels, after masking fixed timing HUD and editor gizmo regions. The instrumented complete image also matches the pre-experiment original. This verifies this view, not every possible camera or publication.

## Bounds, memory and fallback

The 128×128 classifier stores two bitsets (4,096 logical bytes), using no texture resource or material sampling. Bounds cover transformed UV rectangles, bilinear extrema, footprint edges, height cutoff and conservative FP16/interpolation margins. It classifies 14,671 endpoint tiles, 119 guaranteed-blend tiles, and leaves 1,594 uncertain. A 300,000-point comparison against a CPU FP16 selector model found no classification errors; three focused tests cover transformed/negative-height-scale bounds, uncertain seams/tiny contributions and proven cases.

The process GPU allocation rises by 123 MiB when a table-using shader mode first executes, and stays elevated when returning to complete mode. This is not a 4 KiB total-memory implementation. The allocation is not yet attributed to driver/compiler scratch, shader residency or another resource. On this UMA GPU it uses shared system memory. Production work must resolve it.

Modes default to zero and original publications have key zero. The experiment enables the frozen table only for the captured key, FP16/two sources/fixed extent and matching overlap width/ground cutoff parameters. The benchmark checks height dependency hashes before loading. Automatic live invalidation for transformed/edited/streamed sources is still required for production; this is a frozen-scene prototype.

## Restoration and limitations

All pre-existing canonical and consumer shader, material-layout and generator bytes, plus the sync manifest, are restored from verified backups; the previous page-cache work is preserved. The saved VueLiveLink prefab hash remains FAEC5FD3F9B69643B918AF3A917623776D31BF7CC962485CFBA712313A36A922. Successful run records verify camera, visibility, material binding, height precision and level restoration. Effect restoration is recorded separately. Temporary diagnostic material sources and the generated active include are removed. Prototype snapshots remain under `tools/ContributorSelectionPrototype`.

An initial asset-hot-reload trial failed during material layout replacement. A second trial used unsupported reflected integer overrides (readback succeeded, application emitted warnings); it rendered identical images and is excluded. Only explicit compiled-material runs are reported. The known startup image-input-index assertion also occurs in baseline launches and remains unresolved; this is not a clean full-suite validation.

## Next architectural work toward comfortably above 30 fps

Use 25 ms as a provisional 40 fps frame budget. The table-using paths render at about 30–31 ms with effects disabled, but the reason is not isolated and the 123 MiB cost is unresolved. This is not yet a production improvement to bank toward the budget; atmosphere/cloud cost must also be included later. Do not count discard or color-marker modes as usable rendering gains.

Prioritize compiled-code/resource analysis: compare the complete, lookup-only and selection paths, including register allocation, scratch/private memory, occupancy and GPU operating clocks if available. Reproduce the faster behavior without dummy lookup or the unexplained allocation before promoting a production change. Keep draw/cluster separation of ordinary and seam terrain as an architectural candidate, preserving tiny valid blends and exact fallback, but do not claim that this test proves its gain. A production classifier also needs automatic conservative invalidation.

The selected forward-pass sample is about 9.6 ms versus 21.2 ms complete. Main depth is about 1.8 ms; four shadow cascade samples total roughly 5.7 ms. Terrain6/7 submit 2,426,734 primitives in both the main depth and forward passes. Audit LOD, visibility and shadow participation before another material rewrite. The mesh normal reconstruction is already one filtered field read; UV/gradient sharing and this reconstruction are existing improvements, not new candidates. Ground stochastic sampling uses three color and up to three normal reads per participating surface; change that only with separate timings and appearance review.
