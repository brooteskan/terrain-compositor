# Sector preparation, committed coverage, and validated replacement

Issues #2, #3, and #4 keep `StartAndWaitForCompletion` and the existing
ordinary-query-then-overlay policy. Owned CPU work, opt-in procedural snapshots,
and one acceptance boundary serve a synchronous batch. These changes make no
performance improvement claim and do not enable cross-frame scheduling.

## Requested placement and committed ownership

`Sector` owns a destination identity, request serial, requested world coordinate,
preparation state, a weak cancellation handle, and exactly one `CommittedSector`.
The committed bundle owns the packed and CLOD buffers, RT buffers and mesh groups,
draw packets/views, SRGs, AABB/quadrants, existence, actual coordinate and LOD,
shader object constants, height origin, accepted serial, publication, and dependency
tickets. Its validity bit distinguishes unknown content from an accepted empty
sector. Relocation changes only the request. It cannot move the old AABB, change
its SRG translation, or relabel its RT mesh.

The existing destination identity remains the slot-lifetime authority. Its numeric
ID supports diagnostics; identity pointer and serial still govern acceptance.
There is no second lease system, retained mesh cache, or asynchronous queue.
The renderer owns all state changes; workers continue to own only CPU requests
and results. Capturing another request at the same coordinate supersedes the
previous serial just as relocation does.

| Event | Request state | Committed coverage |
| --- | --- | --- |
| Allocate or relocate | Requested | Unknown initially; otherwise retain valid old placement |
| Capture and dispatch | Requested → Preparing | Retain valid old placement, including known emptiness |
| Deliver complete populated/empty result | Ready | Unchanged until whole-group acceptance |
| Deliver incomplete/failed result | Failed | Retain only still-valid old coverage; request synchronous retry |
| Explicit cancellation or cancelled completion | Cancelled | Retain only still-valid old coverage; request retry |
| Newer request supersedes work | Requested → Preparing | Old completion cannot change the newer state |
| Accept complete group | Ready → Committed | Replace all bundle contents and then publish group visibility |
| Invalidate displayed content | Invalidated | Hide raster/RT coverage, release publication/tickets, request full rebuild |
| Reset, buffer clear, teardown | Retired | Withdraw RT registrations, release all slot resources and handles |

Workers may finish obsolete work. Results hold no sector or GPU reference, and
rejection changes no committed resource. Explicit cancellation uses the existing
cooperative flag; it cannot undo an accepted lease. Teardown retires the existing
manager dependency, so retained work can finish independently but cannot commit.

## Coverage selection

`SelectSectorCoverage` builds a bounded, temporary hierarchy from valid committed
world coordinates. It does not index retained geometry relative to requested grid
starts. Signed parent coordinates use floor division, including negative odd
coordinates. Initial slots and failed/pending results contribute no coverage.
Accepted empty results contribute authoritative coverage without a draw, preventing
coarser terrain from filling authored holes. A moving request also retains this
empty coverage at its original committed coordinate.

The hierarchy selects finer committed sectors within the existing LOD distance
thresholds. A coarser full mesh or quadrant can fill a wholly uncovered child
region. The same selection and masks drive raster candidates and RT registrations;
RT transforms use the committed object translation and height origin. A teleport
cannot drag retained geometry to the requested destination. Old content outside
the camera's distance thresholds is unselected; new areas with no valid fallback
remain unknown and hidden until their replacement commits.

A coarse quadrant cannot represent a partially covered child region when an
intermediate LOD is missing. Drawing it would overlap finer geometry or overwrite
finer holes. In that case, retain the finer coverage and leave the unrepresentable
remainder uncovered. This is an explicit limitation for delayed/partial LOD
population, not a promise of continuous coverage under a future scheduler. Normal
production still prepares and commits the complete synchronous update group.

Before reuse, committed coverage is checked against its exact active publication
and captured dependencies, using publication-then-sorted-dependency lock order.
Visibility is admitted at the renderer's control-thread selection boundary;
notifications after that boundary are observed at the next selection. Unknown or
invalid content never acts as a hole/fallback claim. Invalidation withdraws visible
RT meshes as well as raster candidates, even with a stationary camera.

