# Editor camera-flight stutter (issue #37)

## Baseline and reproduction

The retained-query/CLOD worker optimization was committed in
`d775c10e9fa6e30a7b7c735c0ab36a33151cd7ad`. The completed mesh-height-gap
acceptance arc used for this investigation is
`7c9ad298aaa462dd0ebd44c81e887634c3543466`. The external O3DE checkout is
`061180bf24f1666eb30315b35da292eb14f4659c`.

Measurements on 2026-09-08 used a Windows profile build, a Ryzen 7 2700
(8 cores / 16 logical processors), and an AMD Radeon RX 7900 XTX with DX12.
The observed viewport was 1654 x 394, MSAA 2x. DefaultLevel has a 0.5 m
height-query resolution and a 256 m first-LOD distance. Its resulting sector
grid contains 10 x 10 sectors per LOD, with 128 x 128 quads per sector;
the finest sectors span 64 m.

`Gem/Tests/profile_terrain_flight.py` opens DefaultLevel without saving it,
warms up the translation route, and records separate stationary, rotation,
and translation passes. Translation starts at (16, 16, 32) and travels in
+X at 32 m/s for 16 seconds (512 m). Rotation stays at the start and turns
30 degrees/s for 12 seconds. Each phase resets the camera and settles before
recording. The six-second stationary phase supplies the idle comparison.
The default run repeats all three phases twice. Detailed CPU captures and
GPU pass samples run separately from the uninstrumented measurements.

The frame samples are wall-clock intervals around one editor TickBus step,
including the camera setter and Qt event processing. These are editor frame
intervals, not presentation-to-display latency. The full baseline trace has
823 terrain render invocations for 824 recorded steps, supporting the
one-render-per-step interpretation in that capture. Vsync/pacing holds the
normal interval near 16.67 ms. Do not interpret these numbers as uncapped
render throughput.

The initial `baseline` experiment lost foreground focus and paused; discard
it as a baseline. Subsequent runs use `--autotest_mode` and
`ed_backgroundUpdatePeriod -1` to prevent that pause. Avoid builds, screenshots,
asset processing, and manual camera input during the measured passes.

## Findings before the fix

The valid `baseline_v2` runs recorded:

| Phase | Frames | p95 (ms) | p99 (ms) | Worst (ms) |
| --- | ---: | ---: | ---: | ---: |
| Stationary 1 | 361 | 17.56 | 18.56 | 20.27 |
| Rotation 1 | 720 | 16.99 | 17.21 | 17.54 |
| Translation 1 | 926 | 17.04 | 19.08 | 451.06 |
| Stationary 2 | 361 | 16.94 | 17.51 | 19.33 |
| Rotation 2 | 720 | 16.94 | 17.46 | 18.12 |
| Translation 2 | 923 | 17.40 | 18.22 | 445.71 |

Both translation passes have seven frames exceeding 50 ms over 16 seconds
(about 0.44 hitches/second). They occur at x = 128, 192, 256, 320, 384,
448, and 512 m, within one camera step. The worst frame is consistently
at x = 320 m. Percentiles alone hide these sparse but severe pauses.

The separate full-route CPU trace contains sector batches of 10, 10, 20,
100, 20, 10, and 30. The 100-sector batch takes 442.73 ms: 429.27 ms
preparing/waiting for workers, 13.46 ms uploading, and 0.001 ms waiting
for the publication lock. No composition publication changes occur during
these measured passes. Across the seven updates, worker preparation is
dominated by ordinary terrain queries (2,827 ms aggregate worker time) and
retained queries (2,641 ms). CLOD interpolation is 27 ms and ray-tracing
decoding 49 ms aggregate. Worker sums overlap across threads and must not
be added to frame durations.

Two mechanisms explain the recurring slow frames:

1. **Incorrect wrapping at signed-coordinate zero.** `CheckLodGridsForUpdate`
   previously evaluated a signed coordinate modulo the unsigned sector count.
   For a ten-sector grid, -1 becomes unsigned 4,294,967,295 and maps to slot 5
   instead of slot 9. Moving the grid start from -1 to 0 changes the mapping
   of every overlapping sector. At x=320, the finest grid unnecessarily
   relocates all 100 sectors instead of just the ten in the entering column.
2. **Synchronous boundary work remains expensive.** Even a correct ten-sector
   batch must finish all ordinary and retained queries before the render
   thread proceeds. Larger coincident LOD crossings multiply that work.
   Query batching reduced one part of this cost; it did not remove the
   duplicate height evaluation or spread sector completion across frames.

Disabling CLOD before the index fix reduced the uninstrumented worst
translation frame to 319.24 ms but retained the 100-sector batch. The
diagnostic trace confirms 200 regular queries and no CLOD queries, versus
400 regular/CLOD calls with CLOD enabled. That trace also encountered an
upstream profiler fixed-string assertion causing a 3.23-second pause;
exclude its whole-frame distribution from performance comparisons.

