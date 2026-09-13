# Cached coverage traversal

Coverage uses polytree's immutable forest, cached reverse topological order and
direct child handles. Algo's range-to-sink transform evaluates that order into
caller-owned result storage. Terrain policy supplies claim precedence,
authoritative emptiness, missing/partial/full classification and quadrant masks.
Neither generic library depends on O3DE or scene-polytree's runtime.

## Dependency configuration

`cmake/Dependencies.cmake` resolves `algo::algo` before `polytree::polytree`.
Visible existing targets are reused. Otherwise it uses an explicit source
checkout, an installed config package, then an optional pinned Git fetch.
The pins are polytree **0.2.0**, commit
`6a7e2ee401cb755a40512ab7b4630becf2eb9950`, and algo **0.0.1**, commit
`ea2e45f8c19ebe7deb44c85c91179e12fac57154`. These match scene-polytree 0.4.0's
dependencies; polytree transitively links the same algo target. Direct algo use
is declared as a target dependency too.

Installed-package ranges are `polytree 0.2...<0.3` and `algo 0.0.1...<0.1`.
Existing targets and local overrides must provide the cached evaluation-plan,
immutable storage, child/edge query and `algo::next::transform` APIs at these
revisions. The coverage contract build exercises those APIs; target names alone
do not establish compatibility. CMake 3.24 and C++20 are required.

```text
-DTERRAIN_COMPOSITOR_POLYTREE_SOURCE_DIR=D:/wzmono/polytree
-DTERRAIN_COMPOSITOR_ALGO_SOURCE_DIR=D:/wzmono/algo
-DTERRAIN_COMPOSITOR_FETCH_DEPENDENCIES=OFF
```

For installed libraries, set `CMAKE_PREFIX_PATH` instead of source overrides.
Fetch can be disabled to make missing packages an error. Standard FetchContent
source/cache and disconnected controls remain available. Fallbacks use complete
commit IDs without shallow cloning. A containing project can resolve compatible
targets before either Gem. Imported packages found here are global, so sibling
Gems can reuse them. Existing directory-local imported targets remain owned by
their package scope; config discovery in another scope refers to the same
installed headers and does not compile another library.

The resolver also honors `SCENE_POLYTREE_{POLYTREE,ALGO}_SOURCE_DIR` when terrain
configures first, and rejects differing explicit compositor/scene source paths.
Set matching overrides at project configuration time, before adding either Gem.
Dependency tests and benchmarks are disabled in the resolver's scope, without
overwriting a parent's cache choices. Terrain does not link scene-polytree.

Usage requirements reach `Terrain.Static` (which owns the patched renderer and
coverage construction source), `TerrainCompositor.Static` and their runtime,
editor and test consumers. Only `TerrainSectorCoverage.cpp` enables exception
unwinding for polytree construction and suppresses the dependency's unused
assert-only parameter warning. It is excluded from unity compilation. There
are no global include directories or compiler-option changes.

Both libraries use the MIT license, copyright 2026 Wozzits Engine contributors.
Distributions containing their headers or compiled substantial portions must
include their copyright and permission notices; copies are under
`licenses/`. Existing O3DE license notices and engine hash checks remain intact.

## Cache and policy contract

The workspace belongs to one control owner. It retains ordered `(x, y, lod)`
claim keys, active LOD count, construction scratch, immutable topology, one
coverage value per node and draw capacity. It retains no sector pointers,
publication snapshots, dependency tickets, meshes or GPU resources.

Every evaluation compares the full ordered keys and LOD count. An insertion,
removal, reorder, coordinate change or LOD change rebuilds the plan. This is an
exact comparison, not a hash. `m_generation` advances on construction and is a
diagnostic plan identity; it never authorizes visibility or publication.
`Invalidate()` forces a rebuild even for identical keys. The manager calls it
when sector buffers are cleared for teardown/reconfiguration. Moving a workspace
transfers its storage and invalidates the source. Graph views/handles must not
outlive that workspace generation; no borrowed views are stored separately.

`m_hasData` is read from current claims on every evaluation, so an empty/populated
transition reuses topology but changes policy immediately. Removed eligibility
(including camera changes without grid shifts) changes the input keys. The
existing manager still observes publications and dependency revisions before
forming claims, including a second selection in one frame. Final synchronized
whole-group acceptance and atomic raster/RT replacement are unchanged.

Each unique coordinate at each LOD is a node, including implicit ancestors
through missing intermediate LODs. Signed parent division floors toward negative
infinity. Child coordinate arithmetic uses int64 before checked int32 conversion.
Duplicate claims retain the last supplied index and report each duplicate.
Out-of-range LOD claims remain counted as unrepresentable.

