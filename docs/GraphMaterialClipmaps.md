# Graph material clipmaps

The graph renderer caches material channels in five camera-centered, world-aligned layers. Generated graphs provide a fourth shader, `<graph>_Clipmap.shader`, which evaluates the same graph function and material parameters as the forward shader. The cache runs before forward shading; lighting, atmosphere, cutouts, height gaps, depth and shadows remain on their existing paths.

## Geometry and appearance

The cache rasterizes the existing terrain draw packets using the existing `CalculateTerrainPosition` function. It does not replace terrain mesh scheduling, coverage publication, geometry LOD, CLOD, or the vertex layout. The mesh manager only exposes its existing committed-resource revision for cache invalidation.

Final shading selects a layer that covers the point and resolves the pixel's world footprint. It rejects missing texels and mismatched height/geometric-normal context, including changes caused by CLOD. Finer detail and invalid coverage use the original graph evaluator. Boundaries blend back to that evaluator. Cached shading normals follow the live geometric normal, and a quad-wide fallback keeps graph texture derivatives defined.

The cache stores base color/roughness, shading normal/metalness, specular/AO/validity/world height, and geometric normal. Five 512 × 512 layers use approximately 50 MiB. Texel sizes are 0.125, 0.5, 2, 8 and 32 metres; layer widths are 64, 256, 1,024, 4,096 and 16,384 metres.

## Reuse and invalidation

`terrain.clipmapTemporalReuse` defaults to false for generated graphs. With the default incremental mode, these time-dependent or camera-dependent graphs use the direct evaluator, so budgeted updates cannot mix different animation frames. VueSimpleTerrain enables reuse because its material graph uses static world-space inputs; lighting still runs every frame.

Each layer is a toroidal texture. When its origin crosses the existing 16-texel grid, the scheduler retains the overlapping pixels and queues only the new X/Y strips, without double-counting their corner. Jobs split at texture seams and at the shared per-frame texel budget. Round-robin scheduling and a pass-count budget limit both shaded area and pass submissions. The defaults allow 32,768 texels and at most two cache raster passes per frame across all five layers.

Exact per-world-tile ownership tags are checked at all four bilinear corners. Pending, cleared or recycled pixels fall back to direct graph evaluation. Material/terrain changes invalidate ownership immediately; teleports queue full new windows. Rotation queues a coverage refill. These full requests are also spread across frames. Tags use exact integer values in the existing 32-bit factor attachment; the cache is recreated before tag precision can be exhausted.

Mesh publications during camera travel do not continuously refill whole layers. Existing height/normal validation rejects cached context that no longer matches live CLOD. Once the camera stays still for eight render updates, a pending geometry revision requests a budgeted coherence refill. This changes cache scheduling only; it does not change geometry LOD or when terrain meshes are published.

CPU scheduling runs in the normal terrain render update. GPU cache passes run on the graphics queue, ordered before forward terrain shading by the frame graph. There is no dedicated cache worker or asynchronous-compute queue. The texel/pass budget bounds submitted work, not milliseconds: vertex processing, graph complexity, editor activity and GPU contention can still produce frame-time spikes. Pending pixels can temporarily increase direct shading cost.

## Editor controls

- `r_terrainGraphClipmapIncremental true` enables toroidal strip updates and budgeting (the default). `false` retains the previous unbudgeted full-layer reference path for comparison.
- `r_terrainGraphClipmapBudgetTexels 32768` sets the shared texel budget; `r_terrainGraphClipmapBudgetPasses 2` caps cache passes (one to five).
- `r_terrainGraphClipmaps true` enables caching (the default); `false` selects direct evaluation for comparison.
- `r_terrainGraphClipmapDebug true` shows cache coverage: green uses cached channels, red uses direct evaluation. Lighting still affects this display.
- `r_terrainGraphClipmapDiagnostics true` publishes `r_terrainGraphClipmapReady`, `Updates`, `Texels`, `PendingTexels`, `GpuNs`, `GpuSamples`, `FrameUs` and `FrameId` (each with the `r_terrainGraphClipmap` prefix). Ready means cache resources are usable, not that pending work is empty. FrameUs uses the RHI CPU frame-to-frame interval. GPU samples are delayed query results; zero samples mean unavailable timing, and their durations must not be assigned to the current boundary frame. It is disabled by default to avoid diagnostic command traffic.

Materials without a generated cache shader continue to render directly. DX12 editor rendering is tested. Vulkan shader compilation uses Vulkan 1.1 subgroup support for quad-wide fallback; Vulkan runtime behavior is not certified by the DX12 tests. Metal retains the existing disabled-backend setting.

## Verification

Run `tests/editor/verify_graph_material_clipmaps_editor.py` in a dedicated Editor with `--runpython`. Set `TG_MATERIAL_CLIPMAP_REPORT_ROOT` to select an output directory. The test changes material selections only in memory, restores temporary material/shader edits byte-for-byte, and checks that the saved acceptance level is unchanged. The game-mode camera-restoration prompt may require acknowledging in the editor.

The test captures direct/cached views of default, procedural and textured graphs at near/far cameras, coverage, dynamic refresh, static reuse, small translations, layer scrolling, rapid switching, clearing selection, material and shader reloads, and entering/exiting game mode. The pre-existing missing FlyCameraInput viewport icon is recorded separately from renderer errors.

Pixel comparisons and cache-update counts establish rendering behavior and reuse. They do not establish a whole-scene GPU speedup. Zero GPU timestamp captures must be treated as unavailable timing, not as free rendering.

`tests/editor/verify_incremental_material_clipmaps_editor.py` checks startup and moving-camera budgets, toroidal rendering, and a teleport with a deliberately small 1,024-texel budget. It captures full/incremental/direct views and verifies the saved level hash. Use `TG_INCREMENTAL_CLIPMAP_REPORT_ROOT` for its output.

`tests/editor/benchmark_material_clipmap_updates_editor.py` runs full/strips/strips/full camera sweeps in VueLiveLink at 1280 × 720 with the same camera path, uncapped rendering and diagnostics. Use `TG_CLIPMAP_BENCHMARK_REPORT_ROOT` for its output. It records native frame intervals, frame IDs, update area, pending work and fresh GPU queries. Exclude the first eight frames after each screenshot from timing analysis; compare boundary envelopes as well as whole-run percentiles. Delayed GPU query durations support aggregate cache-cost comparisons only.

The standalone CMake target in `tests/clipmaps` exercises negative coordinates, wrapped seams, exact strip area, bounded partial jobs, invalidation, teleports, retained pending work and stale ownership across 900 randomized moving frames. It includes the production scheduler header.

TG consumer delivery and its normal-storage changes are maintained in [integrations/tg](../integrations/tg/README.md). The published [editor measurement record](GraphMaterialClipmapMeasurements.md) includes the measured frame-time tradeoff.