**Conservative policy change:** manager terrain height/settings invalidation now
requests a full synchronous rebuild, including changes with a regional dirty
rectangle. Configuration or publication/source changes that invalidate retained
content also require full refresh. Previously regional notifications could refresh
only overlapping sectors. Since the existing manager authority invalidates all
captured tickets, retaining the other bundles under that authority would falsely
claim validity. A later optimization can prove regional equivalence, including
normal/CLOD halos, before narrowing this policy. This change claims no speedup.

## Replacement groups and reclamation

`ProcessSectorUpdates` captures one immutable `SectorCommitGroup` with the expected
result count for its entire synchronous update. Every request/result retains that
group. Missing members, mixed groups, duplicates, stale destinations, invalid
publications/dependencies, cancellation, and malformed payloads reject the whole
group before any lease consumption or committed mutation. Ready members remain
ready for a complete retry; failures/cancellation retain their explicit states.
The private ungrouped capture option supports existing independent lifetime tests;
production always supplies its full group.

Acceptance consumes all leases, withdraws affected RT registrations, replaces the
bundles through the resource sink, then marks every bundle valid/committed and
invalidates candidate selection. Object SRGs are queued for compilation before
drawing. Packed geometry, CLOD fallback/interpolation, RT decode, bounds, object
constants and coverage are one replacement. The publication and dependency locks
remain held across every sink invocation. GPU allocation/upload failures keep the
engine's existing handling; there is no new hardware transaction or rollback.

Neighboring sectors share edge samples and normal halos. Each CLOD buffer also
encodes the next LOD's samples and normals. A later scheduler must prove identical
configuration, publication, source tickets and shared edge samples across a split,
retain representable non-overlapping coverage (including empty claims), and move
raster/RT selections together. Independent ready sectors are not sufficient proof
that splitting a production group preserves CLOD boundaries. Removing the wait or
splitting a group is outside this change.

Each slot owns at most one GPU bundle. A new request retains that bundle in place;
CPU results are local to the synchronous batch and are reclaimed after its wait
and acceptance/rejection. Committed metadata retains publication/dependency identity,
not query-source snapshots, preparation settings, cancellation flags, or result
vectors. Reset/retirement clears candidates and SRG queues before slot destruction.
All five RT mesh-info handles per allocated sector are released, including groups
that were never visible or were removed from selection before retirement.

`GetPreparedCpuBytes` reports owned result/vector storage. `GetSectorResourceAccounting`
reports allocated GPU buffer bytes, bytes retained during replacement, committed
metadata/vector storage, and pending request count. Shared common buffers,
transitive publication/source allocations, diagnostic allocations and driver/BLAS
memory are not estimated. Counts are observations for future admission limits;
they do not impose a frame budget or add a cache.

## Owned work

`CapturePreparationSettings` copies the height range, grid dimensions, vertex
remap, RT XY positions, batching choice, and CLOD choice once per update batch.
Each `SectorPreparationRequest` owns its world coordinate, LOD, destination
slot/lease, regular-grid request and sampler, object SRG constants, RT choice,
area-existence result, cancellation token, retained render publication, acquired
procedural source set, and invalidation tickets. Settings are shared as const
values within the batch.
The area-existence query runs after ticket capture so even an empty-area result
participates in invalidation.

Worker lambdas capture only the request and owned result storage. `PrepareSector`,
`GatherMeshData`, `PrepareSectorLodData`, and `PrepareSectorRayTracingData` are
static functions. They have no manager, sector, scene, or GPU resource pointers.
Regular and CLOD query plans borrow only storage within their synchronous
kernel; their publication remains retained by the owned request. Query counters
belong to the result, and the kernel's temporary counter pointer never escapes.

`CaptureTerrainRenderQuerySources` first captures every composition notification
ticket, then acquires each opt-in source. The owned set retains the exact scene
publication plus replacement queries containing immutable source values. A source
ticket is copied from acquisition, never recaptured at execution time. The same
set is used for regular and CLOD gathers, including both normal halos, so numerical
configuration cannot change between them. Queries with no supported source retain
the original callbacks. Neither capture nor execution changes the scene registry.

