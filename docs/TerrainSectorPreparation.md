# Owned sector preparation and validated commit lifetime

Issues #2 and #3 keep `StartAndWaitForCompletion` and the existing
ordinary-query-then-overlay policy. Owned CPU work, opt-in procedural snapshots,
and one acceptance boundary serve a synchronous batch. These changes make no
performance improvement claim and do not enable cross-frame scheduling.

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

Only the accepted sink changes vertex/CLOD/RT buffers, RT mesh registration,
sector existence/AABBs/quadrants, object SRG constants and compilation queues,
or candidate-sector invalidation. In particular, SRG changes no longer precede
acceptance. Rejected batches request the existing full rebuild, so failed and
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
counters remain available. No contended per-sample logging is added.

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
