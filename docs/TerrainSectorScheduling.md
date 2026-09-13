# Sector scheduling and atomic replacement policy

TG #38 now implements the bounded retained-only dispatcher and request-local
sample reuse described in [TerrainDeferredPreparation.md](TerrainDeferredPreparation.md).
The issue #6 history below records the original synchronous decision boundaries;
the new document specifies enabled policy, lifetime proof, limits and recovery.

Issue #6 establishes decision boundaries, deterministic tests and requirements
for a future scheduler. Production still captures one update group, starts its
jobs, waits for every job, and accepts the whole group on the renderer control
thread. No deferred scheduling or performance improvement is claimed. Numeric
limits and same-route performance acceptance belong to TG #37.

## Units and decisions

| Boundary | Unit and authority | Decision |
| --- | --- | --- |
| Capture | Existing owned `SectorPreparationRequest`, destination identity/serial, cancellation flag, settings and complete sampling plan | Capture the entire regular grid, all normal halos, conditional next-LOD CLOD gather and derived RT work together. |
| Execute | One complete request is one CPU scheduling unit | `AssessTerrainSectorAdmission` runs after sampling-plan assessment, before geometry queries. Invalid layouts and cancellation stop preparation. Live ordinary/procedural/mask dependencies retain synchronous execution. |
| Deliver | One owned `PreparedSectorResult` | `DeliverPreparedSector` records receipt and updates only a current request's state. It cannot consume a lease, mutate resources or publish coverage. |
| Replace | Existing immutable `SectorCommitGroup` | `ValidateSectorReplacementUnit` requires all expected, distinct members of the same group and the same settings capture. Readiness order does not change membership. |
| Commit | Complete validated replacement unit | `AcceptPreparedSectors` checks current publication, dependency tickets, destinations, statuses and payloads, then the upload-byte allowance, before consuming any lease or changing any bundle. |
| Select | Transient valid committed coverage claims | `SelectTerrainSectorCoverage` supplies one projection for production raster and RT. It also exposes duplicate claims and unrepresentable child remainders for replacement assessment. |

The group ID is diagnostic correlation, not a second lifetime authority. Group
pointer identity and expected membership count continue to define a replacement;
destination identity and serial define where each result may commit. Settings are
captured once and shared by production group members. The private ungrouped path
remains for independent lifetime tests; a scheduler must not erase group identity
to admit a ready subset.

Admission is not commit authorization. The sampling assessment reports all live
dependencies and across-frame capability, while the enabled policy remains
synchronous even for a hypothetical fully retained plan. The current ordinary
query service and captured area-existence decision lack across-frame lifetime
proof. Unsupported procedural sources and masks add further fallback reasons.
Retaining an immutable composition publication or observing valid tickets cannot
keep a live bus handler alive. A future dispatcher must assess the whole request
before enqueueing deferred work, without copying source acquisition onto a worker
that has no lifetime guarantee. The current assessment stays on the worker to
avoid adding the full ownership walk to the control thread.

## Smaller replacement units

No smaller production group is enabled. `AssessSectorSplit` and
`AssessTerrainSectorSplitSampling` expose executable blockers rather than an
unchecked caller-supplied assertion of safety:

- Settings must be the same immutable capture. This covers height packing,
  vertex remap, grid shape, CLOD and batching choices.
- Scene channel/publication must match exactly. Source tickets must identify the
  same authorities **and** revisions, not merely equal numeric generations.
  Actual commit still validates liveness under locks; pairwise equality is not
  freshness validation.
- Both complete sampling plans must cover remaining live dependencies. A result
  being ready, or an empty area decision, does not prove deferred eligibility.
- Inclusive regular-grid halo intersection identifies shared edge/normal sample
  obligations. Regular/CLOD and CLOD/CLOD intersections identify next-LOD
  obligations, even when the regular grids do not intersect.
- The proposed complete post-replacement coverage must be representable without
  duplicate claims or a coarse quadrant covering a partially populated child.
  Pass **all** still-valid retained neighbors and proposed replacements into the
  same coverage projection used by rendering. Unknown destinations are omitted;
  accepted empty claims are included. Projection does not validate source
  generations, shared numerical samples or route coverage continuity.

Packed results do not retain the raw shared halo samples. Equal packed edge
heights or normals do not prove equality of all inputs to normal calculation,
existence and CLOD. Shared sampling intersections therefore remain blocked even
when tickets match. A future implementation must establish deterministic sample
equivalence across the cut (including already committed neighbors), or carry and
validate the required evidence. It must validate current settings, publications,
source tickets and destinations again at commit, and publish raster/RT changes
together. The unconditional `WholeGroupPolicy` blocker prevents the diagnostic
assessment from accidentally granting split authorization. Spatially disjoint
requests are not automatically independent replacement units.