The built-in height kernel and no-mask existence case have retained values as
specified in [TerrainRenderQueries.md](TerrainRenderQueries.md). Ordinary terrain
and unsupported procedural/mask providers remain live buses with call-scoped
handler lifetime. Requests still execute within the current synchronous update.
A future scheduler must cover these remaining live dependencies, unnotified
changes, and admission across frames. Source snapshots alone do not make the
whole request independent of ordinary terrain results.

Results own packed heights/normals, CLOD fallback/interpolation, decoded RT
positions/normals, AABB, existence, and diagnostics. Incomplete ordinary queries
return `Failed`, including scalar queries and incomplete CLOD queries. A complete
empty CLOD query still uses the original-height fallback. Hole normals have
defined zero storage, including when RT decodes sentinel-height vertices.

## Identity and invalidation

Each sector allocation owns a unique shared destination identity. A serial
advances on relocation, every captured request, and successful acceptance.
Validation checks identity, serial, world coordinate, LOD, and slot. This rejects
older work even after a coordinate round trip, a new request at the same
coordinate, or recreation of the same buffer slot. Requests retain identities,
not sectors or resources.

The manager owns a separate `TerrainPreparationDependency`. Configuration
changes, terrain height/settings notifications, and buffer reclamation invalidate
it. Initialization/reset starts a fresh authority; reset and destruction retire
the old one. Terrain invalidation conservatively covers all pending requests,
including normal and CLOD halos, without changing which sectors ordinary updates
request. Scene-channel removal retires that channel under its publication mutex,
clears activation/publication state, and removes scene registrations. Reusing a
scene address creates a distinct channel. Registry shutdown also retires retained
channels. The cutout feature processor retires its channel on deactivation while
preserving component-owned registrations for scene reactivation. Component
shutdown retains authority for removing those registrations.

Composition source subscriptions publish a separate invalidation authority with
their render query. Source notifications advance it immediately before entering
the deferred mailbox, including notifications coalesced with earlier edits.
Reconnection and shutdown retire it. Thus a source edit can reject work while the
immutable render snapshot still has the same pointer and revision.

Opt-in sources add their own activation authority and captured generation to the
request's existing dependency list. A configuration edit invalidates this ticket
before publishing new numerical settings, independently of delayed composition
notifications. Provider removal retires it, and reconnection creates a distinct
authority/session. Retained values can still be sampled after either event, but
the unchanged production `AcceptPreparedSectors` boundary rejects their results.
This also rejects old work captured from an unchanged scene publication after a
source generation becomes obsolete. Resource commit holds the source dependency
locks alongside the existing manager/composition locks.

All manager/destination mutation and completion acceptance remain renderer
control-thread operations. Workers and foreign-thread source notifications never
write manager/sector state. Dependency invalidation is synchronized independently
of the component and never calls back into it.

## Acceptance and commit

`AcceptPreparedSectors` is the single boundary for both populated and empty
results. It checks completion/cancellation, the active scene channel and exact
publication, dependency revisions, destination lease, and required packed/CLOD/RT
payload sizes. It validates the entire batch before consuming any lease or
invoking the commit sink. Duplicate completions and duplicate destinations in a
batch are rejected.

Lock order is the scene publication mutex followed by unique dependency mutexes
in address order. Both remain held through all GPU commits in the batch, preserving
the existing publication-generation guarantee. The commit sink performs no
terrain/source query or dependency notification. Tests inject a resource-mutation
observer at that same boundary; production supplies `CommitSectorResources`.

Only the accepted replacement path changes vertex/CLOD/RT buffers, committed
placement/existence/AABBs/quadrants, and object SRG constants/compilation queues.
Visibility selection and invalidation can withdraw RT/raster coverage without
accepting a replacement. SRG changes never precede acceptance.
Rejected batches request the full rebuild, so failed and
empty work remain retryable. Cancellation is cooperative before acceptance;
once leases are consumed, commit owns the result. No worker is forcibly stopped.
Owned requests/results are reclaimed after synchronous completion and release of
their final references; retaining them cannot extend GPU-resource lifetime.

## Diagnostics and verification

