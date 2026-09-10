# LOC reduction: first architecture slice

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

## Selected next step

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
