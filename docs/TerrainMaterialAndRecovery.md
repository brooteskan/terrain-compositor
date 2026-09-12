# Detail materials and bounded recovery (issue #8)

The material path now separates pixel storage, uniform-region assessment, query
submission/completion, control-thread waiting, image upload, material properties,
buffer upload and SRG binding. `r_terrainDetailMaterialTiming` adds dimensions,
pixel/capacity bytes, region count and elapsed substages; leave it off during CPU
capture. Worker query scopes overlap the wait and must be reported separately.

`r_terrainUniformDetailMaterials` defaults to true. With no material regions, every
pixel is the passthrough material regardless of surface weights. Otherwise the
first intersecting region must contain the entire sampled rectangle and have no
surface mappings. An earlier partial overlap, touching edge or mapped surface
keeps the ordinary query. Region order and all four pixel bytes are preserved.
The assessment reads current region/material state on every update. It retains
no decision, worker result or source across frames, so changes and teardown use
the existing synchronous ownership boundary.

For region-specific uniform results, the rectangle is certified from an exact
dyadic grid with bounded integer coordinates, including asynchronous job
subdivision. Fractional or unrepresentable grids retain the original query.
No-region passthrough is independent of query coordinates. Allocation remains
request-local: the measured material bottleneck was surface-query waiting.

## Recovery policy

`r_terrainBoundedRecovery` defaults to true. It requires retained-only queries,
deferred preparation and upload staging. Supported updates above four requests
keep a bounded queue of coordinates, prepare
coarse levels first, then refine. Within each level, nearest requests lead and
stable order prevents newly admitted units from displacing older work.
Speculation above four requests is skipped; it must not bypass this limit and
later trigger an unnecessary full rebuild at the ordinary coverage-age deadline.

| Work or storage | Limit |
| --- | ---: |
| Pending coordinate descriptions | 512 |
| Recovery request captures / initial sector resource sets per frame | 4 |
| Recovery publications per frame | 1 whole unit, at most 4 sectors |
| All deferred publications per frame, including small ordinary groups | At most 3 whole units / 12 sectors |
| Upload/metadata operations per frame | 16, shared across all groups |
| Upload elapsed allowance | 1 ms soft deadline shared by completed groups, starting after admission |
| Vertex upload payload per frame | At most 4 MiB per RHI device at the supported grid limit |
| Queued/running/completed/cancelled dispatcher groups | 3 |
| Dispatcher requests | 96 |
| Dispatcher reservation plus fixed planning allowance | 256 MiB |
| Fixed planning allowance deducted from dispatcher capacity | 64 KiB |
| Worker concurrency | min(2, engine workers minus 2) |
| First useful coverage objective | 500 ms |
| Complete refinement objective | 5 seconds |

First coverage means known coverage at the camera's XY position in the renderer's
distance-filtered coverage projection. Valid retained coarse geometry or an
authoritative empty claim can provide it immediately. A partial child remainder
does not count, and an unrelated completed sector does not establish coverage.
The latency clock starts when the coordinate recovery plan begins. Structural
rebuild setup and cancellation polling before that point must be reported
separately; this clock does not cover all level-loading or invalidation latency.

`r_terrainRecoveryTiming` records recovery epochs, cancellation, first coverage,
refinement, unit acceptance and queue accounting while leaving verbose per-sector
diagnostics off. CPU traces and uninstrumented reference flights disable both
timing switches. Full sector timing remains useful for worker and capture details,
but its console overhead changes recovery latency substantially.

Allocation/driver operations cannot be preempted. Operation/byte limits are not
hard elapsed-time guarantees. Deadline misses are reported without cancelling
useful progress or draining workers. A source change, destination supersession,
teleport or switch change cancels whole units; cancelled work keeps its reservation
until workers and submitted upload fences return ownership. Rebuild waits through
nonblocking frame polling. Shutdown still joins workers and outstanding transfers
before releasing their owners.

The supported path initializes sector GPU resources as units are admitted. Common
mesh buffers, slot descriptions and shader configuration remain structural setup.
Live dependencies, missing retained ownership, unknown source byte estimates,
unsupported grids or plans above the coordinate cap retain explicit synchronous
fallback. These cases are outside the bounded policy and must remain visible in
measurement records.

