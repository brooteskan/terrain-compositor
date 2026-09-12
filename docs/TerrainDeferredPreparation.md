# Deferred preparation and request-local sample reuse (TG #38)

The renderer exposes independent `r_terrainDeferredPreparation` and
`r_terrainSampleReuse` switches. Both require the existing retained-only proof;
`r_terrainRetainedOnlyQueries false` retains the ordinary synchronous path.

## Owned area decision and acceptance

The area query executes on the renderer control thread after dependency capture.
The request owns its bounds and Boolean result, including false. Its worker never
calls the area service. This value, rather than a handler pointer or a promise
that a ticket keeps a handler alive, supplies the area decision across frames.
Before deferred acceptance, the control thread queries the same bounds again and
rejects a changed result. This also detects changes before deferred terrain
notifications. Settings and runtime switches are compared with their capture.
The existing publication-then-dependency locks subsequently validate the source
tickets and destination serials and remain held through the complete resource
commit. No terrain query executes under those locks.

Every prospective deferred request is assessed before dispatch, including both
grids and all halos, even for an empty area. Only retained-only requests with a
complete across-frame capability enter the dispatcher. Unsupported providers,
masks, query modes and unbounded retained data use synchronous recovery.

For deferred admission, a pointwise first owner covering the whole rectangular
grid can be certified from the exact corner extrema plus the full grid's count,
sampler and channel contracts. Positive-spacing float coordinate construction is
monotone. Split ownership and undeclared or restricted contracts retain the full
position walk. Differential tests compare both assessments. The synchronous
comparison path retains its original assessment cost.

## Dispatcher and camera movement

`TerrainSectorDispatcher` persists with the manager. Its reservations bound queued,
running and completed-but-uncommitted work together. Workers mutate only their
owned payload. Completion is published under a mutex; the control thread takes
only complete batches. Cancelled work retains its reservation until running jobs
acknowledge cancellation. Reset/shutdown cancels, waits and reclaims storage.

Terrain uses at most two engine jobs concurrently and leaves at least two engine
worker slots available for other jobs. On smaller job pools it retains synchronous
execution. Coarse requests precede finer requests inside each group. Required
coverage has priority over speculative work; every fourth dispatch serves the
oldest eligible group. A reversal cancels the obsolete speculative group. A
superseded member cancels its entire group, and still-required destinations are
reissued together rather than publishing an incomplete subset.

Camera motion predicts up to half a second ahead, capped at half the finest sector
width. The first upcoming boundary group is captured. Speculative capture reserves
a new serial without moving requested or committed placement. At the matching
boundary, requested placement adopts that serial. A completed speculative group
stays reserved until the boundary requires it. Committed raster/RT bundles and
authoritative empty coverage remain at their original coordinates until the
whole replacement group validates and commits.

## Limits and explicit recovery

| Resource or debt | Admission limit |
| --- | ---: |
| Outstanding groups / retained publication generations | 3 |
| Queued, running and completed sector requests | 96 |
| Reservation ledger | 256 MiB |
| Peak scratch/result allowance per sector | 2 MiB |
| Publication/source allowance per group | 32 MiB |
| Accepted deep publication estimate | 16 MiB |
| Queries per publication | 32 |
| Output grid width for deferred requests | 129 vertices |
| Concurrent terrain workers | min(2, engine workers − 2) |
| Required replacement age before recovery | 500 ms |
| Unsupported speculative retry interval | 250 ms |

Deep retained-data accounting includes image samples and reconstruction, mesh
height fields, cutout vertices/indices/BVH, collision cell masks, surface data,
strings and vector capacities. Shared data is conservatively counted again.
Unknown byte declarations do not grant admission. The remaining allowance covers
source snapshots, callable/control-block allocations, worker planning and gather
scratch, packed/RT output and small allocator/job overhead. Reservations are not
process working set or GPU/driver memory; each sector's existing GPU bundle is
accounted separately. Diagnostic records expose reserved/peak bytes and result
and reuse scratch bytes.

Startup and large teleports can exceed one atomic group's deferred capacity. They
use the existing synchronous path. Capacity exhaustion also uses explicit
synchronous recovery without evicting valid committed bundles for queue space.
A required group older than 500 ms cancels outstanding work and requests a full
refresh. Edits/publication invalidation retain the conservative existing full
refresh. Recovery can exceed the soft control-thread target; it is recorded as
such, not hidden in warmed-route averages. No upload staging or smaller atomic
publication unit is introduced.

## Exact raw sample reuse

Only the built-in retained composition adapter opts into pointwise/subset
equivalence. The original regular and CLOD samplers, both output channels and the
same immutable source set/publication must agree. Unsupported subset contracts
retain the original gather. The request-local helper stores raw final render
height and existence from the regular gather. For each CLOD position it looks up
the regular axes, then requires bit-identical float coordinates constructed by
the original layout operations. Nearby positions never match. Unmatched points,
including the wider CLOD normal halo, are evaluated normally.

The regular gather samples directly into its caller's raw output and retains one
copy. The CLOD gather resolves each distinct row/column axis value once, copies
exact matches directly, and scatters only evaluated misses. It avoids a full
regular-grid identity map, duplicate output/scatter buffers and repeated axis
searches per vertex. At the ordinary grid sizes its measured scratch accounting
is 360,381 bytes per request, including the retained raw values.

Clamping, packing, height/existence sentinels, normal construction, CLOD
interpolation, RT decoding and bounds remain downstream of the raw values.
Collision data never participates in this cache. At the ordinary 129/65 grid
sizes and exactly overlapping coordinates, 4,225 of 21,650 requested samples are
reused; 17,425 source evaluations remain. Fractional coordinate rounding can
reduce the overlap and is tested rather than assumed equivalent.

The independent `r_terrainRetainedKernel` switch captures equivalent zero-profile
pruning in the built-in procedural snapshot. A hill whose computed smooth profile
is exactly zero has zero weight for every supported positive exponent. Skipping
its power/sign-hash/union work leaves the original floating-point distance and
profile construction intact. Live/collision source evaluation remains the
unoptimized reference. A separate bitwise test compares 294,912 positions across
densities and exponents. This targets the retained evaluation cost observed after
sample reuse; it does not change the number of position evaluations.

Optional `TerrainSampleReuse` and `TerrainDispatch` records report requested,
evaluated and reused samples, retained evaluation time, preparation time,
scratch/result bytes, reservations, completion age, commit costs and recovery.
Worker CPU sums overlap and are not frame times.

## Validation record

The local profile build passed 388 runtime tests, 46 editor tests, 142,560 hardware
boundary classifications and the maintained override/profiling checks. The fully
optimized setting completed three warmed 32 m/s flight passes without a frame
above 50 ms; its worst interval was 19.54 ms. Reuse independently reduced measured
worker CPU by 13.8% and position evaluations by 19.5%. Raw local validation
artifacts remain under TG's `build/issue38` and `user/TerrainFlightProfiling`.
The 4 ms control-thread target and manual user-flight acceptance remain open;
the queue limits do not establish a hard upload time bound.