## Implemented correction and safety

The wrap calculation now uses signed 64-bit arithmetic until the result is
nonnegative, then converts to a buffer index. Both X and Y use the correction.
`CollectUpdatedSectors` isolates the existing relocation step so regression
tests exercise the actual grid without creating GPU resources. They cover
X, Y, and diagonal zero crossings in both directions, unique complete grid
coverage, stable buffer slots for overlapping world coordinates, and no
updates for a stationary camera.

Terrain heights, hole classification, CLOD interpolation, render/collision
independence, immutable snapshots, and the generation check before uploads
are unchanged. The authored prefab is hashed before and after each run and
never saved by the harness. The correction only eliminates unnecessary
sector relocation; it does not defer publication or upload stale results.

Profile builds passed for `Terrain`, `Terrain.Editor`, `TGProject`,
`TGProject.Editor`, and `TGProject.Tests`. The focused suite passed 59 tests
with exit code 0, including the three grid-crossing regressions and 142,560
hardware D3D11 hole-boundary classifications with zero mismatches. Existing
MSBuild shared-intermediate-directory warnings remain. The known legacy
reflection/shutdown exclusions documented in TerrainMeshHeightGaps.md still
apply; this is not a claim that the full project suite passes.

## After-fix measurements

The first two `fixed` uninstrumented translation runs measured 150.93 ms
and 145.46 ms worst frames, versus 451.06 ms and 445.71 ms before the fix.
The first corrected run had p95 17.11 ms, p99 17.90 ms and seven >50 ms
frames. Its x=320 frame dropped to 59.20 ms. The second corrected run had
p95 17.64 ms, p99 75.08 ms and sixteen >50 ms frames, including several
additional pauses away from sector boundaries. Those samples are retained;
the investigation does not establish the cause of those transient pauses.
Stationary and rotation phases stayed below 20 ms in both corrected runs.

A third uninstrumented confirmation (`fixed_confirm`), back on the original
0.5 m settings with CLOD and ray tracing enabled, measured p95 17.36 ms,
p99 18.36 ms and a 132.19 ms worst frame. The three corrected route maxima
are therefore 132-151 ms, compared with 446-451 ms before the correction.
This confirms the large-spike reduction without claiming that overall hitch
frequency or every percentile improved in every run.

The corrected full CPU trace contains 110 sector workers instead of 200,
with batches of 10, 10, 20, 10, 20, 10, and 30. At x=320, the update falls
from 442.73 ms to 55.96 ms. Aggregate sector-update time falls from
939.29 ms to 518.38 ms in the diagnostic traces. The unprofiled timing-log
pass gives 46-61 ms for ten-sector batches, 89-90 ms for twenty-sector
batches, and 121 ms for the thirty-sector batch. This fixes the largest
redundant rebuild, but does not eliminate recurring boundary stalls.

Four delayed GPU samples while moving show Forward at 0.880-0.943 ms,
DepthPass at 0.598-0.708 ms, and the ray-tracing acceleration-structure
pass around 0.254-0.283 ms. Parent/root timestamps are zero, so these
are individual pass measurements, not total GPU frame time. They support
prioritizing CPU work but cannot exclude all GPU/driver stalls.

The `fixed_rt_off` control has no `PrepareSectorRayTracingData` calls in
its CPU trace, confirming that the startup switch took effect. Its timing
pass still takes 54-59 ms for ten-sector batches and 138 ms for thirty
sectors, although upload time drops to 0.2-0.6 ms. Disabling terrain ray
tracing therefore does not resolve the worker-query stalls. Its uninstrumented
worst frame was 186.67 ms with p99 55.40 ms, further demonstrating that
one-off whole-frame results include variability beyond the isolated sector
costs.

The `fixed_resolution_1m` control changes only the live level's height-query
resolution from 0.5 m to 1 m. Its translation pass has p95 17.14 ms,
p99 18.18 ms, worst 44.20 ms, and no frames over 50 ms. A separate
timing-log pass has worst 46.09 ms. The first LOD distance and sector world
width stay the same; each sector has fewer sample vertices. This is strong
evidence of query-volume cost, but it sacrifices geometric/collision sampling
detail and is not applied to the authored level.

The `fixed_procedural` control opens a disposable prefab copy under `user`,
removing the three image stamps, mesh-height stamp, closed-mesh cutout,
and three authored mesh components while retaining the procedural source,
terrain region/composition, renderer, and lighting. It has p95 17.44 ms,
p99 19.40 ms, worst 139.30 ms. Its timing pass still spends 42-51 ms on
ten-sector batches and 125 ms on thirty sectors. Authored mesh/stamp assets
are therefore not required to reproduce the remaining boundary stalls.