Profiler scopes distinguish capture/update, worker preparation, worker wait,
publication wait, acceptance, and GPU commit. `r_terrainSectorUpdateTiming`
remains false by default. When enabled, control-thread output reports accepted
and rejected counts, capture/dispatch, worker wait, summed worker preparation,
publication wait, acceptance, and commit time. Existing result-owned query
counters remain available. `TerrainSectorState` correlates numeric destination IDs,
request/accepted serials, requested/committed coordinates, committed LOD/publication,
validity, and transitions. `TerrainSectorReject` records obsolete request identity,
coordinates, reason and result bytes even when the slot has been superseded.
`TerrainSectorMemory` reports the available CPU/GPU accounting. State and reason
values follow their enum declaration order in the manager header. No contended
per-sample logging is added.

Deterministic tests delay and reorder actual owned requests and feed their
results through the production acceptance boundary. Coverage includes negative
coordinate crossings in the actual wrapped grid, coordinate round trips, slot
recreation, superseded/duplicate completions, populated and empty batch atomicity,
configuration and terrain invalidation, source notifications before deferred
refresh and during sampling, snapshot replacement, scene removal/address reuse,
registry shutdown, reset, cancellation, incomplete queries, CLOD fallback, and
preparation after manager destruction. A contender thread verifies that the
publication mutex stays held for every commit in a batch.

Maintained renderer patches and their normalized output hashes remain the source
of truth; generated build sources are only verification artifacts.

### Issue #4 verification on 2026-09-11

Implementation: `3303fb462f0d6ea4985d41dc9abb89a3a162d375`, based on
`627a39fb2a76202e9f2032903ee88fc016f45afa`, on local branch
`codex/sector-committed-coverage`. Engine:
`061180bf24f1666eb30315b35da292eb14f4659c`. TG remained at
`6db89b4dcc60cf690c6008e94572f30016180702`, with its compositor pin at
`894ad31e43335436f8b36e1a3ce647bad5f14b1e`.

- **353 runtime and 46 editor tests passed.** Two existing optional runtime
  benchmarks remain disabled. Eight new lifetime tests cover committed placement,
  complete fine/coarse groups, missing/failed/empty coverage, absent intermediate
  LODs, cancellation, publication/source retirement, worker-storage reclamation,
  and the production RT registration path. The three existing signed grid tests
  additionally verify retained placement through crossings and teleports.
- **142,560 boundary classifications matched on D3D11 hardware**, with zero
  mismatches. Engine override generation, repeat generation without rewriting
  unchanged output, and incorrect input/output hash rejection passed.
- Rebuilt `Terrain.Static`, `TerrainCompositor.Static`, runtime/editor modules,
  and both compositor test modules in the MSVC profile candidate build at
  `D:/TG/TGProject/build/render-query-candidate`. Standard target builds used
  existing engine dependencies (`BuildProjectReferences=false`); this was not a
  clean build of the entire engine. The separate `build/windows` reference output
  was preserved.
- The final Editor opened DefaultLevel, confirmed all seven requested camera
  positions through the camera getter, crossed X/Y boundaries, teleported to
  `(1024,-1024,128)`, returned to `(16,16,32)`, captured a rendered frame, and
  exited with code **0**. Its 15,755 state records contain 2,551 committed
  transitions and 2,204 records retaining valid coverage at a different requested
  coordinate. The audit found no committed-coordinate change before acceptance;
  every committed coordinate matched its accepted destination.
- Live accounting reported 266,256,000 allocated sector GPU buffer bytes and
  266,524,000 owned pending CPU bytes for a full 500-sector batch. These exclude
  the shared/transitive and driver allocations described above. Synchronous
  completion left zero pending requests and zero replacement-retained GPU bytes
  in the post-commit records.
- All **62 authored-file hashes**, including the pre-existing Blender edits,
  matched before and after verification. Batching and RT stayed enabled; optional
  timing was restored to false. Temporary TG integration build inputs were backed
  up and restored; its Git pin and authored content were not updated.

