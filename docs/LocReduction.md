# LOC reduction

This report addresses [issue #1](https://github.com/brooteskan/terrain-compositor/issues/1).
The session starts at `52cbfec5be2aa41df6f4507a68fc3c0ee4971e96` (post-cleanup).
The initial extraction is `b3b5519788cc1e003e42d45e728124d7ae4b497d`.

## Reproducible measurement

From the standalone repository root, with Python 3 (this workstation used
`C:/Users/david/.o3de/Python/venv/16f8cfd2/Scripts/python.exe`):

```powershell
python tools/MeasureLoc.py --revision b3b5519
python tools/MeasureLoc.py --revision 52cbfec
python tools/MeasureLoc.py --worktree
```

`--json` includes every counted file and its category. Committed measurements
read Git blobs, without checkout filters or working-copy edits. Worktree mode
includes tracked and unignored untracked files, including this report, the
measurement script, new helpers, and all tests. Ignored build output is excluded;
maintained inputs to generated engine sources are counted. NUL-containing files
are reported as excluded binary files (none in these trees).

The unit is a physical line, including blank lines and comments. An unterminated
last line counts. Categories are exclusive: documentation includes Markdown
anywhere and all of `docs/`; engine patches/overrides includes both old copied
sources and current patch inputs; tests includes test CMake files; public headers
is `Code/Include`; production C++ is `.cpp`, `.h`, and `.inl` in `Code/Source`,
including editor code and internal helpers. Remaining assets, licenses/notices,
and build/tooling have separate categories. First-party C++ is production C++
plus public headers. Maintained text excludes only LICENSE and NOTICE from total
repository text.

| Category | Initial extraction | Post-cleanup | This slice |
| --- | ---: | ---: | ---: |
| Production C++ | 14,391 | 13,993 | 13,979 |
| Public headers | 3,016 | 3,016 | 3,016 |
| Tests | 2,853 | 3,704 | 3,866 |
| Shaders/assets | 966 | 966 | 966 |
| Engine patches/overrides | 3,570 | 939 | 939 |
| Build/tooling | 390 | 426 | 520 |
| Documentation | 3,014 | 3,014 | 3,180 |
| License/notice | 28 | 28 | 28 |
| Total repository text | 28,228 | 26,086 | 26,494 |
| Maintained text | 28,200 | 26,058 | 26,466 |
| First-party C++ | 17,407 | 17,009 | 16,995 |

The issue's approximate historical totals (28,979 original and 26,081 after
cleanup) do not reproduce from these two committed trees with this scope. They
remain historical estimates; the table supplies comparable, reproducible bases.
First-party C++ falls by 14 lines (0.08%) against post-cleanup and 412 lines
(2.37%) against initial extraction. This is a foundation for later reductions,
not evidence that a safe 50% reduction is achievable.

## Focused duplicate-definition and ownership inventory

Paths below are relative to `integrations/o3de/Code/Source`. Sizes and coordinator
line numbers refer to the post-cleanup commit. They measure candidate files or
regions, not promised removable lines. The inventory was checked with:

```powershell
rg -n '^    .*::(RegisterStamp|RegisterMeshCutout|RegisterMeshHeightStamp|PublishStamps|OnCompositionAvailable|OnCompositionUnavailable|DisconnectTarget|OnSystemTick|StartProvider|StopProvider|Activate|Deactivate|BindPlacementEntity|UnbindPlacementEntity)\(' integrations/o3de/Code/Source
```

| Repeated concept | Locations and baseline size | Ownership boundary / next treatment |
| --- | --- | --- |
| Resolve revisions, classify, insert, fan out | `Components/TerrainCompositionGradientComponent.cpp:1327-1496`; coordinator total 2,206 lines | Extracted here into one value-only algorithm with explicit role metadata. Admission and publication remain with the coordinator. |
| Three registration-client protocols | `HeightmapStampRegistration.cpp` (403), `TerrainMeshCutoutRegistration.cpp` (200), `TerrainMeshHeightStampRegistration.cpp` (328) | Duplicate session/lease handshake, retarget, replay and disconnect definitions. Image has five subscriptions; each mesh role has distinct preparation/status. Share protocol ownership after revision indexing. |
| Query-state preparation, diagnostics and invalidation | Coordinator `PublishStamps`, lines 1558-2126 | Five diagnostic histories, three preparation traversals, old/current footprint bookkeeping. Keep geometry rules and separate render/query gap footprints explicit; consider typed prepared records after indexing. |
| Runtime mesh placement and visibility lifecycle | `Components/TerrainMeshCutoutComponent.cpp` (364), `Components/TerrainMeshHeightStampComponent.cpp` (396) | Repeated deferred placement binding, transform/model notifications and visibility restoration. Existing helpers cover operations; a shared owner could remove lifecycle state. |
| Editor stamp lifecycle | `Editor/EditorHeightmapStampComponent.cpp` (237), `Editor/EditorTerrainMeshCutoutComponent.cpp` (221), `Editor/EditorTerrainMeshHeightStampComponent.cpp` (265) | Similar activate/deactivate, export/configuration and preview polling. Existing preview helper is shared; reflection and debug geometry differ. |
| Height/surface providers | `Components/TerrainCompositionHeightProviderComponent.cpp` (258), `Components/TerrainCompositionSurfaceProviderComponent.cpp` (217) | Parallel start/stop, restart, configuration and status methods around shared `TerrainCompositionProviderBinding`. Query and notification buses differ. |
| Model preparation publication | `TerrainMeshCutoutDataCache.cpp`, `TerrainMeshHeightDataCache.cpp`, `ModelAssetSource.h` | Lifecycle helper already exists. Cutout lacks height-cache preparation-ticket retirement behavior; three pre-existing tests expose it. Do not fold this behavioral fix into a mechanical LOC change. |

These are near-duplicate concepts and methods, not necessarily byte-identical
functions. One-line bus/serialization adapters are not counted as standalone
architecture opportunities. Large sampling/geometry files have distinct numerical
rules; splitting them alone would redistribute LOC.

## Implemented slice

`CompositionRegistrationState.h` owns the deterministic transformation of an
admitted registration and its peers. A role table replaces positional dirty-bit
selection. A single algorithm reconciles snapshots before classification, stores
the new record, and propagates strictly newer snapshots to matching asset roles
while accumulating their dirty bits. It has no cache access, bus calls, retained
component references, or publication side effects.

The coordinator retains admission/session checks, role-specific change
classification, geometry preparation, diagnostics, and atomic publication. It
shrinks from 2,206 to 2,097 lines; the 95-line helper is included in the net
14-line production reduction. Two duplicated implementations of the reconciliation
sequence are removed. Runtime scans and the three registration maps remain;
this slice does not claim to remove their runtime cost or ownership state.

Ten characterization cases were added and run against the original implementation
before extraction: equal revision ties, mesh versus image unassigned-ID policy,
no fan-out from unassigned IDs, last-claim revision-history retirement, and a new
record sharing one asset across image roles. Two direct transformation tests
cover reconciliation-before-classification, caller snapshot preservation, pending
dirty-bit accumulation, every source/target role, and unrelated/equal-revision
claims. Existing tests continue to cover failure/recovery and retained image data.

## Verification

The existing TG build projects compile files directly from
`D:/wzmono/terrain-compositor/integrations/o3de`; no sources are copied into TG.
Verification reuses those generated VC projects and installed dependency outputs.
TG's current CMake configuration selects its newer submodule checkout, so the
commands below disable automatic custom-build regeneration. A fresh consumer
configuration must instead point its external Gem path at the standalone checkout.
The baseline binary ran 132 tests: 129 passed, with these existing failures:

- `TerrainModelCacheLifecycleTests/0.NewerAcceptedPreparationSuppressesAnOlderQueuedCompletion`
- `TerrainModelCacheLifecycleTests/0.FailureRetiresPreparationAlreadyWaitingToPublish`
- `TerrainModelCacheLifecycleTests/0.FailureDuringPreparationCannotBeOverwrittenByItsCompletion`

All three are the cutout specialization. One existing test is disabled. The
characterization run passed all 37 registration/image tests before production
changes. Runtime, editor and tests built successfully. The final full suite ran
144 tests: 141 passed and exactly the three baseline cases failed. All 12 added
cases passed; no existing cases were removed or newly disabled. Shuffled testing
ran 61 cases per iteration for 100 iterations (seeds 173-272): 5,800 passes and
300 known failures, with no new failures. Failure-name sets were compared against
the baseline XML and all shuffled output. D3D11 verification compared 142,560
boundary classifications with zero mismatches.
Engine override verification passed all valid, repeat, mismatched-upstream, and
mismatched-output cases. Public headers, component/configuration reflection,
UUIDs, serialized versions, shaders and engine patches are unchanged.