Machine-readable phase distributions, thresholds and configurations are in
[TerrainCameraFlightMeasurements.json](TerrainCameraFlightMeasurements.json).
The raw CPU/GPU captures, frame samples and corresponding editor logs remain
under `user/TerrainFlightProfiling/`. All successful runs verified the
DefaultLevel prefab SHA-256 remained
`e08a174569660f680d0da1609c2fa826f55d01ed5dcd30f044ac47043939618f`.

## Prioritized follow-up and targets

After correcting the grid wrapping, the remaining target is to keep sector
work on the render thread below 4 ms per frame and eliminate route frames
over 50 ms attributable to terrain updates. Use at least three repeated
512 m routes, reporting both >50 ms hitches/second and the worst frame;
p99 alone is insufficient at the observed boundary frequency.

1. **Avoid duplicate queries for proven retained ownership.** Ordinary and
   retained queries each account for about half the sampled worker CPU cost.
   Target at least 35% less worker preparation time on matched 10/20/30-sector
   batches. Skip the ordinary query only when the retained immutable owner
   supplies both height and image-hole existence over the complete batch;
   preserve scalar providers, unowned samples, independently owned channels,
   and overlapping-region fallback. This is a moderate implementation change
   with meaningful semantic regression risk, so retain all ownership and
   hole-parity tests. It cannot by itself guarantee a hitch-free frame budget.
2. **Budget sector preparation and publication across frames.** Remove the
   unconditional render-thread wait for every entering sector. Target no more
   than 4 ms of main-thread sector publication/upload per frame and zero
   terrain-driven >50 ms hitches on the reference route. This requires
   bounded in-flight memory, cancellation on scene/configuration changes,
   stable sector ownership while work is pending, and the existing generation
   check before any upload. Retain old/coarser coverage until replacement data
   is ready; quantify temporary LOD latency and memory before adopting it.
3. **Tune quality only as an explicit tradeoff.** CLOD removal reduces query
   count but introduces LOD transitions; coarser query resolution changes
   geometry detail and collision sampling. Use the controlled comparisons to
   estimate headroom, not as silent changes to the authored level. Ray-tracing
   decoding and upload work are lower priority than query preparation in the
   captured batches.

## Reusing the harness

Enable the project's Profiler gem and build `Profiler`, `ProfilerImGui`, and
the project/terrain profile targets. From the repository root:

```powershell
& .\TGProject\build\windows\bin\profile\Editor.exe `
  --project-path D:\TG\TGProject --autotest_mode `
  --runpython D:\TG\TGProject\Gem\Tests\profile_terrain_flight.py
```

Set `TG_TERRAIN_FLIGHT_CONFIG` to an absolute JSON filename to override the
configuration. Use a fresh `label` per run. Results are written beneath
`TGProject/user/TerrainFlightProfiling/<label>/`. Example CLOD comparison:

```json
{
  "label": "clod_off",
  "repeats": 1,
  "trace_seconds": 16.0,
  "properties": [{
    "entity": "Level",
    "component": "Terrain World Renderer",
    "path": "Configuration|Mesh configuration|Continuous LOD (CLOD)",
    "value": false
  }]
}
```

Property overrides apply only in memory and are discarded on editor exit.
`r_terrainEnableRayTracing` defaults to true and is sampled when the terrain
scene initializes. Pass `--r_terrainEnableRayTracing=false` at editor launch
for the terrain-only ray-tracing comparison; restart normally to restore it.
This deliberately removes terrain from ray-traced effects during the test.

Run `Gem/Tests/analyze_terrain_flight.py <output-directory>` with Python to
write `summary.json` with nearest-rank p50/p95/p99, worst-frame positions,
hitch counts/rates, scope totals, and individual sector updates. Keep the
associated Editor.log alongside the outputs. Raw traces are large and remain
under the ignored user directory; the checked-in report preserves findings.

The stock CPU profiler has malformed dynamic scope names in some records.
The analyzer replaces invalid UTF-8, counts replacements, and groups known
literal terrain scopes by exact name. Its aggregate scope totals are
inclusive. CPU profiling itself adds allocation/serialization overhead and
can assert in unrelated long/dynamic scope names, so use uninstrumented
passes for before/after distributions. GPU timestamp requests return
delayed pass samples, not a synchronized GPU trace of each CPU hitch.

The harness restores query batching to true and sector timing to false;
profiling is inactive outside diagnostic capture. Manual user flight testing
remains necessary to assess the perceived improvement at the user's usual
viewport size and route.

Issue #37 should remain open for that flight test and the remaining recurring
stalls. This investigation establishes the baseline, a measured cause and
correction for the largest spike, and prioritized follow-up; it does not
claim a hitch-free editor.
