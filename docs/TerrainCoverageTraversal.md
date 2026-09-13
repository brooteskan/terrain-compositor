# Incremental terrain coverage and renderer changes

Coverage uses polytree's immutable forest, cached reverse topological order,
direct child handles and reusable inclusive ancestor closure. Algo composes
affected-node filtering and terrain-policy evaluation into caller-owned output.
Terrain owns precedence, authoritative emptiness, quadrant masks, resource
identity and publication policy. It does not depend on scene transform runtime.

## Dependencies

Source dependencies are pinned Git submodules: `external/polytree` (0.2.1)
and its nested `external/algo` (0.0.1). The gitlinks, visible with
`git submodule status --recursive`, are the revision authority. The generic
ancestor primitive, construction changes, independent tests and INCREMENTAL.md
contract live upstream in polytree.

Run `git submodule update --init --recursive` before configuring. CMake reads only
these initialized submodules. It performs no FetchContent or installed-package
fallback and exposes no sibling-checkout path overrides. Missing submodules,
checked-out revisions that differ from their gitlinks, and conflicting existing
targets fail configuration. Compatible shared targets must come from an
initialized submodule at the same pinned revision.

The scene-first and terrain-first compositions use matching pins and work in
either order without parent-level dependency bootstrapping. The scene-polytree
submodule under `tests/dependencies/scene-polytree` is test-only: production
terrain does not add it to the build. GoogleTest is also pinned in the upstream
repositories' test submodules. The installed package exports remain available
for downstream binary-package consumers; repository source acquisition is
exclusively through submodules.

CMake 3.24 and target-scoped C++20 requirements remain. The coverage source is
isolated from unity compilation and enables exception cleanup only for that
translation unit. Engine hash checks, compiler/include scope and MIT notices
in licenses/ remain. No scene-polytree runtime or global compiler flags are added.

## Identity and topology

One control owner owns the scratch. Inputs are ordered logical slots: committed
signed coordinates, LOD, destination, resource version, known flag and has-data
flag. Renderer inputs include every bounded slot, including camera-ineligible
or invalid resources, at committed rather than pending requested locations.
Unknown is never authoritative emptiness; a known claim without data is.

Topology keys include all ordered coordinates, LODs, destination tokens and LOD
count. Known/has-data/resource-version changes reuse topology. Slot count,
coordinates, identity ordering, LOD count or explicit invalidation rebuild it.
Clear/reconfigure invalidates even with identical coordinates. Committed grid
movement and teleports rebuild; camera eligibility changes alone do not.

Destination tokens use the existing leased destination identity. Resource
versions are monotonic owner tokens assigned to every accepted bundle and
draw-packet replacement, including identical coordinates and quadrant masks.
They are separate from topology handles and confer no publication authority.
Generations are scoped to an owner and assumed not to wrap in its lifetime;
a fresh owner requires a fresh consumer baseline.

Each coordinate retains its ordered duplicate chain. The last *known* supplied
claim wins. An unknown later slot does not hide an earlier known claim.
Duplicate diagnostics count only extra known claims. Invalid LOD claims count
as errors only while known. Synthetic ancestors stay unknown without coverage.
Signed doubling uses checked 64-bit arithmetic; negative parenting floors division.

Handles belong to one plan generation. Scratch owns no sector, source,
publication, dependency, mesh or GPU resource pointers. Returned references and
spans borrow scratch until evaluation, invalidation, move or destruction.
Move invalidates the source workspace.

## Evaluation and stopping proof

Every evaluation compares the complete current input snapshot, including a second
call in the same frame. This establishes a complete directly changed-node set.
An incomplete upstream event list never suppresses immediate input observation.

Polytree computes the inclusive union of changed nodes and ancestors in cached
children-before-parents order. Algo filters it to directly marked nodes or nodes
marked by a changed child aggregate. Terrain evaluates each node, emits its local
draw change, and marks its parent only if the aggregate classification changes.

A parent depends only on its own current winning claim and children's
Missing/Partial/Full classifications. Child resource tokens, draw masks,
duplicate diagnostics and local partial counts do not otherwise enter parent
policy. Independently changed parents are marked directly. Therefore a local
draw change is emitted *before* stopping on an unchanged aggregate: turning a fine
draw into authoritative emptiness cannot silently lose that removal.

forceFull retains full evaluation as oracle and fallback. Topology changes and
missed consumer baselines use full reset snapshots. When every input slot changed,
the evaluator also uses the cached full order and a reset snapshot instead of
constructing and sorting a dense ancestor union. Independent preserved map
and flat/cell oracles characterize the policy. Full ordered draw-vector
materialization is lazy; production batch consumption does not request it.