Build/test commands used on this workstation (logs/XML are in ignored
`build/loc-session`):

```powershell
$msbuild = 'C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe'
$projects = 'D:/TG/TGProject/build/windows/External/o3de-193b5b6a'
foreach ($target in 'TerrainCompositor.Static','TerrainCompositor','TerrainCompositor.Editor','TerrainCompositor.Tests') {
    & $msbuild "$projects/$target.vcxproj" /p:Configuration=profile /p:Platform=x64 /p:BuildProjectReferences=false /p:CustomBuildToolBeforeTargets= /p:CustomBuildToolAfterTargets= /m:4 /nologo /v:minimal
    if ($LASTEXITCODE) { throw "Build failed: $target" }
}
$bin = 'D:/TG/TGProject/build/windows/bin/profile'
& "$bin/AzTestRunner.exe" "$bin/TerrainCompositor.Tests.dll" AzRunUnitTests
& "$bin/AzTestRunner.exe" "$bin/TerrainCompositor.Tests.dll" AzRunUnitTests '--gtest_filter=*LifecycleTests*:*ImageRegistrationTests*:*CompositionRegistrationStateTests*' --gtest_shuffle --gtest_random_seed=173 --gtest_repeat=100
cmake -DTC_ENGINE_TERRAIN_ROOT=D:/code/o3de-development/Gems/Terrain/Code/Source -DTC_TEST_ROOT=D:/wzmono/terrain-compositor/build/loc-session/engine-overrides -P integrations/o3de/Code/Tests/EngineOverridesTests.cmake
```

## Next step proposed after the first slice

Build a session-owned **image asset revision and dependent-role index** using
the newly characterized transformation as its behavioral contract. The five image
roles currently perform up to 25N comparisons to reconcile an input and another
25N to propagate it across N registrations. Index by canonical asset ID and
record each dependent entity/role so a publication visits only its claims.

Preserve equal-revision ties, last-claim removal, session reset, asset retargeting,
unassigned-ID policy, dirty-bit classification and immutable publication. Add
retarget/removal/index-consistency tests before replacing the scan. Measure both
maintained LOC and actual visited claims to show that state/traversals disappeared.

Then extend the ownership model to mesh roles. Cutout and mesh-height currently
use independent `s_nextCutoutRevision` and `s_nextMeshHeightRevision` counters;
their numbers are not a shared source-asset revision. Establish a source-generation
contract before making cross-role claims authoritative in one service, and retain
separate prepared-geometry results and failure semantics. The documented cutout
preparation-retirement failures should be fixed in a focused behavioral slice.

## Registration-client consolidation (2026-09-10)

Baseline: `f2e888c`. Measure with `python tools/MeasureLoc.py --revision f2e888c`
and `python tools/MeasureLoc.py --worktree`; add `--json` for every counted file.

| Scope | Before | After | Net change |
| --- | ---: | ---: | ---: |
| Three registration implementations | 931 | 472 | -459 |
| Their headers plus both new internal helpers | 180 | 366 | +186 |
| First-party C++ | 16,995 | 16,722 | **-273 (1.61%)** |
| Tests | 3,866 | 4,050 | +184 |
| Total repository text | 26,494 | 26,443 | -51 |

`Internal/RegistrationClient.h` now owns session/lease admission requests,
context and target changes, replay, tick retries, and deactivation for all three
clients. `Internal/AssetSubscription.h` owns each handle, event connection,
selected ID and callback generation, including all five image subscriptions.
The clients retain asset selection, preparation diagnostics and image warning/retry
policies. Public declaration blocks and status-message implementations are unchanged;
private C++ layouts changed and consumers must rebuild. Reflected schemas, UUIDs,
registration data, preparation, publication, shaders and engine patches are unchanged.

Verification: 49 characterization tests passed before replacing the clients.
All four targets built with the standalone-source commands above. The full suite
ran 156 cases: 153 passed, with exactly the three previously listed cutout-cache
failures; one existing disabled test remains. All 12 added cases passed. Shuffled
lifecycle/state verification ran 73 cases for 100 iterations (seeds 173-272):
7,000 passes and 300 known failures, with the failure-name set matching baseline.
Tests exercise both activation orders, reactivation, late context, target clearing,
destruction, all seven asset-role subscriptions, cache restart and queued work after
asset retargeting/deactivation. D3D11 matched all 142,560 classifications; engine
override verification passed. Logs/XML are in ignored `build/registration-client-session`.

This slice advances the LOC goal by consolidating clients ahead of asset indexing.
First-party C++ is now 3.94% below the original extraction; a 50% reduction is not achieved.

## Publication bookkeeping consolidation (2026-09-10)

Baseline is the preceding, uncommitted registration-client worktree; its per-file
report is saved in `D:/TG/TGProject/build/tc-publication-session/loc-before.json`.
The estimate was roughly 150 net C++ lines; the measured reduction is 110.

| Scope | Before | After | Change |
| --- | ---: | ---: | ---: |
| First-party C++ | 16,722 | 16,612 | -110 |
| Repository text, including tests and this report | 26,443 | 26,486 | +43 |

The coordinator drops from 2,097 to 1,940 lines; the new footprint helper adds 46
and a test friend adds one. One diagnostic updater replaces five rebuilt maps;
histories still retire with their claims. Both publication states use the same
footprint derivation, retaining candidate insertion order, distinct cutout/render/
collision bounds and old/new invalidation order. Preparation rules stay explicit.
Candidate footprint collection adds a linear pass over prepared contributions;
no performance improvement is claimed. Public signatures and reflected schemas are unchanged.

All four targets built. Before replacement, 53 characterization cases passed.
The final suite ran 161 cases: 158 passed and the same three cutout-cache cases
failed; the existing disabled test remains. All five added cases passed. Shuffled
verification ran 78 cases for 100 iterations (seeds 173-272): 7,500 passes and 300
known failures, with baseline/full/shuffled failure sets equal. D3D11 matched all
142,560 classifications and engine override verification passed. Logs, XML and
the final `MeasureLoc.py --worktree --json` report are in the same session directory.

## Mesh placement and visibility lifecycle consolidation (2026-09-10)

Baseline: `db4c55b`. The selected slice replaces the duplicate state machines in
`TerrainMeshCutoutComponent` and `TerrainMeshHeightStampComponent` with
`Internal/MeshPlacementLifecycle.h` and `Source/MeshPlacementLifecycle.cpp`.
The design estimate was 200-300 net C++ lines; the measured reduction is **292**.

| Scope | Before | After | Change |
| --- | ---: | ---: | ---: |
| Two component implementations | 760 | 307 | -453 |
| Two component headers | 182 | 99 | -83 |
| New lifecycle header and implementation | 0 | 244 | +244 |
| **Complete changed C++ scope** | **942** | **650** | **-292** |
| All production C++ | 13,409 | 13,106 | -303 |
| All include headers | 3,203 | 3,214 | +11 |
| **First-party C++** | **16,612** | **16,320** | **-292 (1.76%)** |
| Tests, including the file list | 4,175 | 4,615 | +440 |
| Build/tooling | 523 | 525 | +2 |
| Documentation | 3,243 | 3,328 | +85 |
| **Total repository text** | **26,486** | **26,721** | **+235** |

The existing `MeshPlacementHelpers.h` remains unchanged and is counted in the
repository totals. First-party C++ is now 1,087 lines (6.24%) below the original
extraction and 689 lines (4.05%) below post-cleanup. A safe 50% reduction remains
unproven. Added characterization tests increase total repository text; the C++
reduction is not a claim of whole-repository reduction.

The shared owner now holds placement binding, transform/model/scale and editor
identity subscriptions, the two retry counters, per-activation callback tokens,
and saved source-mesh visibility. It also derives the common placement/identity
inputs for the existing typed registration clients. Components retain control
thread checks, their configurations, reflection, services, status messages, and
registration clients. The height-stamp request bus and diagnostic snapshot stay
explicit in its component. Geometry preparation, cache behavior, ordering rules,
publication, shaders, and engine patches are unchanged. No performance claim is
made; the goal is one owner for the duplicated transitions.

Characterization covers runtime/editor activation, owner and descendant models,
late children and model handlers, ambiguity and lease replacement, reparenting,
transform/scale changes, asset reconfiguration, worker notifications, queued work
after stop/reactivation/destruction, initially hidden meshes, and editor identity
resolution without mutating configuration. All **28 cases passed against the
original implementations** before replacement. The existing distinction between
hierarchy events (placement retries) and model events (placement and visibility
retries) is characterized and retained, including after the retry window expires.