## Proof and publication

The new `m_replacementSampling` capability is stronger than request-local sample
reuse. Only the built-in immutable composition adapter declares that identical
publication identity and the complete authority/revision tickets fix both raw
channels across independent acquisitions. Pointwise/subset support alone grants
no replacement permission. Exact dyadic regular and CLOD grids certify identical
shared coordinates, including every normal halo.

Each committed sector records its sampling certificate. A recovery unit must
match all overlapping retained neighbors and other unit members in sampler,
publication and exact dependency tickets. The proposed coverage applies the
renderer's distance filter to still-valid retained and proposed populated claims;
authoritative empty claims remain known. Old offscreen children do not block
relocation of their previous parents. Missing intermediate coverage and
duplicate claims reject the unit before any destination lease or resource changes.
All original settings, area, publication, dependency and destination checks remain
at final whole-unit acceptance. Raster and ray tracing publish together; old
resources retain their committed coordinates until replacement.

Reservations include pending input/result storage, retained source/publication
allowances and staged replacement resources. Cancellation does not release credit
early. Existing committed GPU buffers, driver allocations, BLAS memory and allocator
overhead remain outside the reservation ledger, as documented for upload staging;
256 MiB is not a process/GPU working-set guarantee.

## Validation and measured limits

The final profile build passed 407 runtime tests, 46 editor tests and 142,560 GPU
boundary classifications with zero mismatches. Fifteen offline profiling tests
and maintained override/hash checks passed. The earlier material-only build passed
401 runtime tests and the same editor/hardware checks.

Matched same-binary warmed captures measured a full terrain-process peak of
5.38 ms with uniform-query elimination off and 2.52 ms with it on. This short
capture meets the 4 ms terrain target; it does not establish a guaranteed maximum
or hitch-free flight. Traced whole-frame intervals contained approximately 285 ms
hitches outside the measured terrain peak.

On the final v5 implementation, the warmed full terrain-process peak was 2.3967 ms
(mean 0.9176 ms), with no reported profiler data loss. The enabled teleport capture
peaked at 2.8357 ms for the full process and 0.0228 ms for resource commit. It had no
worker-wait scopes and no reported data loss. The matched disabled capture retained
worker waits up to 353.27 ms, but dropped expensive parent events; its surviving
parent maximum is not a valid comparison. A 1.2973 ms staging call exceeded the
soft upload allowance while retaining the operation limit.

Lower-volume diagnostics measured stable startup first coverage in 60.485 ms and
refinement in 4.570 seconds. Teleport and return had immediate retained coarse
coverage and refined in 2.608 and 2.484 seconds. No unit rejections, recovery
rebuilds, synchronous fallbacks or coverage/refinement deadline misses occurred in
that pass. Startup publication changes cancelled three epochs; a later route reset
cancelled another. Missing per-group logging is unknown, not zero cancellations.

Cold startup remains a limitation. Its separate trace retained a 29.3536 ms full
terrain call, including 18.5614 ms material setup, and reported data loss during
level opening. Initial image transfer reached 16.4074 ms, individual sector resource
creation 9.0675 ms, mesh update 8.0140 ms and upload staging 8.5757 ms. These cold,
indivisible operations and bulk structural retirement are not hard wall-time
bounded. Data loss prevents treating the surviving startup call as a complete
maximum. The 4 ms evidence applies to the specified warmed route.

The matched uninstrumented reference peaked at 21.56 ms enabled versus 515.49 ms
disabled for teleport/return. Enabled warmed translation peaked at 18.01 ms; none
of its reference phases exceeded 50 ms. All seven final v5 Editor launches passed
provenance checks and exited cleanly, preserving all 62 authored hashes. The twelve
early/settled signed-boundary, teleport and returned images were inspected; settled
terrain matches, with expected temporary coarse detail during recovery. Manual
user flight acceptance remains outstanding.

Raw records and provenance are under TG's `build/windows/terrain-issue8` and
`user/TerrainFlightProfiling/terrain-issue8-*`. The failed initial baseline launch
had no provenance manifest and performed no route; the successful replacement
uses the `baseline-translation-v2` label. The recovery baseline reported profiler
data loss; surviving parent scopes cannot reconstruct its full control-thread peak.