Nodes are inserted in increasing LOD and lexicographic coordinate order; edges
are inserted in quadrant order 0, 1, 2, 3. Polytree's characterized LIFO
topological order reverses to the existing ascending-root, ascending-quadrant
postorder. Children are evaluated before parents, including children of
authoritatively empty nodes. Draws preserve that exact order. Unknown coverage
never becomes an authored hole, and a coarse populated claim still rejects an
unrepresentable partially covered child quadrant.

## Cost and reproducible checks

Let C be claim count and N/E the constructed node/edge counts. Exact key checking
costs O(C); warm evaluation is O(N+E), with no sorting, spatial search, recursion
or heap allocation. Draw capacity reserves C entries on construction, enough for
all populated claims even when previously empty claims become populated.

Cold construction retains the previous sorted spatial-level builder, then adds
the generic topology. Sorting and child searches cost O(sum N_l log N_l), with
the library's construction scratch and compact retained storage on top. The
library currently stable-sorts edges and builds per-node temporary adjacency;
construction is not allocation-free. Changing topology rebuilds that storage.
Levels, claim keys, values and output keep their high-water capacities; replacing
the graph frees its previous compact allocation. Retained graph storage also
includes the library's other cached orders, even though coverage only evaluates
reverse topological order.

The portable suite preserves both the previous map oracle and flat implicit
selector (using standard containers in place of AZStd for portability), compares
9,000 populated/empty selections, and checks exact output
order, signed extremes, duplicate claims, missing LODs, plan invalidation and
move lifetime. A separate allocation-instrumented executable verifies warm
evaluation, including empty/populated transitions, performs no allocations.

```powershell
cmake -S tests/coverage -B build/coverage-contracts -DTERRAIN_COMPOSITOR_POLYTREE_SOURCE_DIR=D:/wzmono/polytree -DTERRAIN_COMPOSITOR_ALGO_SOURCE_DIR=D:/wzmono/algo
cmake --build build/coverage-contracts --config Release
ctest --test-dir build/coverage-contracts -C Release --output-on-failure
build/coverage-contracts/Release/terrain_coverage_tests.exe --benchmark
build/coverage-contracts/Release/terrain_coverage_allocations.exe --benchmark
tools/TestDependencyModes.ps1 -PolytreeSource D:/wzmono/polytree -AlgoSource D:/wzmono/algo -SceneSource D:/wzmono/scene-polytree -Fetch
```

The dependency matrix configures/builds terrain alone and with scene-polytree in
both orders for source, installed-package and real pinned-fetch modes. These are
portable coverage consumers; O3DE runtime/editor/test builds were also validated
with both Gems in TG and in a standalone compositor consumer reusing matching
engine artifacts. TG owns flight measurements, raster/RT integration evidence,
provenance and authored-content verification. Record cold,
unchanged, policy-changing and topology-changing timings separately. A faster
warm traversal does not establish a frame-rate improvement.

On the 2026-09-12 Windows/MSVC 19.51 run, the 700-claim portable benchmark
(median of three processes, 500 calls each, allocation hooks disabled) measured:

| Workload | Previous flat, microseconds | Cached polytree, microseconds |
| --- | ---: | ---: |
| Cold workspace, construction/evaluation/destruction | 134.516 | 230.375 |
| Warm unchanged | 109.425 | 14.231 |
| Warm populated/empty changes | 111.401 | 12.841 |
| Topology changes every call | 110.222 | 220.353 |

Separate allocation instrumentation found zero allocations in both warm paths.
Initial allocation counts were 115/789 and retained payload bytes 34,095/81,126
(flat/polytree). Cold peak payload bytes were 41,718/86,890. Alternating topology
incurred 688 allocations per cached-plan rebuild versus zero in the warmed flat
builder. Counts cover these selectors' ordinary C++ allocation calls; bytes
exclude allocator metadata, stacks, inputs and the rest of the renderer.

The cached representation is retained because reuse pays for its construction
cost in the measured renderer route. In matched four-second TG translation
traces, selection total fell from 24.974 to 15.355 ms and candidate update total
from 51.181 to 40.556 ms. **Tail cost regressed:** maximum selection increased
from 0.225 to 0.410 ms, and maximum candidate update from 0.421 to 0.654 ms.
Cold cost, memory and changing-topology cost remain targets for subsequent work;
the warm microbenchmark improvement must not be substituted for these renderer
measurements. The reference camera flights remained near 60 FPS.

TG's `Gem/Docs/TerrainCoverageTraversalProfiling.md` and accompanying measurement
JSON record the integration settings, revisions, complete timing rows, tests,
authored-file hashes and raw local artifacts. These are consuming-project
evidence, not a dependency of this Gem.