Public component method declarations, reflection bodies, service declarations,
and status-message literals were compared to `db4c55b` and match. Component UUIDs
and serialized configuration fields/versions are unchanged. Private C++ layouts
changed when subscriptions and state moved into the owner; consumers must rebuild.

All four targets built successfully: `TerrainCompositor.Static`,
`TerrainCompositor`, `TerrainCompositor.Editor`, and `TerrainCompositor.Tests`.
The final full suite ran **189 cases: 186 passed and the same three known
cutout-cache cases failed**. All 28 new cases passed after replacement; no tests
were removed or newly disabled, and the one existing disabled test remains.
Shuffled verification ran **100 iterations of 106 cases**, seeds 173-272:
10,300 passes and 300 occurrences of those same three failures. Baseline,
full-suite, and every shuffled iteration have the same failure-name set.
D3D11 hardware verification matched all 142,560 boundary classifications.
Engine override generation/rejection checks and `git diff --check` passed.

Verification records are under `D:/TG/TGProject/build/tc-mesh-lifecycle-session`.
The build uses the standalone sources and existing generated VC projects as
described above. The maintained CMake file lists include both new source files;
the reused generated unity inputs additionally include them directly, without
copying sources into TG. A fresh consumer configuration must still select this
standalone Gem checkout. Reproduce measurement with:

```powershell
python tools/MeasureLoc.py --revision db4c55b --json
python tools/MeasureLoc.py --worktree --json
```

The final shuffled filter includes the earlier publication footprint case:

```powershell
& "$bin/AzTestRunner.exe" "$bin/TerrainCompositor.Tests.dll" AzRunUnitTests '--gtest_filter=*LifecycleTests*:*ImageRegistrationTests*:*CompositionRegistrationStateTests*:*PublicationFootprintsTests*' --gtest_shuffle --gtest_random_seed=173 --gtest_repeat=100
```

Next: consolidate editor/provider lifecycle ownership where measurement supports
a net reduction. The historical asset-index proposal above remains deferred until
it demonstrates net code reduction or a separate traversal-performance benefit.
Keep the three known cutout preparation-retirement failures in a focused fix.

## Editor preview lifecycle consolidation (2026-09-10)

Baseline: `6ed0b2e`. The initial estimate was 25-50 net C++ lines for the three
stamp editors. Inspection found that the existing status helper also serves the
composition, height-provider and surface-provider editors. Including all six
allows the helper to become the single owner of runtime previews, cached status,
the polling clock and tick subscriptions. The complete reduction is **52 lines**.

| Scope | Before | After | Change |
| --- | ---: | ---: | ---: |
| Six editor implementations | 1,172 | 1,114 | -58 |
| Six editor headers, including test friends | 288 | 267 | -21 |
| Shared preview helper | 66 | 93 | +27 |
| **Complete changed C++ scope** | **1,526** | **1,474** | **-52** |
| All production C++ | 13,106 | 13,054 | -52 |
| All include headers | 3,214 | 3,214 | 0 |
| **First-party C++** | **16,320** | **16,268** | **-52 (0.32%)** |
| Tests, including the new file list | 4,615 | 5,037 | +422 |
| Build/tooling | 525 | 535 | +10 |
| Documentation | 3,328 | 3,400 | +72 |
| **Total repository text** | **26,721** | **27,173** | **+452** |

`EditorPreviewStatus.h` now defines `EditorPreview`, replacing its free functions.
The owner creates the runtime component during activation, refreshes it in place,
polls selected entities every 0.25 seconds and releases both the tick subscription
and runtime preview on teardown. A preparation callback keeps composition quality
baseline capture before runtime activation. The editor adapters retain serialized
configuration, reflection, service declarations, export rules and viewport drawing.

All **46 characterization cases passed against the original code** and the same
46 passed after replacement. Coverage includes worker-thread construction/config
loading without preview creation, selected-only polling and exact refresh timing,
change-only property notifications, immediate configuration refresh, repeated
teardown, reactivation, queued ticks after destruction, inactive status restoration,
runtime export, baked identity guards, and composition quality baseline restoration.

Public declaration blocks, reflection, services, configuration read/write, export
and underlying-type methods match the baseline. Viewport drawing matches after
substituting the cached-status accessor. Component UUIDs and serialized fields and
versions are unchanged. Private editor layouts changed; editor consumers must
rebuild. Runtime sources, include headers, shaders and engine overrides are unchanged.

All five targets built: `TerrainCompositor.Static`, `TerrainCompositor`,
`TerrainCompositor.Editor`, `TerrainCompositor.Tests`, and the new
`TerrainCompositor.Editor.Tests`. Editor shuffled verification passed **4,600 cases**
over 100 iterations, seeds 173-272. Runtime verification ran **189 cases: 186 passed
and the same three known cutout-cache cases failed**; the existing disabled case
remains. Its 100 shuffled iterations ran 106 cases each: 10,300 passes and 300
occurrences of those same failures. Failure sets match baseline and every iteration.
D3D11 matched all 142,560 classifications. Engine override checks and
`git diff --check` passed. No live editor scene smoke test was performed.

Records are in `D:/TG/TGProject/build/tc-editor-preview-session`, including baseline
and final XML, shuffled logs, compatibility checks and per-file LOC reports.
The new host-tools test target compiles the six actual editor implementations.
Verification reused generated VC projects as described above; its test project
uses the existing editor/test SDK and dependency settings and references standalone
sources directly. Fresh CMake generation was not rerun against TG's submodule path;
a fresh consumer must select this standalone Gem checkout.

```powershell
python tools/MeasureLoc.py --revision 6ed0b2e --json
python tools/MeasureLoc.py --worktree --json
& "$bin/AzTestRunner.exe" "$bin/TerrainCompositor.Editor.Tests.dll" AzRunUnitTests
& "$bin/AzTestRunner.exe" "$bin/TerrainCompositor.Editor.Tests.dll" AzRunUnitTests --gtest_shuffle --gtest_random_seed=173 --gtest_repeat=100
```

First-party C++ is now 1,139 lines (6.54%) below the initial extraction and 741
lines (4.36%) below post-cleanup. Tests and documentation increase total repository
text. This slice consolidates editor preview ownership; runtime provider lifecycle
consolidation remains a separate candidate that needs its own net-LOC estimate.

## Runtime provider lifecycle consolidation (2026-09-10)

Baseline: `e5ad965`. `Internal/ProviderLifecycle.h` now owns start/stop/restart,
context-change polling and the system-tick subscription, configuration retargeting,
common status checks and area invalidation for the height and surface providers.
This slice is accepted for removing duplicate lifecycle ownership; its net LOC
reduction is small. All helpers and the two new test-friend declarations are counted.

| Changed C++ scope | Before | After | Change |
| --- | ---: | ---: | ---: |
| Two component implementations | 475 | 359 | -116 |
| Two component headers | 147 | 146 | -1 |
| New lifecycle owner | 0 | 108 | +108 |
| Existing binding header/implementation | 83 | 83 | 0 |
| **Complete scope** | **705** | **696** | **-9** |

The owner reuses `TerrainCompositionProviderBinding` and its control-thread and
address rules. Both components lose their `StartProvider`, `StopProvider`,
`RestartProvider`, `RefreshArea` and `OnSystemTick` implementations. The public
runtime/editor entry points delegate to the owner. The components retain their
serialized configuration, reflection, service declarations, distinct query-bus
connections and query methods. Height alone retains dependency/terrain notifications,
its height-bounds mutex and cache, clamping, and the final cache reset after stop.

Stopping first marks the binding inactive and drains the area query bus, then
disconnects notifications/ticks, invalidates the old region, clears routing and
resets height bounds. Starting binds routing before connecting the role-specific
buses and refreshing the region. Changed targets and context ownership use the
same restart path; unchanged targets do not restart. Configuration can still be
loaded on a worker before activation establishes control-thread ownership.

The public component declarations, configuration/reflection bodies, UUIDs,
serialized fields/versions, service contracts and status strings match the baseline.
Query and notification implementations match after replacing calls to the shared
refresh method. The existing binding API and implementation are unchanged. Private
provider C++ layouts change because tick ownership moves into the new member;
consumers must rebuild. No performance improvement is claimed.

Added 34 characterization cases across both providers. They passed against the
original implementations before replacement. Coverage includes runtime/editor
activation, repeated start/stop, invalid and retargeted configuration, late/changed/
lost contexts, rejected worker calls and worker construction, scalar/batched routing,
height chunk boundaries, queued ticks after destruction, refresh ordering/masks,
height settings and dependency notifications, and queries in flight during stop,
retargeting, region changes and context changes. Concurrent tests hold an actual
shared-dispatch area query while the control thread changes lifecycle state and
verify completion waits for the query and every height chunk retains its old route.