## Admission, memory and fairness requirements for TG #37

Production has one local batch, no persistent queue, and the engine job manager's
existing worker concurrency. All CPU results are released after synchronous
acceptance/rejection; each destination still owns at most one GPU bundle. The
following rules are requirements for the later scheduler, not implemented numeric
limits or promises about this synchronous batch:

1. Bound queued requests/groups, running jobs, completed uncommitted results and
   total retained bytes separately. Reserve peak input, scratch and result
   storage **before** dispatch, counting a running cancellation until the worker
   acknowledges it. Reserve completed-result capacity before starting a worker
   so completion cannot overflow a full queue. Bound shared source/publication
   generations retained by queued, running, completed and committed work too.
2. Limit terrain worker concurrency below the available job capacity with an
   explicit allowance for other engine jobs. Backpressure stops new admission;
   it cannot evict valid displayed bundles merely to make room. Keep distinct
   headroom for completion/reclamation and for a critical coverage replacement.
3. Prioritize unknown visible coverage, then expiring/invalid coverage and the
   camera's upcoming boundary, then refinement and prefetch. Prioritize replacement
   groups so an easy member cannot strand the remaining members. Use bounded
   aging or a reserved share for older groups, including coarse prerequisites.
   Camera prediction is a hint; it must not starve work after a reversal.
4. Coalesce queued requests by existing destination/serial; cancel superseded
   running work cooperatively. Reclaim completed obsolete results promptly on
   the control thread. Never release worker-owned storage before job completion
   or treat cancellation as permission to consume a stale destination lease.
   Retain only one desired request per destination, with a bounded allowance for
   still-running obsolete work. When the allowance is exhausted, stop admission.
5. Do not spin on failure. Reissue after source/publication readiness changes or
   bounded retry backoff; budget waits reuse valid ready data without resampling.
   Give retries a bounded share and expose their cause. Every retry captures
   current dependencies and a new serial; obsolete completion cannot mark the
   new request failed. If a required group cannot be admitted, report the reason
   and choose a documented synchronous recovery point before route coverage is
   lost; do not silently accumulate unbounded debt.

`GetPreparedCpuBytes` counts owned result/vector/dependency capacity.
`GetSectorWorkStorage` separates pending (not delivered) and delivered completion
storage, deduplicates shared settings capacity, and counts retained source sets
and publications by pointer identity, including their direct object/vector
capacity bytes. Delivery accounting is independent of optional timestamps.
These are not transitive byte estimates: callbacks, source values and prepared
cutout/gap data can retain additional allocations. The selector's temporary maps,
worker scratch, job allocations, shared_ptr control blocks, allocator overhead,
diagnostic storage, shared mesh buffers, driver allocations and RT BLAS storage
are also outside these counts. Source-set counts can exceed distinct source-value
counts because requests acquire separate sets. A hard admission limit requires
accounting/estimates for these missing terms and a measured safety allowance;
these counters alone must not be advertised as a total memory bound.

Accounting may inspect pending storage only before dispatch or after joining its
workers; production reads it after the join. Polling result vectors while workers
write them is a data race. A future scheduler needs synchronized completion
handoff and reservation accounting for running work, not concurrent vector reads.

## Coverage through movement and invalidation

| Event | Required coverage behavior |
| --- | --- |
| Boundary crossing, including signed zero | Keep valid old/coarse bundles at their committed coordinates until replacement commits. Use signed floor-parent mapping; requested toroidal slot placement cannot translate retained raster/RT geometry. |
| Rapid reversal | Supersede obsolete requests, retain valid old placement and authoritative emptiness. Revisit with a new serial; a coordinate round trip cannot resurrect old work. |
| Teleport | Old coverage stays at its old coordinate and is distance-filtered. New terrain without valid committed fallback is unknown. Prioritize complete new coverage groups; an arbitrary teleport cannot promise immediate known terrain. |
| Startup | Unprepared slots have no coverage claim. Prepare complete groups; do not interpret unknown slots or failed results as empty terrain. |
| Empty authored terrain | Accepted emptiness remains a coverage claim during movement and replacement, preventing coarse terrain from filling the hole. |
| Missing intermediate LOD | Preserve finer populated/empty claims. A coarse quadrant cannot fill the remaining partial child without overlap or filling a hole; leave the remainder unknown and schedule the missing prerequisite/group. |
| Edit or source/publication invalidation | Withdraw invalid raster/RT coverage, even with a stationary camera. Retain only still-valid bundles. Existing manager-wide invalidation conservatively requests a full synchronous refresh, including normal/CLOD halos. |
| Scene removal, reset or shutdown | Retire the existing channel/dependency authorities, withdraw RT/raster registrations, cancel/reject work, join/reclaim owned results and release slot resources. No retained request owns a manager or GPU bundle. |