The live Editor retained the startup/shutdown warning categories seen in the
previous verification (audio, console ranges, missing macro/icon resources and
DynamicDrawContext shutdown). No terrain error or assertion failure was reported.
This checks rendering/lifetime integration with intrusive diagnostics, not frame
time or hitch-free flight; TG #37 remains outside this acceptance.

Normalized generated SHA-256 values:
`be2ecda6872fb36794cb086e985c51f6f80afa76784184ae227bd7b592a575dc`
(manager CPP),
`2ad55b8b7eeee84f6b7c0cacf79d111582638f513a1c0ba66a896d2421a2f03f`
(header). The complete compiled-source digest is
`c1c419b2bc59499dd3b8fa273a017f2a7976209b2701793f3ec02c7361ae3ed5`.
Raw XML, build/Editor logs, camera getter results, screenshot, source and binary
hashes, override checks, authored-file hashes, and the restoration manifest are
under `build/sector-coverage-session` in the standalone checkout.

### Issue #3 verification on 2026-09-11

The source-snapshot integration passed 345 runtime and 46 editor tests, including
the 32-test snapshot/sector-lifetime subset. New production-boundary checks cover
identical packed regular/CLOD vertices, halo normals and RT decoding; source
changes during ordinary sampling; retained preparation after source destruction;
reconnection and publication replacement; and rejection of an original stale
ticket even when an acquisition callback offers a newer ticket.

Final Editor diagnostics confirmed the retained source set is used by live sector
preparation while ordinary queries and synchronous scheduling remain enabled.
Generated manager SHA-256 values are
`6330886d2866b508c8075891dbe8e8e68b237dac53c3bcc9c351c00bb5938463`
(CPP) and
`5509421017b864c411e1070a7f7d838196ecdbaaff637f18dde23509779504fe`
(header). The [render-query verification record](TerrainRenderQueries.md#issue-3-verification-on-2026-09-11)
contains exact provenance, shader-build remediation, remaining warnings, and
artifact locations. No frame-time improvement or cross-frame policy is claimed.

### Historical issue #2 verification on 2026-09-11

- MSVC 19.51, profile configuration, pinned O3DE
  `061180bf24f1666eb30315b35da292eb14f4659c`.
- **328 runtime tests and 46 editor tests passed.** The runtime suite includes
  15 new controlled sector-lifetime tests, the new source-mailbox test, and
  delayed-completion checks in all three existing wrapped-grid crossing tests.
  The two existing optional benchmarks remain disabled.
- D3D11 hardware compared **142,560 boundary classifications with zero mismatches**.
- Maintained engine-override generation, unchanged-output regeneration, and
  incorrect input/output hash rejection passed. Current normalized SHA-256:
  `43074f2c46a85cda9ca612e420869eb6aa8daedbbeac939a450aaf90b31d4c5d`
  (manager CPP),
  `12d823c809e749d20c804ec1f8489d0c26b968744ab2b7230d065c060abea99e`
  (manager header).
- All **150 implementation/test source files** match between the standalone
  checkout and TG's compositor integration, normalizing line endings.
- Rebuilt `Terrain.Static`, `TerrainCompositor.Static`, runtime/editor modules,
  and both compositor test modules. The final Editor opened DefaultLevel and
  exited with code **0**. Its diagnostic run recorded **1,000 accepted results**
  in two 500-sector batches, with no rejected batches. Preparation, worker wait,
  publication wait, acceptance, and commit diagnostics were exercised.
- All **62 hashed authored files**, including the pre-existing Blender edits,
  were unchanged. Batching and RT remained true; sector timing was restored to
  false. This was a startup/lifetime check, not camera-flight performance acceptance.

The candidate uses `D:/TG/TGProject/build/render-query-candidate`; the existing
reference build at `D:/TG/TGProject/build/windows` was preserved. TG started at
`f83946ed78e4461898cf2476a057612e1cda64f8`. Its detached compositor integration
was put on the existing `main` branch and merged with section 1, preserving the
earlier integration commit. The standalone `main` fast-forwarded to the same
content-equivalent integration history (`d7b5062`). No new branch or worktree was
created. Raw XML, build logs, override checks, authored-file hashes, Editor
diagnostics, and verification manifests are retained under
`build/sector-lifetime-session` in the standalone checkout.