Measurements and validation artifacts are under
`D:/TG/TGProject/build/tc-provider-lifecycle-session`. As in the previous slices,
builds reuse generated VC projects/dependencies and compile standalone sources
directly. The maintained CMake lists include the new header and test implementation;
the reused test unity input also references the standalone test source. Fresh CMake
generation and a live editor scene smoke test were not performed.

```powershell
python tools/MeasureLoc.py --revision e5ad965 --json
python tools/MeasureLoc.py --worktree --json
& "$bin/AzTestRunner.exe" "$bin/TerrainCompositor.Tests.dll" AzRunUnitTests '--gtest_filter=*TerrainProviderLifecycleTests*:*TerrainHeightProviderNotificationsTests*'
& "$bin/AzTestRunner.exe" "$bin/TerrainCompositor.Tests.dll" AzRunUnitTests '--gtest_filter=*LifecycleTests*:*ImageRegistrationTests*:*CompositionRegistrationStateTests*:*PublicationFootprintsTests*:*TerrainHeightProviderNotificationsTests*' --gtest_shuffle --gtest_random_seed=173 --gtest_repeat=100
```

All five targets built: `TerrainCompositor.Static`, `TerrainCompositor`,
`TerrainCompositor.Editor`, `TerrainCompositor.Tests`, and
`TerrainCompositor.Editor.Tests`. All 34 provider cases passed unchanged after
replacement. The full runtime suite ran **223 cases: 220 passed and the same
three known cutout-cache cases failed**. The 46 editor cases passed. No tests
were removed or newly disabled; the one existing disabled case remains.

D3D11 hardware matched all 142,560 classifications. Engine override generation
and rejection checks and the public-interface/serialization comparison passed.

Shuffled runtime verification ran 140 cases for 100 iterations, seeds 173-272:
13,700 passes and 300 occurrences of the same three known failures. Failure sets
match baseline and every iteration. The 100 shuffled editor iterations passed
all 4,600 cases. `git diff --check` passed.

| Repository category | Before `e5ad965` | After | Change |
| --- | ---: | ---: | ---: |
| Production C++ | 13,054 | 12,938 | -116 |
| Include headers (including the new owner) | 3,214 | 3,321 | +107 |
| **First-party C++** | **16,268** | **16,259** | **-9** |
| Tests, including file lists | 5,037 | 5,554 | +517 |
| Shaders/assets | 966 | 966 | 0 |
| Engine patches/overrides | 939 | 939 | 0 |
| Build/tooling | 535 | 536 | +1 |
| Documentation | 3,400 | 3,498 | +98 |
| License/notice | 28 | 28 | 0 |
| **Total repository text** | **27,173** | **27,780** | **+607** |

Cumulative first-party C++ reduction is **1,148 lines (6.60%)** from the initial
extraction and **750 lines (4.41%)** from post-cleanup. Tests and documentation
grow the repository. A safe 50% reduction ceiling is still unproven. The remaining
coordinator/asset-index work should be assessed for clearer ownership and fewer
traversals as well as measured LOC. Image indexing must preserve revision ties,
last-claim retirement and dirty-bit rules; cross-mesh authority still requires a
shared source-generation contract because its two revision counters are independent.

## Indexed image revision ownership (2026-09-10)

Baseline: `060931b` (`Consolidate runtime provider lifecycle ownership`). The next
architectural slice gives `Internal::ImageRegistrationState` ownership of the image
registration records, each canonical asset's latest revision, and its dependent
entity/role claims. The coordinator exposes only a const record view to admission,
diagnostics and publication; insertion, retargeting, removal and session clearing
maintain the index together. The five image roles share this authority. Cutout and
mesh-height registration state and their independent revision counters are unchanged.

An incoming image role looks up the latest snapshot by canonical snapshot asset ID.
Publication visits the affected asset's claims rather than all registrations and all
peer roles. Cache rebuilding and claim removal also visit only affected claims.
Unchanged asset selections retain their existing claim links. Clearing the final
claim erases revision history, and clearing the composition session erases both
records and index state. Retained immutable query/image snapshots are unaffected.

Equal-revision snapshots may have different statuses or data pointers. Incoming
ties retain their own payloads. If an older incoming snapshot encounters conflicting
ties, the owner falls back to the existing registration-map/role order to choose
the first maximum. This preserves behavior across rehash and removal instead of
introducing a new tie-break rule. Ordinary updates use the cached representative.
All assigned claims finish an update at their asset's latest revision, while tied
payloads can remain distinct. The coordinator retains admission, lease tombstones,
dirty-bit classification, geometry preparation, diagnostics and publication.

The existing shared scan and role metadata move from `Code/Source` to
`Code/Include/TerrainCompositor/Internal/CompositionRegistrationState.h`, alongside
the new owner, so the coordinator can hold it by value without changing public
constructors or introducing a heap-owned implementation. The scan remains the
production mesh algorithm and the comparison reference for image tests. Its existing
implementation is unchanged. Moving it accounts for 95 lines changing categories;
that movement is not counted as a net code reduction.

Before replacement, **68 characterization cases passed** using the existing scan
and original coordinator. These include 11 new state cases and two new coordinator
cases for retargeting the last image role and reactivating the same component.
The temporary test adapter delegated to the production scan and was removed when
the fixture switched to the indexed owner; the characterization assertions remain.
Three further tests exercise exact index membership during 1,000 mixed operations,
equivalence to the production scan (including iteration order and dirty bits),
conflicting ties across rehash/removal, and traversal cost with unrelated records.
The mixed-operation test uses the Google Test shuffle seed for reproducible runs.

The traversal check updates seven dependent claims with **32 and 2,048 unrelated
registrations**. Both cases visit **14 claims** (seven during fan-out and seven
during cache rebuilding), with **zero fallback registration visits**. These are
instrumented logical visits, not elapsed-time benchmarks. Conflicting ties can still
require full-map lookup, and repeated source revisions can require multiple passes
over dependent claims. The index adds per-asset snapshot/claim storage; no memory
reduction or end-to-end speedup is claimed.

Public component declarations, reflection, UUIDs, serialized configuration and
services are unchanged. An exact normalized comparison confirms the coordinator
differs only in image-state access and mutation: query, preparation, publication,
admission and retirement logic match the baseline. Its private C++ layout changes;
consumers must rebuild. Shaders and engine patches are unchanged.

Reproduce measurement with `python tools/MeasureLoc.py --revision 060931b --json`
and `python tools/MeasureLoc.py --worktree --json`. Logs, XML, compatibility checks
and measurements are under `D:/TG/TGProject/build/tc-image-index-session`.
Verification uses the same generated VC projects/dependencies as the prior slices,
with standalone sources and the maintained CMake lists. Fresh CMake generation and
a live editor scene smoke test were not performed.

All five targets built. All **71 focused registration/index cases passed**, including
all 68 pre-replacement cases and the three additional index tests. The full runtime
suite ran **239 cases: 236 passed and the same three known cutout-cache failures
remained**. All 46 editor cases passed. No tests were removed or newly disabled;
the existing disabled case remains. D3D11 matched 142,560 classifications, engine
override generation/rejection checks passed, and `git diff --check` passed.

The 100 shuffled runtime iterations ran 156 cases each: **15,300 passes and 300
occurrences of the same known failures**, with each failure set matching baseline.
The mixed-operation comparison covers 100,000 operations across shuffle seeds
173-272. The shuffled editor run passed **4,600 cases** across those same 100 seeds.

```powershell
& "$bin/AzTestRunner.exe" "$bin/TerrainCompositor.Tests.dll" AzRunUnitTests '--gtest_filter=ImageRegistrationStateTests.*:*ImageRegistrationTests*:*CompositionRegistrationStateTests*:*TerrainRegistrationLifecycleTests*'
& "$bin/AzTestRunner.exe" "$bin/TerrainCompositor.Tests.dll" AzRunUnitTests '--gtest_filter=*LifecycleTests*:*ImageRegistrationTests*:*CompositionRegistrationStateTests*:*PublicationFootprintsTests*:*TerrainHeightProviderNotificationsTests*:ImageRegistrationStateTests.*' --gtest_shuffle --gtest_random_seed=173 --gtest_repeat=100
```