Missing coverage is unavoidable at startup, outside known data after teleports,
after invalidation with no valid fallback, or when the available hierarchy cannot
represent a partial child. A later scheduler must define coverage-age and unknown
area limits, admit prerequisites before crossing a boundary, and preserve empty
claims as carefully as populated claims. It must compare the same warmed routes,
including reversals and boundary crossings, to the synchronous baseline and
demonstrate no new visible gaps, cracks, overlaps, normal/CLOD discontinuities or
RT disagreement. No policy-only guarantee replaces those measurements.

## Atomic upload allowance

`AssessTerrainSectorCommitBudget` separates three outcomes: commit the complete
group, wait for replenished allowance, or report an atomic group larger than the
maximum allowance. `AcceptPreparedSectors` uses this decision **after** validity
checks and **before** lease consumption, resource uploads or visibility changes.
The default allowance is unlimited, preserving synchronous production behavior.
Tests exercise finite allowances at that exact resource-sink boundary.

The allowance counts packed regular/CLOD and RT vertex payload bytes. It omits SRG
compilation, allocation, driver/BLAS work and other overhead. An accepted empty
group can have zero vertex bytes while still incurring commit work. This is not a
time bound. A timer measured after an upload cannot enforce a hard frame budget.

If a valid group exceeds the maximum allowance, waiting another frame cannot fix
it. There is currently no proven smaller grouping and no staged-upload mechanism;
the result is `AtomicGroupTooLarge`, with zero resource mutation. A future scheduler
must either establish a proven split or separately design bounded staging buffers,
upload progress ownership, cancellation/reclamation, final generation validation
and atomic visibility publication. Staging cannot overwrite displayed buffers.
A separately chosen synchronous recovery may exceed a soft frame target, but
must be explicit and measured. Never consume part of a group or silently turn the
allowance into a per-sector budget.

## Diagnostics and locks

Existing optional `r_terrainSectorUpdateTiming` remains off by default. New records
correlate group ID with destination ID/request serial, capture/start/completion/
delivery timestamps, admission/result causes, and pending/delivered storage.
Committed state records include group, monotonic timestamp and coverage age;
rejection records retain correlation even after supersession. A zero timestamp
means timing was disabled at that boundary. Stage timestamps are observations,
not synchronization. Cancellation now checks before preparation, after regular
gather, after CLOD work and before completion; synchronous bus calls in progress
must return before cooperative cancellation can proceed.

Acceptance retains publication-then-sorted-dependency lock ordering through every
resource sink call, with no source queries under commit locks. Coverage visibility
observes current publication state and atomic dependency revisions on every call;
unchanged shared metadata skips duplicate sector walks. These observations never
grant a commit lease. The locked visibility fallback remains available. See
[control storage](TerrainControlStorage.md) for cache invalidation and ownership.
Completion delivery and storage reclamation are renderer control-thread operations;
workers retain owned requests/results and observe atomic cancellation.

## Verification (2026-09-11)

Validated the local `main` changes based on compositor `03a61c2`, with O3DE
`061180bf24f1666eb30315b35da292eb14f4659c` and the MSVC profile candidate at
`D:/TG/TGProject/build/render-query-candidate`:

- 375 runtime tests and 46 editor tests passed; two existing optional runtime
  benchmarks remain disabled. The 11 added tests cover independent delivery,
  complete-group membership/settings, finite atomic upload allowances,
  invalidation while waiting for allowance, cancellation between gathers,
  delayed storage reclamation, live-plan admission and conservative split checks.
- The coverage oracle checked all 81 populated/empty/unknown child combinations
  at 16 signed parent coordinates, plus missing intermediate LODs, duplicate
  claims and extreme signed coordinates. Existing production tests passed for
  zero crossings, reversals, teleports, slot reuse, raster/RT placement, source
  retirement, publication locks and shutdown.
- The D3D11 hardware shader test matched all 142,560 boundary classifications.
- Maintained override generation, unchanged-output preservation and incorrect
  input/output hash rejection passed. Generated manager hashes are
  `04fda8c2520579594d8031ebbf54c5b388128ea59155174653e257929a3aaf02` (header) and
  `68c8005e9cb5f3777d92b6c081999f418b8f44995dbe5b8e47a7a23cc5921933` (CPP).

The affected terrain/compositor static libraries, runtime/editor modules and test
DLLs were built against the candidate's existing engine dependencies. Verification
used temporarily staged TG integration sources, with source/hash provenance and
test XML under `build/sector-scheduling-session`; those integration inputs were
restored afterward. All 62 tracked authored files checked retained their original
hashes, including the pre-existing Blender edits. No Editor flight measurement,
asynchronous default, numeric scheduling limit or performance gain is asserted.