Let C be slots, N nodes, A the ancestor union and V actually evaluated nodes.
Warm unchanged evaluation costs O(C). Sparse evaluation costs
O(C + A log A + child edges of V + duplicate-chain visits). Full evaluation is
O(C + N + E). Optional changed-output materialization is O(N).
Dense changes are not claimed to have sparse cost.

Scratch owns topology, cached aggregates, duplicate chains, dirty flags, closure
workspace, output and change capacity. Construction reserves the possible draw
and change count for that topology. Warm unchanged, sparse and dense policy or
resource updates allocate nothing after construction; topology rebuilds can
allocate. Retained payload reports owned vector capacities and compact topology,
excluding stacks, allocator metadata, input vectors, renderer and driver memory.
Allocation measurement is separated from timing.

Polytree construction uses contiguous temporary CSR adjacency and skips stable
sorting of already parent-grouped edges. Terrain reserves node/edge builder
inputs. Cached plan orders and level construction scratch remain. Mutable
authoring/freeze would add maps and stable-ID storage to this bounded forest;
it was not introduced without evidence that it benefits group coordinate changes.

## Batch and renderer application

A borrowed batch identifies baseline/result/plan generations, node count and reset
status. Changes identify an ordered position plus before/after draws: claim,
quadrant mask, destination and resource version. Zero masks mean absence.
A reset lists all current draws and replaces the complete consumer selection.

Validate before mutation. Ordinary batches require the exact baseline and plan.
Repeated/older results are rejected. Resets can skip a missed baseline but cannot
roll back an accepted result. The renderer requests a fresh full reset after a
missed evaluation or RT enablement change. Application is synchronous in the
owner; copying token storage does not authorize delayed publication against a
later producer state or a new owner.

Candidates and RT entries remain ordered contiguous vectors. Binary lookup
removes affected order ranges; changes insert current entries directly. Unchanged
selection performs no edits and no whole-list sorting or reconstruction.
Insert/erase can still shift entries; diagnostics report that cost. Common RT
quadrants are retained only when before/after destination and resource version
match. Draw submission still visits visible candidates.

The established synchronized acceptance path withdraws old raster pointers and
visible RT meshes before swapping their resource owners. Retirement does the
same. A subsequent removal token cannot dereference a replaced resource: the
old visibility was removed while that owner lived. Camera-only changes operate
while current committed resources remain live. Full resets rebuild bookkeeping
from a complete snapshot, reconcile desired masks against current resource owners,
and preserve unchanged visible RT groups. A plan/order change must not cause
unchanged meshes to be removed/re-added or their acceleration structures rebuilt.

RefreshCommittedCoverage still observes publication and dependency authority on
every call. Plans, input equality and batches cannot publish GPU resources,
admit partial atomic groups, defer invalidation or refresh stale source tickets.

## Diagnostics and validation

Statistics report rebuild reason, hits/rebuilds, directly changed claims,
evaluated nodes/edges, output changes, known/resource/has-data transitions and
retained selector payload. Renderer timing logs add plan/result/reset identity,
raster edits/shifts and actual RT API edits. Camera eligibility is counted while
resources remain available; separate availability transitions record invalidation,
retirement and renewed availability. Transition counters apply to matching plans;
rebuilds conservatively count all claims in the new plan as directly changed and
report the topology cause. Publication/dependency checks remain separately traced
by RefreshCommittedCoverage.

Portable tests include 9,000 preserved selections and 12,000 sequential
full/incremental/map/flat/delta comparisons, signed extremes, missing LODs,
duplicates, unknown/empty/populated transitions, sparse/dense changes, resource
replacement, reconfiguration, destination changes, missed/obsolete batches,
move/reset lifetime and allocation checks. Strict RT mocks exercise production
acceptance, identical-mask replacement, unchanged work, camera-only eligibility
consumer resynchronization, topology resets and retention of common RT quadrants.

TG owns route/build/test evidence in Gem/Docs. Raw local artifacts are under
build/issue10. Traversal timing is not an FPS claim. See
[CoverageAdoptionFollowups.md](CoverageAdoptionFollowups.md) for bounded next work.

Build the portable targets with `cmake -S tests/coverage -B build/coverage`, then
`cmake --build build/coverage --config Release` and
`ctest --test-dir build/coverage -C Release --output-on-failure`. `terrain_coverage_tests --benchmark` measures
the preserved flat and current selector; `--incremental-benchmark` compares full
and sparse batch evaluation and bounded/eligible-only topology. Run the matching
`terrain_coverage_allocations` options separately for allocation measurements;
their instrumented timings are not performance evidence. The submodule composition matrix
is `tools/TestDependencyModes.ps1`. Engine validation uses the maintained
`integrations/o3de/Code/Tests/EngineOverridesTests.cmake` script and runtime/editor
test targets, including GPU and sector-lifetime tests.