| Repository category | Before `060931b` | After | Change |
| --- | ---: | ---: | ---: |
| Production C++ | 12,938 | 12,841 | -97 |
| Include headers | 3,321 | 3,603 | +282 |
| **First-party C++** | **16,259** | **16,444** | **+185** |
| Tests, including file lists | 5,554 | 5,956 | +402 |
| Shaders/assets | 966 | 966 | 0 |
| Engine patches/overrides | 939 | 939 | 0 |
| Build/tooling | 536 | 536 | 0 |
| Documentation | 3,498 | 3,602 | +104 |
| License/notice | 28 | 28 | 0 |
| **Total repository text** | **27,780** | **28,471** | **+691** |

This step deliberately adds **185 first-party C++ lines** to remove unrelated
revision traversals and keep index lifetime under one owner. The coordinator itself
shrinks by only two lines. Cumulative first-party C++ reduction is now **963 lines
(5.53%)** from the initial extraction and **565 lines (3.32%)** from post-cleanup;
the 50% goal remains unproven. The next ownership opportunity is the repeated
admission/retirement and diagnostic lifecycle around the three typed registration
stores. Cross-mesh revision authority still needs a source-generation contract;
the three cutout-cache preparation failures remain a separate behavioral fix.

## Registration admission, retirement and diagnostic ownership (2026-09-10)

Baseline: `5d4a276` (`Index composition image revision ownership`). The next slice
introduces `Internal::CompositionRegistrations`, a control-thread owner for the
image index, two typed mesh stores, shared lease tombstones and five diagnostic
histories. The coordinator has const typed record views for preparation, queries
and ordering. Registration, removal and session clearing now maintain records and
their associated history through one owner.

The existing admission algorithm moves out of the query helper into this owner.
It still checks context ownership, target, session, lease and update revision before
applying the unchanged image/mesh transformations. The coordinator's six bus
overrides use one registration/publication transition and one removal/publication
transition. Replay and rejection never publish pending dirty work. Successful
admission publishes only when dirty work exists; actual removal retains the same
role-specific dirty mask and publication behavior.

Removal records a shared tombstone even for an unknown entity or an unmatched
lease. Wrong-session and null-lease removals do nothing. Only the matching live
claim loses its record and diagnostic history; a stale lease cannot remove a
replacement or make its warning repeat. The five diagnostic channels retain their
separate warning/recovery histories and exact text. Session clearing retires all
records, index state, tombstones and warning histories together. Collision history,
pending notification queues and immutable publication remain in the coordinator.

Added 15 characterization cases (five shared cases across all three roles) for
cross-role unknown-lease retirement, invalid removal, pending dirty state during
replay/rejection, stale-lease warning history and reactivation of the same object.
All **86 focused cases passed before replacement and unchanged after it**. Existing
image index, mixed-operation, role-specific revision and publication cases remain.
No tests were removed or newly disabled.

Public component declarations and all bus override signatures are unchanged.
Normalized comparison confirms that all coordinator code outside registration
mutation/clearing and moved diagnostic bookkeeping matches after record-accessor
substitution. Reflection, services, status text, queries, geometry preparation,
publication, invalidation and ordering are preserved. The moved admission and
warning-update bodies match the original algorithms. Image indexing, mesh revision
scans, shaders and engine patches are unchanged. The private coordinator layout
changes; consumers must rebuild.

All five runtime/editor/test targets built using the existing generated VC projects
and dependencies, directly referencing standalone sources. The maintained file list
includes the new owner header. Fresh CMake generation and a live Editor scene smoke
test were not performed. The full runtime suite ran **254 cases: 251 passed and
the same three known cutout-cache failures remained**; the existing disabled case
remains. All 46 editor cases passed. D3D11 matched all 142,560 classifications;
engine override generation/repetition/rejection checks and `git diff --check` passed.

The 100 shuffled runtime iterations ran 171 cases each: **16,800 passes and 300
occurrences of the same known failures**, with the failure-name set checked in every
iteration. The editor shuffle passed **4,600 cases**. Both runs use seeds 173-272.
Logs, XML, source audits and per-file counts are under
`D:/TG/TGProject/build/tc-registration-owner-session`.

Reproduce measurement with `python tools/MeasureLoc.py --revision 5d4a276 --json`
and `python tools/MeasureLoc.py --worktree --json`. Counts include the new 172-line
owner and all existing helpers. The coordinator shrinks from 1,938 to 1,856 lines;
the complete first-party C++ scope grows by **49 lines**. This is an ownership
improvement, with no additional traversal, speed or memory reduction claimed.

| Repository category | Before `5d4a276` | After | Change |
| --- | ---: | ---: | ---: |
| Production C++ | 12,841 | 12,722 | -119 |
| Include headers | 3,603 | 3,771 | +168 |
| **First-party C++** | **16,444** | **16,493** | **+49** |
| Tests, including file lists | 5,956 | 6,063 | +107 |
| Shaders/assets | 966 | 966 | 0 |
| Engine patches/overrides | 939 | 939 | 0 |
| Build/tooling | 536 | 537 | +1 |
| Documentation | 3,602 | 3,683 | +81 |
| License/notice | 28 | 28 | 0 |
| **Total repository text** | **28,471** | **28,709** | **+238** |

Cumulative first-party reduction is **914 lines (5.25%)** from the initial
extraction and **516 lines (3.03%)** from post-cleanup. The 50% goal remains
unproven. A subsequent bounded pass can audit obsolete wrappers and comments across
the consolidated owners and their adapters. Cross-mesh revision authority still
requires its source-generation contract, and the three cutout-cache failures remain
a separate behavioral fix.

## Obsolete-wrapper and comment audit (2026-09-10)

Baseline: `475c861` (`Consolidate composition registration lifecycle ownership`).
This bounded audit covers the consolidated registration, asset-subscription,
image-index, registration-store, mesh-placement, provider and editor-preview
owners, their component adapters, and the shared model-asset source.

Five internal entry points are removed:

- The image stamp, composition coordinator and two mesh components now implement
  teardown directly in their existing `Deactivate()` override. Editor teardown
  and restart call that operation; the four private `StopStamp`, `StopCutout` and
  `StopComposition` aliases are gone. Their original stop bodies, guard order,
  subscription teardown, visibility restoration and publication cleanup are intact.
  All four components are final, so the calls cannot dispatch to a derived override.
- `ModelAssetSource::Publish` only forwarded to `Self().Publish`. Its four callers
  now name the derived publisher directly, matching the queued callback paths.
  Each cache retains its own publication and preparation-retirement behavior.

