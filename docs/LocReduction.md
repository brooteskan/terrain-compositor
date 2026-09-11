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
