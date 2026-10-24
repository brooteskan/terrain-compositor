# Material clipmap editor measurements, 2026-10-24

DX12 editor, AMD Radeon(TM) Graphics, VueLiveLink, 1280 x 720, uncapped rendering. Four 360-frame camera sweeps ran in full/strips/strips/full order. The first eight samples after each screenshot were excluded; retained native render-update IDs were consecutive. The camera moved 0.25 m in X and 0.125 m in Y per frame. Indices 252-262 cover the aligned clipmap boundary at index 256.

| Measurement | Full layers | Budgeted strips |
|---|---:|---:|
| Median frame | 33.00 ms | 33.49 ms |
| 95th percentile | 35.87 ms | 36.17 ms |
| 99th percentile | 39.63 ms | 37.48 ms |
| Worst frame | 42.28 ms | 41.14 ms |
| Worst boundary envelope frame | 39.65 ms | 36.60 ms |
| Scheduled texels, two retained runs | 38,273,024 | 1,410,560 |
| Maximum moving update | 1,310,720 | 13,824 |
| Aggregate fresh cache GPU queries | 199.66 ms / 134 samples | 394.99 ms / 286 samples |

Update area decreased 96.3%. Boundary pacing and the 99th percentile improved, while median and 95th-percentile frame times increased slightly. More small raster submissions increased aggregate cache GPU time about 1.98 times. Existing terrain draw packets leave vertex/pass costs on each update; this is consistent with that aggregate cost. This is not an overall GPU speedup or a guarantee against hitches. Delayed GPU queries support aggregate comparison only and cannot be assigned to an exact boundary frame. The default budget is 32,768 shaded texels and at most two passes per update, not a hard GPU time limit.

The stationary full/incremental ground crops were identical. Moving pending/direct and teleport pending/direct mean RGB channel errors were 0.02141 and 0.00932 on a 0-255 scale. A deliberately small 1,024-texel teleport budget passed. Coverage captures confirmed cached shading was used. These are sampled-view checks rather than a proof for every graph or camera.

All 19 saved acceptance scenes remained unchanged. The generated TerrainMeshManager implementation retained its pinned hash, and geometry LOD/CLOD code was unchanged. Editor and MaterialCanvas native builds, shader/asset processing, live editor checks, graph contract tests and the native scheduler tests passed. Initial shader processing reported eight graph-vector/SDK derivative warnings, with zero failures/errors; the final up-to-date sweep reported zero failures/errors/warnings. Standalone scheduler tests cover 900 randomized moving frames and stale ownership; the earlier exact strip planner covered 441 scrolling cases.

Machine-readable summaries are in [GraphMaterialClipmapMeasurements.json](GraphMaterialClipmapMeasurements.json). Portable reproduction scripts and camera settings are in `tests/editor`.