Removed all 12 `jscpd:ignore-start/end` comment lines from six adapter headers.
Duplicate scans now see those declarations; the comments explaining why reflected
editor and terrain-provider interfaces remain explicit are retained. No code is
minified, moved into generated output, or replaced with macros or a new framework.
The reduction is **20 code/declaration/associated separator lines plus 12 comment
lines**, totaling **32 first-party C++ lines (0.19% of this slice's baseline)**.

The audit deliberately retains these small operations and comments:

| Area | Reason to retain |
| --- | --- |
| Component reflection, services, configuration and registration-client entry points | These are existing public or framework contracts, including legacy generic wrappers. |
| `Start*` adapters and mesh registration-update callbacks | They supply editor/entity context or enforce control-thread checks. |
| `RegistrationClient::BeforeRegister` and unavailable/retry hooks | Image registration clears its admission result before dispatch and retains distinct warning/retry behavior. |
| Editor preview accessors, activation overloads and status adapters | Viewport drawing, lifecycle tests, composition quality preparation and reflected status metadata use them. |
| Typed registration views, diagnostic helpers and revision roles | They preserve read-only access, diagnostic channels and independent mesh revision counters. |
| Lifecycle, numerical and compatibility comments | Activation ordering, stale callbacks, equal-revision ties, prefab identity, query draining and render/collision bounds remain non-obvious constraints. |

Normalized source comparison verifies each removed wrapper's expansion against
`475c861`, with the complete stop body unchanged. All other integration files match
the baseline apart from the six comment-only headers. Public declarations, virtual
methods, member fields, base classes, reflection, services, diagnostics, UUIDs and
serialized fields/versions are unchanged. This slice does not change member or
base-class layout. No tests, shaders or engine patches are edited.

All five targets built from the standalone sources: `TerrainCompositor.Static`,
`TerrainCompositor`, `TerrainCompositor.Editor`, `TerrainCompositor.Tests` and
`TerrainCompositor.Editor.Tests`. The existing generated VC projects and installed
dependencies were reused with custom-build regeneration disabled, as documented
above. Fresh CMake generation and a live Editor scene smoke test were not performed.

The unchanged runtime suite ran **254 cases: 251 passed and the same three known
cutout-cache failures remained**. Its test-name set and one disabled test match
the baseline. The editor suite passed **all 46 cases**, before and after the edit.
Across 100 shuffled seeds (173-272), runtime recorded **16,800 passes and 300
occurrences of exactly those known failures**; editor recorded **4,600 passes**.
Every iteration's failure-name set was checked. D3D11 hardware matched **142,560
classifications with zero mismatches**. Engine override generation, repetition and
both hash-mismatch rejection checks passed, as did `git diff --check`.

Reproduce counts with `python tools/MeasureLoc.py --revision 475c861 --json` and
`python tools/MeasureLoc.py --worktree --json`. Counts include every helper and
this report; ignored build logs and temporary audit scripts are not maintained
repository inputs. Logs, XML, source comparisons and per-file measurements are in
`D:/wzmono/terrain-compositor/build/wrapper-audit-session`.

| Repository category | Before `475c861` | After | Change |
| --- | ---: | ---: | ---: |
| Production C++ | 12,722 | 12,698 | -24 |
| Include headers | 3,771 | 3,763 | -8 |
| **First-party C++** | **16,493** | **16,461** | **-32** |
| Tests, including file lists | 6,063 | 6,063 | 0 |
| Shaders/assets | 966 | 966 | 0 |
| Engine patches/overrides | 939 | 939 | 0 |
| Build/tooling | 537 | 537 | 0 |
| Documentation | 3,683 | 3,770 | +87 |
| License/notice | 28 | 28 | 0 |
| **Total repository text** | **28,709** | **28,764** | **+55** |

Cumulative first-party C++ reduction is **946 lines (5.43%)** from the initial
extraction and **548 lines (3.22%)** from post-cleanup. This audit establishes no
safe reduction ceiling, speedup or memory improvement; the 50% target remains
unproven. A further 7,758 C++ lines would have to be removed to reach 8,703.
The next behavioral slice can address the three characterized cutout preparation-
retirement failures separately. Broader geometry preparation and immutable
publication ownership remain architectural candidates; cross-mesh revision
canonicalization still requires a shared source-generation contract.

## Cutout preparation retirement fix (2026-09-10)

Baseline: `a2ca9b8` (`Remove obsolete lifecycle wrappers and scan suppressions`).
This is the separate behavioral fix for the three previously reported cutout-cache
failures. It changes only `TerrainMeshCutoutDataCache.cpp` in production.

Each accepted cutout preparation now receives a ticket owned by its source. The
control thread assigns it before publishing Loading, so subscriber reentrancy
cannot make an older preparation current again. Completion must match both the
source lifecycle generation and its latest preparation ticket, as well as the
existing active-source and model-identity checks. Stale valid and invalid geometry
results are discarded before assigning a revision or notifying subscribers.

Load and reload failures now advance the atomic lifecycle generation immediately
on the callback thread. Both previously queued ready callbacks and pending job
completions become obsolete before the queued Error publication. Subscribers are
still notified only on the control thread, and later ready/reload callbacks can
prepare the model again. The fix adds one private 64-bit counter per cached source;
it does not introduce shared revision authority between the two mesh caches.

Added two typed regression definitions (four cases) for failure before a queued
ready callback followed by reload recovery, and worker-thread reload failure after
preparation followed by ready recovery. The deterministic fixture uses in-memory
models and synchronous preparation with the real queued publication path; the
reload-error case sends the asset event from a worker. Both new cutout cases failed
before the fix, alongside the three original failures; all mesh-height cases passed.
After replacement, all **26 cache lifecycle cases passed unchanged**.

All five standalone-source targets built using the existing generated VC projects
and installed dependencies. The full runtime suite passed **258/258 cases**, including
all three former failures and the four added cases. The one previously disabled
case remains disabled; no existing test was edited or removed. Editor passed
**46/46 cases**. Across 100 shuffled seeds (173-272), runtime passed **17,500 cases**
and editor passed **4,600 cases**, with zero failures in every iteration. D3D11
hardware matched **142,560 classifications with zero mismatches**. Engine override
generation/repetition/hash-rejection checks and `git diff --check` passed.
Fresh CMake generation and a live Editor scene smoke test were not performed.

Source comparison confirms unchanged geometry extraction/build algorithms,
publication payloads, validation/status messages and revision assignment outside
the new rejection guards. All public headers, reflected/serialized contracts, the
mesh-height cache, shared model source, shaders and engine patches are unchanged.
`TerrainMeshCutouts.md` now documents the failure/recovery contract.

Measure with `python tools/MeasureLoc.py --revision a2ca9b8 --json` and
`python tools/MeasureLoc.py --worktree --json`. Logs, before/after XML, source audits
and per-file counts are under
`D:/wzmono/terrain-compositor/build/cutout-retirement-session`.

| Repository category | Before `a2ca9b8` | After | Change |
| --- | ---: | ---: | ---: |
| Production C++ | 12,698 | 12,704 | +6 |
| Include headers | 3,763 | 3,763 | 0 |
| **First-party C++** | **16,461** | **16,467** | **+6** |
| Tests, including file lists | 6,063 | 6,109 | +46 |
| Shaders/assets | 966 | 966 | 0 |
| Engine patches/overrides | 939 | 939 | 0 |
| Build/tooling | 537 | 537 | 0 |
| Documentation | 3,770 | 3,844 | +74 |
| License/notice | 28 | 28 | 0 |
| **Total repository text** | **28,764** | **28,890** | **+126** |

This behavioral fix adds six C++ lines. Cumulative first-party C++ reduction is
**940 lines (5.40%)** from initial extraction and **542 lines (3.19%)** from
post-cleanup. The 50% target and a safe reduction ceiling remain unproven. Geometry
preparation and immutable publication ownership remain the next architectural
candidates; cross-mesh revision authority still needs a source-generation contract.

## Composition preparation and publication ownership (2026-09-10)

Baseline: `84fcafd` (`Retire stale cutout preparations on replacement and failure`).
This bounded slice separates deterministic candidate preparation from the
coordinator's bus access, diagnostic history, reconstruction cache and publication.

`PrepareComposition` reads admitted registrations and explicit configuration,
region, session, grid-spacing and non-uniform-scale inputs. Its value-owned result
contains the prepared query geometry, render cutouts, candidate footprints,
ordering collisions and normalized registration diagnostics. It calls no buses,
interfaces, caches or publishers, and does not mutate registrations or history.
`PreparedComposition` supplies the geometry fields of the private query state;
moving those fields into a replacement does not copy contributor lists. These
three new files total **286 lines**, all included in the measurements below.

The coordinator captures environment inputs, consumes diagnostics through the
existing registry history, attaches cached reconstruction, handles unavailable or
rejecting render publication, and atomically exchanges immutable query ownership.
All invalidation/exchange code from old-footprint derivation onward is unchanged.
Render rejection still retains the previous CPU pointer and pending dirty work.
Diagnostic recovery/removal history and palette/collision/role warning order are
preserved, including collision claim traversal order independently of blend order.

Role geometry remains explicit: image height and surface contributions require
positive strength, while image masks do not; zero-strength mesh stamps may retain
gaps; cutout render geometry is copied before collision-cell padding; mesh gaps
retain distinct logical/render and conservative query bounds. Candidate footprints
are still collected before sorting, and previous footprints from the sorted
publication. Those traversals intentionally retain different first-claim behavior.

Actual bookkeeping changes and costs:

- One captured non-uniform-scale probe per image registration replaces three bus
  probes. This introduces an image-input traversal and a temporary set; accepted
  height contributors are traversed afterward to attach reconstruction.
- Geometry preparation emits diagnostic values; one coordinator loop now updates
  the registry's five history channels. This adds a temporary diagnostic vector
  and its consumption traversal instead of mutating history inside role loops.
- Collision suppression reads the already-built claim map. Ordered collision
  values preserve diagnostic order before rebuilding the coordinator's history.
- Missing-registry fallback filters the owned gap and existence lists directly.
  The duplicate gap vector, suppressed-entity set, preliminary `any_of` and separate
  suppression-collection pass are removed. With a registry, its by-value Publish
  argument still copies the gap list once. Without one, both lists are filtered
  even when no coupled gap is present; no end-to-end speedup is claimed.
- Scene-channel acquisition and render publication share one scene lookup.

This is an ownership separation with **79 additional first-party C++ lines**, not
a LOC reduction. New candidate diagnostic/collision data and input capture have
temporary allocation/traversal costs; no overall memory improvement is claimed.
The three role preparation loops and old/new footprint traversals remain.

| Focused C++ scope | Before | After | Change |
| --- | ---: | ---: | ---: |
| Coordinator implementation | 1,851 | 1,640 | -211 |
| Coordinator header | 241 | 228 | -13 |
| Registration owner header | 172 | 187 | +15 |
| Publication footprint helper | 46 | 48 | +2 |
| New preparation implementation, declaration and geometry header | 0 | 286 | +286 |
| **Complete focused scope** | **2,310** | **2,389** | **+79** |

`PublishStamps()` itself changes from **397 to 211 physical lines**, counting its
signature, braces, blanks and comments, but excluding the following separator.
The coordinator's other 25 removed lines are the cutout data diagnostic helper
retained verbatim in preparation. Function-size reduction alone overstates savings.

Added six tests / 213 lines. Three characterization cases passed against the
original production implementation: render rejection/retry with retained dirty
work, reconstruction reuse/retention, and invalid cross-role ordering collisions
with warning recovery. Three more exercise deterministic preparation with captured
scale and no publication/history effects, image zero-strength role differences,
cutout render versus padded collision coverage, and zero-strength mixed render/query
gaps with missing-registry suppression and subsequent publication recovery.

All five standalone-source targets built. Runtime passed **264/264 cases**, editor
**46/46**; the one previously disabled runtime test remains. All existing test names
are retained. Across 100 shuffled seeds (173-272), runtime passed **19,000 cases**
and editor **4,600**, with zero failures in every iteration. Runtime shuffle uses
the documented lifecycle/state filter plus image-index, publication-footprint,
retained-render and preparation suites. D3D11 hardware matched **142,560 boundary
classifications with zero mismatches**. Engine override generation, repetition and
both hash-rejection checks passed, as did `git diff --check`.

Source audits verify unchanged public component/configuration declarations,
reflection/serialization, coordinator code outside the extraction boundary, stream
sorting, diagnostic normalization/history and the complete invalidation/exchange
suffix. The private query record now derives its geometry fields from an internal
value type; its C++ layout changes and consumers must rebuild. Component UUIDs,
serialized layouts, shaders, engine patches and public component methods are intact.

Builds reuse the generated VC projects and installed dependencies described above,
with regeneration disabled. The generated static unity input includes the new
preparation source directly from this checkout; the maintained CMake list includes
all three new files. No sources were copied into TG. Fresh CMake generation and a
live Editor scene smoke test were not performed. Logs, XML, audits and per-file
measurements are under `build/composition-preparation-session` (ignored).

Reproduce measurements with `python tools/MeasureLoc.py --revision 84fcafd --json`
and `python tools/MeasureLoc.py --worktree --json`. Scope includes every helper,
test, maintained build input and this report; ignored build output is excluded.

| Repository category | Before `84fcafd` | After | Change |
| --- | ---: | ---: | ---: |
| Production C++ | 12,704 | 12,760 | +56 |
| Include headers | 3,763 | 3,786 | +23 |
| **First-party C++** | **16,467** | **16,546** | **+79** |
| Tests, including file lists | 6,109 | 6,322 | +213 |
| Shaders/assets | 966 | 966 | 0 |
| Engine patches/overrides | 939 | 939 | 0 |
| Build/tooling | 537 | 540 | +3 |
| Documentation | 3,844 | 3,964 | +120 |
| License/notice | 28 | 28 | 0 |
| **Total repository text** | **28,890** | **29,305** | **+415** |

Cumulative first-party C++ reduction is **861 lines (4.95%)** from initial extraction
and **463 lines (2.72%)** from post-cleanup. The 50% target and safe reduction ceiling
remain unproven. Invalidation planning is a possible later boundary; extracting it
should preserve old/new region contexts and first-claim behavior. Cross-mesh revision
authority still requires the separately deferred source-generation contract.

## Invalidation planning and publication ownership (2026-09-10)

Baseline: `72fa557` (`Separate composition preparation from publication ownership`).
This slice extracts deterministic invalidation planning and gives normal render/CPU
publication one consuming commit boundary. Preparation, role geometry, registration
classification, cache behavior, tick dispatch and shutdown behavior are preserved.

`PendingCompositionInvalidation` owns footprint notification metadata and both
terrain invalidation queues. `PlanCompositionInvalidation` transforms that value
and the dirty map using immutable previous/current query state and the explicit
candidate footprints. It derives old footprints, marks membership changes, handles
whole-region changes, records old/new stamp metadata and routes surface, existence,
cutout-overlap and mesh-gap coverage. It calls no buses or publishers. Its narrow
state template lets the component keep its private query type and tests supply
value-only records; it takes no component pointers or callback parameters.

The planner folds work into the existing pending queues in their original order.
`TerrainInvalidation` merging is not associative: for XY rectangles A=(0,0)-(1,2),
B=(1,0)-(2,2), C=(2,0)-(5,3), adding old B and new C to pending A preserves two
regions under the 1.1 merge-area bound. Merging B+C before applying the result to A
would create one larger rectangle. The new regression test preserves this concrete
distinction. Production moves the pending value and dirty map into the planner;
there is no intermediate operation list, pre-coalesced batch merge or replay pass.

`CommitPublication` consumes the mutable candidate, candidate footprints and render
cutouts. After existing missing-registry filtering and channel acquisition, it moves
the candidate into `shared_ptr<const QueryState>` before a render query can retain
it. The captured publication identity supplies its scene/session/revision. Render
rejection returns before consuming any dirty or pending invalidation work. On
acceptance, pure planning produces the next pending value, the atomic query pointer
is exchanged, and pending work is installed for deferred dispatch. No external calls
occur between planning and installing those control-thread values. Shutdown retains
its separate empty-query publication and whole-region cleanup.

The five membership comparisons retain their category order. Candidate footprints
remain from before blend sorting, while old footprints come from the sorted query
publication. Old-before-new regions, footprint metadata and cutout intersections
are preserved. Query-only gaps invalidate height coverage, render-only gaps surface
coverage, and coupled gaps retain distinct bounds for each. Surface-only dirty bits
and palette-only changes do not create height work. Unchanged/absent membership
does not repeatedly publish removal metadata.

The owner removes three separately stored component fields in favor of one value,
and one source/region comparison replaces the duplicated expression. The underlying
metadata list, independent height/surface queues, dirty map, membership scans and
footprint traversals remain necessary and remain present. No speed or memory
reduction is claimed. The helpers add **167 lines**, including the relocated
membership and XY-intersection algorithms. Complete first-party C++ grows **51
lines**; the component shrink alone is not a LOC saving.

| Focused C++ scope | Before | After | Change |
| --- | ---: | ---: | ---: |
| Coordinator implementation | 1,640 | 1,538 | -102 |
| Coordinator header | 228 | 233 | +5 |
| Query helpers (membership algorithm relocated) | 60 | 41 | -19 |
| New planner and pending-value headers | 0 | 167 | +167 |
| **Complete focused scope** | **1,928** | **1,979** | **+51** |

`PublishStamps()` changes from **211 to 68 physical lines**; the new
`CommitPublication()` is **59 lines**. Counts include signatures, braces, blanks
and comments, excluding the following separator. Both functions and all helpers
are included above and in the repository counts.

Added ten cases / 335 net test lines. Five characterization cases passed with the
original implementation: empty old/new region contexts, palette-only invalidation,
rejection with accumulated terrain work and retry, chronological metadata across
multiple publications, and registration during footprint dispatch. The five direct
planner cases cover differing published/candidate first claims, six role-removal
routes and repeated absence, old/new height-cutout intersections, existing-queue
merge order and immutable caller inputs, and independent dirty channels.

All five standalone-source targets built. Runtime passed **274/274 cases**, editor
**46/46**. Existing test names are retained and the one previously disabled runtime
test remains disabled. Across 100 shuffled seeds (173-272), runtime passed **20,000
cases** and editor **4,600**, with zero failures in every iteration. Runtime shuffle
uses the preceding slice's filter plus `*CompositionInvalidationTests*`. D3D11
hardware matched **142,560 boundary classifications with zero mismatches**. Engine
override generation/repetition/hash-rejection checks and `git diff --check` passed.

Source comparison confirms unchanged preparation, diagnostics, reconstruction,
missing-registry filtering and membership traversal order. Outside the extraction,
coordinator code matches baseline after mechanical pending-owner substitutions,
including query sampling, dispatch, source callbacks, shutdown and tick behavior.
Public component/configuration declarations, reflection, UUIDs, serialization,
shaders and engine patches are unchanged. Grouping/reordering the private pending
fields changes component C++ layout; consumers must rebuild.

Builds reuse existing generated VC projects and installed dependencies with custom
regeneration disabled. Both new headers are listed in maintained CMake inputs and
are compiled through their standalone source includes. No source copies or new
generated unity edits were needed. Fresh CMake generation and a live Editor scene
smoke test were not performed. Logs, XML, source audits and per-file measurements
are under `build/invalidation-publication-session` (ignored).

Reproduce counts with `python tools/MeasureLoc.py --revision 72fa557 --json` and
`python tools/MeasureLoc.py --worktree --json`. Every helper, test, maintained build
input and this report is included; ignored outputs are excluded.

| Repository category | Before `72fa557` | After | Change |
| --- | ---: | ---: | ---: |
| Production C++ | 12,760 | 12,784 | +24 |
| Include headers | 3,786 | 3,813 | +27 |
| **First-party C++** | **16,546** | **16,597** | **+51** |
| Tests, including file lists | 6,322 | 6,657 | +335 |
| Shaders/assets | 966 | 966 | 0 |
| Engine patches/overrides | 939 | 939 | 0 |
| Build/tooling | 540 | 542 | +2 |
| Documentation | 3,964 | 4,080 | +116 |
| License/notice | 28 | 28 | 0 |
| **Total repository text** | **29,305** | **29,809** | **+504** |

Cumulative first-party C++ reduction is **810 lines (4.65%)** from initial
extraction and **412 lines (2.42%)** from post-cleanup. The 50% target and a safe
reduction ceiling remain unproven. Cross-mesh revision authority still requires its
source-generation contract; broader duplicate/comment and algorithm audits remain.

## Indexed mesh registration ownership (2026-09-10)

Baseline: `5335cc2` (`Extract invalidation planning and publication commit ownership`).
The image registration owner is now a narrow `IndexedRegistrationState` template
with three concrete aliases. Images retain their shared five-role index; cutouts
and mesh-height stamps each own separate record and asset maps. Matching canonical
asset IDs never share revision authority across those three instances.

All accepted inserts, placement edits, asset retargets, removals and session clears
maintain the dependent claims and cached maximum together. Reconciliation precedes
classification, including the existing claim for the entity being replaced. The
caller remains unchanged, and fan-out accumulates only the dependent role's dirty
bits. Removing the final claim retires its history. Configured product selections
do not replace canonical snapshot IDs as the index key.

Mesh unassigned IDs have indexed claims for reconciliation, but never fan out.
Their retained revisions can differ, and removing or retargeting the maximum can
lower the cached maximum. An absent record is distinct from an unassigned claim;
assigned insertion/removal must not touch the unassigned bucket. Image unassigned
roles retain their prior local-only policy. Invalid IDs with different sub-IDs also
remain distinct keys, matching the scan's exact asset-ID comparison.

Equal revisions retain their payloads. An older input facing conflicting maxima
uses the existing full-map first-maximum fallback, preserving record/role order
through rehash and removal. Mesh comparisons include validation, model validation
and every diagnostic field/detail as well as status and immutable data pointer.
Ordinary stale inputs use the cached representative without that fallback.

The former production scan is retained in `Tests/RegistrationStateReference.h`.
Its 57 algorithm lines match baseline exactly after removing two optional visit
counters. No production path calls it. The owner/registry/reference scope grows
from 468 to 510 physical lines (+42); the production header decrease alone does
not represent eliminated code. New tests and file-list entries are counted below.

Equivalence checks compare full snapshots, classification inputs, caller values,
record iteration order, and exact dirty masks by entity. Like the image index,
fan-out follows dependent-claim order. Dirty-map insertion/iteration order across
different entities can differ from the scan; AZStd's order-sensitive map equality
is therefore not used for that assertion. Cross-entity footprint notification
order and invalidation coalescing partition can consequently differ, while the
same entities/channels/footprints are invalidated and each entity retains old-before-
new processing. This is not a claim of identical dirty-map or invalidation ordering.

For each mesh domain, seven dependent registrations give these logical visits:

| Operation | Unrelated records | Original scan record visits | Indexed claim visits | Fallback record visits |
| --- | ---: | ---: | ---: | ---: |
| Assigned revision update | 32 | 78 | 14 | 0 |
| Assigned revision update | 2,048 | 4,110 | 14 | 0 |
| Assigned stale placement | 32 | 78 | 7 | 0 |
| Assigned stale placement | 2,048 | 4,110 | 7 | 0 |
| Unassigned revision update or stale placement | 32 | 39 | 7 | 0 |
| Unassigned revision update or stale placement | 2,048 | 2,055 | 7 | 0 |

Assigned fan-out visits seven claims and cache rebuilding visits seven. A stale
placement needs only rebuilding; unassigned updates skip fan-out. An additional
check inserts/removes an assigned registration alongside 2,048 unassigned claims:
insertion visits two claims and removal one, with no fallback visits. Traversal
properties are retained in the focused test XML for both mesh specializations.
These counters include claim removal, fan-out and maximum rebuilding, not hash
probes, diagnostic-detail comparisons, elapsed time or allocations. Conflicting
ties can still scan the full registration map. Per-asset snapshots and claim
vectors add memory; no end-to-end speedup or memory reduction is claimed.

Before replacement, 87 characterization/index/lifecycle cases passed against the
original mesh scan, including ten new mesh cases. The final tests add 19 runtime
cases: 18 typed mesh-index cases and one coordinator case proving independent mesh
domains for both assigned and unassigned IDs. Mixed-operation tests verify exact
index membership and scan equivalence across insert, edit, retarget, remove and
clear operations, with randomized statuses, validation, diagnostics and dirty work.

Public component declarations, UUIDs, reflection, serialization, all runtime source
implementations, shaders and engine patches are unchanged. Source audit verifies
that admission, replay, lease tombstones, removal masks and diagnostic history
match baseline after indexed-store substitutions. The coordinator's private C++
layout gains the two mesh asset maps; consumers must rebuild.

Builds reuse the five generated VC projects and installed dependencies with custom
regeneration disabled. Maintained CMake inputs list both new test files, and the
generated test unity input includes the new source directly from this checkout.
No sources were copied into TG. Fresh CMake generation and a live Editor scene
smoke test were not performed. Logs, XML, audits and per-file counts are under
`build/mesh-index-session` (ignored).

Reproduce counts with `python tools/MeasureLoc.py --revision 5335cc2 --json` and
`python tools/MeasureLoc.py --worktree --json`. All helpers, the retained reference,
tests, maintained file lists and this report are included; ignored output is not.

All five targets built. Focused index/domain tests passed 33/33; runtime passed
293/293 and editor 46/46. All existing test names remain, including the one already
disabled runtime case. Across shuffle seeds 173-272, runtime passed 20,800 cases
(208 per iteration) and editor 4,600, with zero failures. The three differential
tests cover 300,000 mixed operations across those seeds. D3D11 hardware matched
142,560 boundary classifications with zero mismatches. Engine override generation,
repeat/hash-rejection checks, source audit and `git diff --check` passed.

```powershell
$bin = 'D:/TG/TGProject/build/windows/bin/profile'
& "$bin/AzTestRunner.exe" "$bin/TerrainCompositor.Tests.dll" AzRunUnitTests '--gtest_filter=*MeshRegistrationStateTests*:*ImageRegistrationStateTests*:*MeshRevisionDomains*'
& "$bin/AzTestRunner.exe" "$bin/TerrainCompositor.Tests.dll" AzRunUnitTests '--gtest_filter=*LifecycleTests*:*ImageRegistrationTests*:*CompositionRegistrationStateTests*:*PublicationFootprintsTests*:*TerrainHeightProviderNotificationsTests*:ImageRegistrationStateTests.*:*MeshRegistrationStateTests*:*CompositionInvalidationTests*' --gtest_shuffle --gtest_random_seed=173 --gtest_repeat=100
```

| Repository category | Before `5335cc2` | After | Change |
| --- | ---: | ---: | ---: |
| Production C++ | 12,784 | 12,784 | 0 |
| Include headers | 3,813 | 3,788 | -25 |
| **First-party C++** | **16,597** | **16,572** | **-25** |
| Tests, including reference and file lists | 6,657 | 7,234 | +577 |
| **C++ plus tests/file lists** | **23,254** | **23,806** | **+552** |
| Shaders/assets | 966 | 966 | 0 |
| Engine patches/overrides | 939 | 939 | 0 |
| Build/tooling | 542 | 542 | 0 |
| Documentation | 4,080 | 4,202 | +122 |
| License/notice | 28 | 28 | 0 |
| **Total repository text** | **29,809** | **30,483** | **+674** |

The first-party count is 835 lines (4.80%) below initial extraction and 437 (2.57%)
below post-cleanup. This slice grows total code; its first-party decrease includes
moving the reference into tests. The 50% target and a safe reduction ceiling remain
unproven. Shared cross-mesh revision authority still requires a source-generation
contract; conflicting-tie fallback and composition/publication traversals remain.
