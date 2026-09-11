# Architectural refactoring for terrain optimization

This is the current architecture ranking for [terrain-compositor #1](https://github.com/brooteskan/terrain-compositor/issues/1), preparing the compositor and its renderer integration for optimization under [TG #37](https://github.com/brooteskan/TG/issues/37). It replaces the LOC-reduction ranking. The objective is to make ownership, query semantics, preparation and publication explicit enough to optimize safely and measure the result.

Rank work by the optimization it enables, evidence of relevant runtime cost, dependencies, behavior risk and characterization effort. LOC is descriptive maintenance information. A justified request/result type, cancellation owner, test or measurement interface may add code. The previous percentage target, net-savings gate and reduction-ceiling task are retired.

## Evidence and baseline

The source inspection uses `19b6d1b09b478b2af9f0c2288aac9cc1ffcd0cab` plus the stable-removal changes now committed as `9dc34a1`. This is the architecture baseline, not the revision used for earlier flight measurements. Registration traversal improvements remain valid prior work; they are not evidence of a camera-flight speedup.

TG #37's [latest update](https://github.com/brooteskan/TG/issues/37#issuecomment-5593180935) records the sector-wrapping correction and prioritizes duplicate terrain queries and render-thread worker waits. The checked-in [flight investigation](TerrainCameraFlightProfiling.md) and [measurements](TerrainCameraFlightMeasurements.json) provide the detailed 2026-09-08 evidence:

- Correcting sector wrapping reduced same-route worst frames from about 450 ms to 132-151 ms. Remaining boundary updates involve 10/20/30 sectors and still cause substantial waits.
- Ordinary and retained queries dominate the captured worker time. No composition publication changes occurred during the measured route.
- The procedural-only control still reproduced stalls. Asset preparation, stamp-specific sampling, registration indexes and mesh validators have not been established as their dominant cause.
- Lower sample resolution reduced cost but changed quality. Preserve authored quality and content during architectural refactoring.

These are historical measurements, not newly reproduced results on this worktree. Refresh the baseline on the actual build before optimizing, recording Gem/project/engine revisions and generated override paths. Preserve the established lifecycle owners, immutable composition publications, typed revision domains, numerical contracts, diagnostics and public serialization.

In the scopes below, **S** means `integrations/o3de/Code/Source/`, **I** means `integrations/o3de/Code/Include/TerrainCompositor/`, **T** means `integrations/o3de/Code/Tests/`, and **P** means `integrations/o3de/EnginePatches/TerrainRenderer/`. Source line references use the inspected worktree; patch scopes use symbols/hunks because generated and upstream line numbers differ. Every new helper, contract type, adapter, build entry and fixture belongs to the complete review scope.

## Ranked architecture work

| Rank | Refactoring boundary | Optimization enabled later | Evidence / priority | Readiness criterion |
| --- | --- | --- | --- | --- |
| 1 | Explicit render-query ownership and batch execution contract | Avoid duplicate ordinary/retained evaluation where equivalence is proven; improve batching | Directly addresses sampled worker cost and TG #37 follow-up | Classify supported ownership/fallback cases before execution; preserve current outputs and source-call behavior |
| 2 | Owned sector request, prepared result and validated commit lifecycle | Budget completion across frames, cancel obsolete work and bound in-flight storage | Directly addresses synchronous worker wait; requires coherent query inputs | Test preparation with owned inputs and delayed completions; only accepted current results reach GPU resources |
| 3 | Explicit preparation dependencies and invalidation outputs | Reuse unaffected contributions and reduce preparation/publication work during edits | Edit/scalability concern; no publication churn in the historical route | Identify exact artifact inputs and preserve diagnostics, ordering and footprints |
| 4 | Model preparation acceptance and source-generation contract | Safely deduplicate jobs/extracted geometry and schedule/cancel asset preparation | Reload/import concern; lower priority for measured flight stalls | Characterize reentrancy and independent results; decide authority before sharing results |
| 5 | Independently measurable sampling and geometry kernels | Targeted batching, scratch reuse, traversal and data-layout improvements | Promote individual kernels when profiling identifies them | Explicit input/output/scratch and numerical contracts, behavioral oracles and stage measurements |

Start with ranks 1 and 2. Ranks 3-5 are conditional follow-ups, not prerequisites for the first duplicate-query optimization. Small helper extractions do not outrank a missing ownership or lifetime contract because they delete more code.

### 1. Render-query ownership and batch execution

**Implemented boundary.** Section 1 of issue #2 now has an explicit request,
capability, retained ownership plan, and executor in the actual scalar and batch
render paths. See [the render-query contract](TerrainRenderQueries.md) for the
preserved execution policy, source dependencies, fallback reasons, diagnostics,
and differential verification. Ordinary-query elimination remains subsequent
work; the discussion below records the motivation and constraints for that work.

**Current responsibility and scope.** `I/TerrainMeshCutoutRenderRegistry.h:20-151` defines retained scalar/batch callbacks, first-match height/existence ownership and ordinary-result overlay. `S/TerrainMeshCutoutRenderRegistry.cpp::RebuildSnapshot` aggregates callbacks. Coordinator queries at `S/Components/TerrainCompositionGradientComponent.cpp:945-1265` create callbacks and implement normalized-height, terrain-height and surface batches; include its component header and `S/Components/TerrainCompositionQueryHelpers.h`. `P/TerrainMeshManager.cpp.patch::GatherMeshData` executes ordinary `QueryRegion` before applying retained geometry; `SectorDataRequest` is in the corresponding header patch.

**Architectural change.** Separate ownership resolution from execution with a small plan that retains its snapshot and describes supported runs/channels and required ordinary fallback. Give batch kernels explicit query purpose and retained state. The first extraction keeps ordinary-query-then-overlay behavior while making its decision inputs observable. Consolidate repeated normalized-height batch mechanics only where source checks, callback timing and output semantics match.

**Contract still to establish.** Having `m_getGeometry` on the same owner is insufficient proof of ordinary-query independence. Its current contract receives ordinary surface Z, including collision fallback. Procedural source/existence buses can remain live calls even with retained composition state. A later fast path needs an explicit opt-in guarantee about coordinates/Z, sample-grid/sampler behavior, source availability, both output channels and dependence on ordinary results. Unknown, legacy, partially owned and unsupported cases retain ordinary fallback. A retained composition pointer alone does not freeze external procedural state.

**Preserve and characterize.** Inclusive XY boundaries, existing first-match order, overlaps, independently owned channels, scalar-only providers, unowned points, missing/disappearing providers and empty/mismatched spans. Render topology uses image holes while collision queries can use cutouts/gaps. Compare normalized/clamped height, existence, final-height surface points, normals, CLOD inputs and packed output. Exercise reentrant callbacks and publication replacement, retaining one publication per batch. Keep call counts/order during extraction; later optimization must identify its intended call-count change.

Extend `TerrainRenderGeometryBatchTests`, `TerrainPublicExistenceTests`, `TerrainMeshCutoutRenderRegistryTests` and `TerrainSectorPreparationTests` in `T/TerrainMeshCutoutTests.cpp` with an ownership/fallback matrix and 0/1/255/256/257-point batches. Include overlap edges, split channels, out-of-region Z and an existence callback dependent on input Z.

**Measurement and complete scope.** Record ordinary/retained/scalar/batch samples, proven coverage, fallback reasons, source calls and matched sector preparation time. Include plan/result declarations, capability contract, resolver/executor, component adapters, renderer patch callers and tests. A plan type unused by the actual decision path is not architectural progress. Query elimination belongs to a subsequent measured policy change.

### 2. Sector preparation and commit lifetime

**Current responsibility and scope.** `P/TerrainMeshManager.cpp.patch` contains `CollectUpdatedSectors`, `GatherMeshData`, `PrepareSectorLodData`, `PrepareSectorRayTracingData`, `ProcessSectorUpdates` and `UpdateSectorBuffers`; include the sector/request declarations in `P/TerrainMeshManager.h.patch`. `ProcessSectorUpdates` owns local `PendingSector` results, launches workers capturing `this` and `Sector*`, waits for every job, then checks the render snapshot under the publication mutex before uploading. Data staging exists; lifetime and scheduling remain tied to the synchronous call.

**Architectural change.** Define an owned sector request and prepared result with one acceptance/commit operation. Capture coordinates, LOD, grid/height settings, CLOD/RT inputs and retained query ownership; identify manager-owned arrays/services still needed by preparation. Use a stable sector identity/lease so a reused buffer slot cannot accept work for its former coordinate. Carry scene, configuration and relevant source/terrain invalidation identity alongside render publication identity. Live procedural callbacks mean the existing render-snapshot check is not a complete future cancellation protocol.

The first extraction preserves synchronous scheduling and publication order while making request construction, CPU preparation, completion acceptance and GPU mutation independently testable. A cross-frame queue, admission limits and changed coverage latency are later optimization policy. Simply deleting `StartAndWaitForCompletion` would leave unsafe captures and resource reuse.

**Preserve and characterize.** Delay/reorder completions, then relocate/reuse sectors, cross signed-coordinate zero, change configuration/source data, replace publications, remove scenes and shut down. Reject obsolete results before GPU mutation, including empty-result updates. Preserve normals, CLOD remapping/no-data fallback, RT decode, AABBs, candidate-sector invalidation and SRG work. Characterize the lock/acceptance/upload boundary. Use existing sector/grid tests and a deterministic fake executor for lifetime/acceptance tests.

**Measurement and complete scope.** Include request/result types, lifecycle state, executor adapter, completion routing, renderer resources, tests, `I/TerrainMeshCutoutRenderRegistry.h`, `S/TerrainMeshCutoutRenderRegistry.cpp` and snapshot/admission coordination in `S/TerrainMeshCutoutFeatureProcessor.cpp`. Changed engine behavior must update maintained patches and expected output hashes in `integrations/o3de/EngineOverrides.cmake`, verified by `T/EngineOverridesTests.cmake`; generated sources are not the implementation.

The later scheduler must specify bounded in-flight bytes/jobs, cancellation/reclamation, fairness, upload budget and old/coarser coverage until replacements are ready. Measure queue depth, retained bytes, obsolete completions, worker wait, commit/upload time and coverage latency. Moving the existing raw captures into a helper does not establish ownership.

### 3. Preparation dependencies and invalidation

**Current responsibility and scope.** `S/CompositionPreparation.cpp:38-236`, its header, `I/Internal/PreparedComposition.h`, `S/CompositionInvalidation.h`, `S/PublicationState.h`, `I/Internal/PendingCompositionInvalidation.h`, `I/Internal/CompositionRegistrations.h`, and coordinator classification/publication at `49-157,1331-1489`. Preparation is already value-oriented and separate from publication. Each publication currently traverses/prepares contributions; dirty classification largely controls subsequent invalidation rather than selecting reusable artifacts.

**Architectural change.** Establish an input/dependency contract for image height, surface, mask, mesh height/gaps, cutouts, palette, ordering collisions, reconstruction and collision-cell coverage. Make captured environmental inputs such as scale presence, region bounds and collision spacing explicit. Identify preparation that can be shared within a publication, such as image placement, and keys that could later prove an old artifact equivalent. Establish dependencies and ownership before introducing another cache.

**Preserve and characterize.** Index tie payload equality differs from dirty classification, especially for detailed mesh diagnostics. Retain typed stores and channel-specific policies. Invalid claims can suppress other ordering claimants; zero-strength gaps still matter. Retain diagnostics-only edits, absence/loading/failure/recovery and warning retirement. Region/palette/spacing changes have wider effects than one registration. Footprints precede sorting; preserve first-claim selection, old/current contexts, cutout intersections, distinct render/query footprints and rejected-publication retries.

Use lifecycle, scan-equivalence, footprint and invalidation tests. Add a field-by-field dependency matrix, and compare any later incremental result with full preparation. Measure prepared/reused artifacts, rebuild causes, allocation/retained bytes and invalidation regions on edit/retarget/reload workloads. Include dependency/key types, all preparation callers, any cache owner, adapters and tests. Prioritize this when edit workloads show the cost; the historical flight trace does not establish publication churn as its bottleneck.

### 4. Model preparation acceptance and source generation

**Current responsibility and scope.** `S/ModelAssetSource.h`, `S/TerrainMeshCutoutDataCache.cpp:15-124`, `S/TerrainMeshHeightDataCache.cpp:29-115`, their cache headers and `S/TerrainModelGeometry.cpp`/`I/TerrainModelGeometry.h`. Image acquisition at `S/HeightmapDataCache.cpp:533-566` repeats canonical weak-source lookup; its image/mip loader at `40-518` has a different lifecycle.

**Architectural change.** Establish accepted-job/result semantics before sharing dispatch or extraction. Cutout takes its per-source ticket before `Publish(Loading)`; mesh height stores a globally allocated ticket after the signal. Characterize nested ready/failure delivery and reentrant queue pumping before choosing a policy. Keep role-specific build/validation results and diagnostics explicit.

Decide source-generation authority separately from prepared-result revision. If sharing model extraction is justified, define a source token valid across both consumers' reload/removal/lifetime transitions, with separate preparation keys/results. Otherwise document independent ownership as the intended architecture. Asset-ID equality and numerically comparable unrelated counters do not establish authority. Missing/unassigned IDs, aliasing, final retirement, reload failure and old retained query data need defined behavior.

**Preserve and characterize.** Extend `T/TerrainModelCacheLifecycleTests.cpp` beyond rejected empty-model cases with successful geometry, opposite completion orders, nested Loading callbacks and retained old results. Keep invalid-model versus invalid-geometry diagnostics and independent revision histories. Images retain image/mip generations, creation tokens, fallback rejection and row-layout validation. Canonical acquisition sharing supports this work; it is no longer the first priority because it removes duplicate code. Separate any newly exposed reentrancy bug fix from extraction.

**Measurement and complete scope.** Accepted/repeated/retired/discarded jobs, extraction/build time, allocations and retained bytes on import/reload workloads. Include runner/policy/result types, ticket state, source authority, cache adapters and tests. No asset-preparation speedup is asserted for TG #37 without evidence connecting it to the stalls.

### 5. Sampling and geometry kernel contracts

**Exact candidate scopes.** Float bilinear sampling in `S/HeightmapStampSampling.cpp:29-56` and `S/TerrainExistenceSampling.cpp:22-37,344-361`; surface decoding/accumulation in `S/SurfaceStampSampling.cpp:65-129,333-428`; BVH traversal in `S/TerrainMeshCutoutData.cpp:211-243` and `S/TerrainMeshCutoutSampling.cpp:117-172`; XY bounds in `S/TerrainMeshHeightStampSampling.cpp:109-137` and `S/TerrainExistenceSampling.cpp:377-418`; mesh-height building in `S/TerrainMeshHeightData.cpp:208-527`; `S/StampPlacement.cpp`, `S/StampMath.h` and their public data/sampling headers. Include `I/TerrainMeshHeightMapping.h` and shader counterparts for mesh mapping changes.

**Architectural change.** Expose a selected kernel's validated inputs, outputs, scratch ownership, numerical guarantees and stage measurements. A small shared interpolation/traversal primitive can provide one implementation to optimize or instrument even if it adds code. Choose the kernel from current profiling or a concrete dependency. Keep separate algorithms separate rather than building a general geometry framework without a measured use.

**Preserve and characterize.** Surface IDs are categorical; preserve per-pixel decoding, transparency/mass normalization and neighbor order. Image/mesh/surface feathering has arithmetic and validation-order differences. BVH parity counts all hits, while distance/self-intersection can stop early; ray half-open ownership and segment tolerances differ. Cutouts validate closed volumes and exact welded positions; mesh height validates tolerant grids and imported diagonals. Raw-span builders retain their own validation after Atom extraction. Logical/render/collision bounds and float/`precise` mesh-gap mapping remain distinct contracts.

Start from reconstruction, mesh-height/cutout, numeric-helper and GPU tests. Add selected edge-size/threshold/rounding cases, multi-level BVH reference traversal or independent geometry fixtures before changes. Measure samples/nodes/triangles visited, allocations, scratch bytes and stage time at representative sizes. Include helpers, adapters, headers, scratch owners and tests. Follow microbenchmarks with the reference route or relevant edit/import workload before claiming user-visible improvement.

## Disposition of the previous inventory

| Previous candidate | Current treatment |
| --- | --- |
| Coordinator normalized-height batch helper | Expanded to rank 1's ownership/execution contract and renderer integration; local extraction alone cannot safely eliminate ordinary queries |
| Model preparation protocol and canonical acquisition | Rank 4, driven by correct lifecycle/result sharing |
| Bilinear sampling, BVH traversal and transformed bounds | Rank 5, selected individually when profiling or an optimization dependency warrants it |
| Duplicate six-scalar/AABB accumulation | Incidental cleanup; not an architecture milestone or TG #37 prerequisite |
| Generic yaw/feather, registration routing and geometry validation | No requirement to merge; retain meaningful numerical, diagnostic and type differences |
| Source-generation design | Explicit readiness decision in rank 4 before cross-role sharing |
| Sector lifetime and before-query ownership proof | Leading gaps added from TG #37, beyond the original small-function deletion inventory |

Completed registration-client, mesh-placement, editor-preview and provider owners remain. Do not repeat their extraction, force typed stores into one map or move code solely to shrink the coordinator. Conversely, code growth is acceptable when it establishes a necessary, testable optimization boundary.

## Measurement and acceptance

Before implementing the leading refactors, record exact Gem/engine/project revisions and build paths, preserve authored content, and use the TG harness (`D:/TG/TGProject/Gem/Tests/profile_terrain_flight.py` and `analyze_terrain_flight.py`). Run stationary, rotation and warmed 512 m translation controls at original quality. Use at least three matched translation routes; report worst frames and >50 ms hitch counts/rates alongside p95/p99. Separate timing runs from intrusive captures and concurrent builds/asset processing. Historical worker scope totals overlap and cannot be summed into frame time.

Make relevant boundaries observable with stable stage names and optional counters, using thread-local/result-owned aggregation where appropriate instead of contended per-sample logging. Separate ordinary/retained sampling, plan resolution, worker preparation/wait, commit/upload and queue/retention costs. These counters are readiness work, not instrumentation claimed to exist already.

An architectural slice is complete when inputs/ownership, failure/cancellation and output/commit contracts are explicit; old behavior is characterized before extraction; the full changed owner/helper/adapter scope is reviewed; applicable runtime/editor, lifecycle, GPU and override checks pass; and the enabled optimization is concrete. Compare the same workload to detect regressions, even when retaining existing execution policy. An architecture refactor can be useful without claiming a speedup.

Subsequent optimization uses the flight report's targets as goals to validate: at least 35% less preparation time on matched 10/20/30-sector batches for duplicate-query work; at most 4 ms per frame of main/render-thread sector commit/upload and no terrain-attributable >50 ms route hitches for scheduling work. Define memory and coverage-latency limits before enabling cross-frame policy. Preserve heights, holes, render/collision independence and generation safety. TG #37 remains open for performance and user-flight acceptance.

This ranking changes documentation/tracking only; no architectural implementation or new performance capture is claimed. The LOC log remains historical accounting with no active reduction target. The GitHub tracking issue carries the architecture goal and staged readiness work.

## Startup and build checkpoint (2026-09-10)

The Editor startup failure was reproduced under the Windows debugger. `TerrainCompositorSystemComponent::Activate` called `FeatureProcessorFactory::RegisterFeatureProcessor` before Atom had created the factory, causing a null-pointer access violation in `Atom_RPI.Public.dll`. Declaring the required `RPISystem` service establishes activation and reverse shutdown order. The fix is committed as `4534411` in the standalone repository and `e2281e2` in TG's compositor submodule; TG commit `715a5ae` records that submodule revision.

Project Manager builds `D:/TG/TGProject/build/windows` in `profile`, with the Editor and gem DLLs under `bin/profile`. Its current source is `D:/TG/TGProject/Gems/terrain-compositor`, whose base is the initial extraction `b3b5519`. Earlier standalone refactoring builds used `D:/wzmono/terrain-compositor` and wrote into the same output tree through different generated projects. A successful incremental build therefore does not identify which checkout supplied every artifact.

The startup fix was verified with the normal CMake build of `TerrainCompositor.Editor` and `TerrainCompositor`, including project dependencies. Both targets passed. The Editor then opened `Levels/DefaultLevel/DefaultLevel.prefab`, Asset Processor became idle, and the user confirmed that the Editor runs. Debugger/build evidence is local under `D:/TG/TGProject/build/editor-launch-diagnosis`. This validates the startup fix on TG's older compositor checkout, not the standalone refactoring code's live performance.

Before the first optimization measurement, align TG with the intended compositor revision, configure and build from that source, verify Editor startup and the relevant lifecycle/GPU checks, then capture the matched original-quality route. Keep the older working TG revision available while establishing the new baseline.
