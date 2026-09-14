# Production function audit (issue #11)

Baseline: `9ae5107b15631bce5234b9cc4950922e8883b3d9`.
Scope: first-party production C++, public/internal headers, shaders, complete function bodies in maintained engine patches, and maintained engine override sources under `integrations/o3de`. Tests and their independent oracles, benchmarks, third-party code, generated override output, and build output are excluded from the production duplicate count.

The audit consolidated **46 shared operations, removing 83 redundant operation implementations**. This is a manual inventory of repeated bodies or repeated suboperations, not a count of deleted public functions. Suboperations such as coordinate construction and interpolation are counted separately; their line savings must not be added together. Typed API, engine callback, and O3DE reflection entry points remain where required. No confirmed shared production algorithm remains unresolved in the reviewed set. This is an audit conclusion with the detection limits below, not a proof that arbitrary C++ programs have unique semantics.

## Repeatable check

Run from the repository root with Python 3.10 or newer:

```sh
python tools/AuditDuplicateFunctions.py --check --near --json build/production-function-audit.json
python -m unittest discover -s tools/tests -v
python tools/AuditDuplicateFunctions.py --revision 9ae5107b15631bce5234b9cc4950922e8883b3d9 --near --json build/production-function-baseline.json
python tools/MeasureLoc.py --revision 9ae5107b15631bce5234b9cc4950922e8883b3d9 --json
python tools/MeasureLoc.py --worktree --json
```

Create `build` first if it does not exist. The JSON report includes the complete input inventory, recognized function locations, exact groups, and near pairs. The working-tree scan includes unignored new files. Historical scans read committed blobs without checkout/filter effects. `MeasureLoc.py` ignores dependency gitlinks rather than treating them as file contents.

The scanner ignores comments and whitespace and preserves identifiers and literals for exact matching. Near matching consistently renames identifiers within each body, retains literals, and uses a similarity threshold of 0.82 for bodies of at least 30 tokens (minimum length ratio 0.72). It includes lambdas, ordinary functions, common qualifiers, and simple operator bodies. It is deliberately lexical: complex trailing return types, constructor initializer lists, macros, conditional compilation, and incomplete patch hunks require manual inspection. Patch hunks are never joined across missing context. Shader arithmetic and engine changes were also reviewed directly; unchanged third-party engine bodies are not counted as compositor duplicates.

`--check` rejects exact groups without a matching entry in [ProductionFunctionAuditExceptions.json](../tools/ProductionFunctionAuditExceptions.json). Each entry records the reviewed body hash, exact caller list, and a specific reason. Line movement does not invalidate a review, but changed bodies or additional callers do. Empty functions contain no implementation and are omitted. Near pairs are review candidates and do not automatically fail the check; review them when modifying affected code. The tool's unit tests cover tokenization, comments/strings, lambdas/operators, nested blocks, incomplete patches, scope, and consistent identifier renaming.

| Same scanner on both revisions | Baseline | After |
| --- | ---: | ---: |
| Production input files | 148 | 156 |
| Recognized bodies (including nested lambdas) | 1,074 | 1,117 |
| Exact groups | 54 | 54 |
| Near pairs | 87 | 41 |
| Unreviewed exact groups in final working tree | — | 0 |

Exact group totals are not an implementation savings metric: extracting an algorithm often creates identical one-line forwarding methods. Every remaining exact group is either a forwarding adapter, a primitive field operation, or an independent schema/policy declaration with a recorded disposition.

## Shared-operation inventory

Paths below are relative to `integrations/o3de/Code`. `Source/` contains private implementations; `Include/TerrainCompositor/` contains public and internal headers. Test evidence names refer to existing files in `Tests/` unless specified otherwise. The three stamp roles are image heightmap, mesh cutout, and mesh height. The six editors are those roles plus composition, height provider, and surface provider.

| Shared operation | Single implementation | Callers now delegating | Redundant implementations removed | Evidence |
| --- | --- | --- | ---: | --- |
| Bilinear interpolation | `Source/ImageSampling.h::SampleBilinear` | Heightmap sampling and existence-mask sampling | 1 | `HeightmapReconstructionTests`, `InternalHelperTests` independent bitwise rows |
| Bilinear coordinates | `Source/ImageSampling.h::GetBilinearCoordinates` | Shared interpolation and surface sampling | 2 | `InternalHelperTests`, surface cases in `ProceduralGroundGradientComponentTests` |
| Surface ID decoding | `Source/SurfaceStampSampling.cpp::AddDecodedId` / `AddDecodedPixel` | Single-ID pixels and weighted A/B channels | 2 | Surface blend, transparency, palette and scalar/batch tests |
| Yaw validation | `Source/StampMath.h::TryGetStampYaw` | Image and mesh placement validation | 1 | Placement cases in `ProceduralGroundGradientComponentTests`, `TerrainMeshCutoutTests`, `TerrainMeshHeightStampTests` |
| Rotated mesh XY bounds | `Source/StampMath.h::TransformStampXYBounds` / `StampXYBounds` | Cutout and mesh-height placement | 1 | Rotated/unequal-grid placement, overflow and outward-rounding tests |
| Editor configuration reflection | `Source/Editor/EditorConfiguration.h::ReflectEditorConfiguration` | All six editor `Reflect` adapters | 5 | `TerrainEditorPreviewLifecycleTests` reflection/removal/metadata tests |
| Editor refresh | `EditorConfiguration.h::RefreshEditorConfiguration` | All six `OnConfigurationChanged` adapters | 5 | `TerrainEditorPreviewLifecycleTests` |
| Normalized batch heights | `Source/Components/TerrainCompositionGradientComponent.cpp::GetNormalizedHeights` | `GetValues` and `GetHeights` with their already-retained state | 1 | Scalar/batch/boundary/source-call/reentrant composition tests |
| Gap admission | `TerrainExistenceSampling.h::IsTerrainMeshHeightGapAdmitted` | Collision-cell removal and existing render gap consumers | 1 | `TerrainMeshHeightGapGpuTests`, `TerrainMeshCutoutTests` collision admission, `TerrainEngineBoundaryTests` |
| Model job dispatch | `Source/ModelAssetSource.h::PrepareModel` | Both model caches with explicit ticket timing | 1 | Typed `TerrainModelCacheLifecycleTests`, including reentrant Loading |
| Prepared model publication | `ModelAssetSource.h::PublishPreparedSnapshot` | Both model cache `Publish` adapters | 1 | Successful/rejected geometry, immutable retained data, failure and replacement tests |
| Canonical source acquisition | `Source/AssetSource.h::AcquireAssetSource` | Image and both model caches | 1 | Catalog aliases, last-handle release, recovery and cache retirement tests |
| Catalog lookup | `AssetSource.h::GetAssetInfo` | Image and model source lifecycles/acquisition | 1 | Same catalog lifecycle tests |
| Source retirement | `AssetSource.h::StopAssetSources` | All three cache destructors | 2 | Cache destruction, subscriber disconnect, late completion tests |
| Snapshot access | `AssetSource.h::GetAssetSourceSnapshot` | All three cache `GetSnapshot` adapters | 2 | Empty handle, control-thread and retained snapshot tests |
| Event connection | `AssetSource.h::ConnectAssetSourceChanged` | All three cache event APIs | 2 | Subscriber/control-thread lifecycle tests |
| Model failure queuing | `ModelAssetSource.h::QueueFailure` | Error/reload-error callbacks for both model roles | 1 | Failure before ready, during preparation, worker failure, later recovery |
| Persistent ordering identity | `HeightmapStampIdentity.cpp::AssignNewStampOrderingIdentity` | Three config identity APIs | 2 | Ordering identity/export tests |
| Runtime ordering key | `HeightmapStampIdentity.cpp::GetRuntimeStampOrderKey` | Three config key APIs | 2 | Ordering identity/runtime fallback tests |
| Image buffer validation | `Source/ImageSampling.h::HasCompleteImageBuffer` | Heightmap samples and raw surface samples | 1 | Invalid/degenerate image and helper tests |
| Render image-mask composition | `Source/TerrainExistenceSampling.cpp::ComposeRenderImageMasks` | Full contributor span and selected pointer span | 1 | Existence composition scalar/batch tests and retained render tests |
| Registration active predicate | `Internal/RegistrationClient.h::IsClientActive` | Three registration APIs/internal guard | 2 | `TerrainRegistrationLifecycleTests`, `TerrainMeshPlacementLifecycleTests` |
| Registration registered predicate | `RegistrationClient.h::IsClientRegistered` | Three registration APIs | 2 | Same registration lifecycle tests |
| Ordering diagnostics | `RegistrationClient.h::GetOrderingStatus` | Three role status builders | 2 | Pending/missing/ambiguous ordering and role diagnostic tests |
| Provider config metadata | `Source/ProviderReflection.h::ReflectProviderConfiguration` | Height and surface provider configs | 1 | `TerrainProviderLifecycleTests`, editor reflection tests |
| Provider component metadata | `ProviderReflection.h::ReflectConfiguredComponent` | Height and surface runtime providers | 1 | Provider reflection/removal/config tests |
| Hill hash | `ProceduralHillKernel.h::MixBits` | Reference and optimized field paths | 1 | `ProceduralHillKernelTests` independent frozen oracle |
| Hill coordinate noise | `ProceduralHillKernel.h::CoordinateValue` | Reference and optimized field paths | 1 | Same bitwise frequency/coordinate/config sweeps |
| Hill smooth curve | `ProceduralHillKernel.h::SmoothCurve` | Reference and optimized field paths | 1 | Same oracle and policy tests |
| Reference hill field | `ProceduralHillKernel.h::EvaluateReferenceField` | Public component reference API and reference/pruned kernel policies | 1 | `Tests/ProceduralHillReference.h` remains independent |
| Priority/key ordering | `HeightmapStampIdentity.cpp::StampPriorityLess` | Composition contributors and both render lists | 2 | Equal-priority/key and registration ordering tests |
| Ordering claim counting | `Source/Components/TerrainCompositionQueryHelpers.h::CountOrderingClaims` | Three registration maps | 2 | Registration claim/pending identity tests |
| Region activation events | `TerrainCompositionGradientComponent::OnRegionActivationChanged` | Region activate/deactivate callbacks | 1 | Region publication/removal/invalidation tests |
| Configuration assignment/notification | `Source/ComponentConfiguration.h::AssignConfiguration` / overload of `ReadConfiguration` | Six editor reads, procedural read, two mesh setters | 8 | Editor/provider/component lifecycle and configuration tests |
| Ordered entity export | `EditorConfiguration.h::BuildOrderedGameEntity` | Three stamp editor game-entity builders | 2 | Editor export, missing-order-key and UUID/config tests |
| Export key assignment | `EditorConfiguration.h::SetEditorExportKey` | Three stamp editor conversion adapters | 2 | Inactive conversion/active preview export tests |
| Dependency lock ordering | `TerrainPreparationDependency.h::CollectTerrainPreparationDependencies` | Dependency set and reusable admission | 1 | `TerrainSectorSchedulingTests` admission/lock lifetime tests |
| Scalar render lookup | `TerrainMeshCutoutRenderRegistry.h::TryGetTerrainRenderValue` | Height and existence scalar APIs | 1 | First owner, unavailable callback, overlap and boundary tests |
| Render region containment | `TerrainMeshCutoutRenderRegistry.h::TerrainRenderRegionContains` | Scalar lookup, batch owner resolution, sector corners | 4 | `TerrainMeshCutoutTests`, `TerrainSectorSamplingTests` |
| Batch render ownership | `TerrainMeshCutoutRenderRegistry.h::FindTerrainRenderOwners` | Ordinary batch application and retained plan resolution | 1 | Overlap, separate channel owners, reentrant replacement tests |
| Sector identity generation | `TerrainPreparationDependency.h::NextTerrainIdentity<Domain>` | Two identity constructors in `TerrainMeshManager.h.patch` | 1 | `TerrainSectorLifetimeTests` destination/generation ownership |
| Mesh placement diagnostics | `Internal/MeshPlacementLifecycle.h::GetPlacementStatus` | Cutout and height component status adapters | 1 | `TerrainMeshPlacementLifecycleTests` |
| Mesh BVH traversal | `Source/MeshCutoutTraversal.h::VisitMeshCutoutTriangles` | Self-intersection, distance and containment traversals | 2 | `TerrainMeshCutoutTests` geometry, inside/boundary/winding cases |
| Renderer timestamps | `Internal/TerrainTiming.h::TerrainTimestampMicroseconds` | Mesh manager and detail material manager patches | 1 | Both engine translation units compile; sector lifetime tests |
| Asset snapshot equality | `Internal/AssetSnapshot.h::AssetSnapshotsEqual` | Image, cutout and mesh-height coordinator comparisons | 2 | Registration dirty/publication/revision tests |
| Retained allocation accounting | `Internal/RetainedCompositionMemory.h::RetainedAllocationBytes` | Three asset payloads, collision mask, reconstruction | 4 | Sector dispatcher budget and retained-lifetime tests |

## Semantic review and retained differences

All 41 final near pairs are accounted for by these groups. The exact-match manifest separately lists all 54 exact groups, including the required event adapters and primitive comparisons. The broader manual review also inspected query/publication ownership, registration reconciliation, provider binding, engine resource commits, shaders, and numerical/reference paths that fall below the near threshold.

| Remaining candidate group | Review disposition |
| --- | --- |
| Three cache `Acquire` adapters (3 pairs) | Shared canonical acquisition; typed construction and the existing public control-thread/invalid-ID guards remain at their boundaries. |
| Editor `Reflect` adapters (15 pairs) | Shared metadata implementation; adapters supply distinct types, names, descriptions and member pointers. No schema identity is merged. |
| System/feature/identity/runtime component and surface-config reflection (7 pairs) | Distinct O3DE schemas, base classes, version/field sets, and remove-reflection ownership. Two provider components share their common config metadata; remaining declarations express different contracts. |
| Mesh incompatible-service declarations (1 pair) | Different service IDs/roles; service declarations must remain attached to each component schema. |
| Existence contributor priority/key/bounds accessors (3 pairs) | Tagged-union accessors select different fields and return types/defaults; primitive field projection rather than repeated policy. |
| Image source `Start` versus model source `Start` (1 pair) | Image mip/stream loading and model reload/asset-generation protocols differ. Common catalog/acquisition work is shared; event protocols are deliberately distinct. |
| Heightmap/composition `ReadInConfig` (1 pair) | Both delegate type checking; applying a stamp config and applying composition config invoke distinct lifecycle/invalidation contracts. |
| Cutout/height `GetStatusMessage` (1 pair) | Shared placement diagnostics are called with unchanged role text, then each role obtains its own registration diagnostics. |
| Registration snapshot payload comparison versus coverage `SameKey` (1 pair) | Similar equality syntax over unrelated identities. Registration tie comparison intentionally includes diagnostics and excludes revision/asset ID; publication comparison includes revision/asset ID. |
| Procedural snapshot `SampleHeights` / `SampleExistence` (1 pair) | Separate channel capabilities, output types and callbacks. Existing shared span/validity rules remain; query dispatch contracts differ. |
| Cutout/height `Deactivate` (1 pair) | Both delegate shared placement/registration lifecycle; height also disconnects its additional entity/configuration ownership. |
| Model `Publish` adapters (1 pair) | Shared publication lifecycle; snapshot layouts and validation/diagnostic fields differ. |
| Validation-message switches (4 pairs) | Independent enums and user-visible diagnostic mappings, retained verbatim. |
| `CommonStampDataEqual` / `MeshCutoutRegistrationsEqual` (1 pair) | Different registration field sets and nested snapshots; schema comparisons are not interchangeable. |

Additional policy checks:

- **Image numerics:** UV clamp and V flip, four neighbor order, double interpolation, and two-row evaluation remain intact. Raw IDs stay categorical, zero stays transparent, and blend values divide by 65535. Diagnostics retain the original ID A/B wording and zero-weight behavior. `HasCompleteImageBuffer` retains multiplication overflow protection.
- **Placement:** callers keep finite-transform/nonuniform-scale validation and diagnostic ordering. Yaw tolerance remains `1e-4`; rotated corners retain double intermediates, representability checks and outward float bounds. Symmetric image rectangle bounds retain their original arithmetic rather than changing to mesh-corner arithmetic.
- **Batch queries:** `GetValues` and `GetHeights` retain one query snapshot and their own empty/mismatched-span behavior. The shared helper receives that retained state, so it cannot reacquire a publication. Source calls, output sizes, counters, and notification ordering are preserved.
- **Model caches:** cutout advances its per-source ticket **before** signaling Loading; mesh height advances its global ticket **after** Loading. Generation is retired immediately on failure. Jobs keep weak source ownership, retain their model, and validate active/generation/ticket/model ID before completion. Each role keeps its revision counter, extraction, geometry validation, diagnostics and status mapping. Snapshot copies protect signal reentrancy. The new Loading test passed against the original caches before extraction and passes after extraction.
- **Ordering and status:** persistent IDs, deterministic keys, source priority, and tie ordering retain their original operations. Image validation still precedes image ordering diagnostics; mesh placement/model statuses retain their own precedence and text. Control-thread assertions remain at public boundaries.
- **Render ownership:** inclusive XY containment and first eligible owner per channel remain unchanged. Source acquisition and retained plan lifetime are not merged with ordinary query fallback. Collision admission still requires collision opt-in and matching grid/activation/revision; render and collision flags remain independent.
- **Resource handling:** dependency pointers are sorted with the same `std::less` ordering and deduplicated before lock acquisition. Sector IDs use independent atomic sequence domains and the existing ordering. Retained memory counts still include the same capacities, null behavior and allowances. Ordinary/shadow commit callbacks delegate to their existing common implementation; candidate selection, destruction and retirement remain distinct.
- **Geometry and shaders:** BVH escape traversal is shared, while self-intersection, distance acceptance and full parity counting remain policies. The distance comparison keeps its original NaN behavior. CPU/GPU triangle math, float versus double smooth curves, packed normals, edge ownership and gap topology retain independent numerical rules. No shader was changed.
- **Timing and hill kernels:** absolute monotonic timestamps share a helper; duration-of-interval timing remains separate to retain rounding. Cached/integer hill policies retain rejection/caching and power rules; reference/pruned reference share the original reference field. The original scalar code is frozen in the test-only oracle, with no production-helper calls. Existing scan/reference tests were not replaced by calls to the optimized implementation.

## Line accounting

Physical lines include blanks/comments and every new helper/header. Engine patches are counted as maintained patch text, not expanded generated engine files. Shaders/assets are reported separately. This measures maintained source size, not binary size or execution cost.

| Category | Baseline | After | Change |
| --- | ---: | ---: | ---: |
| Production C++ under `Code/Source` | 13,395 | 13,099 | -296 |
| Public/internal headers under `Code/Include` | 5,865 | 5,992 | +127 |
| C++ and headers subtotal | 19,260 | 19,091 | **-169** |
| Maintained engine patches/overrides | 4,017 | 4,012 | -5 |
| Shaders/assets | 1,074 | 1,074 | 0 |
| Production scope total | 24,351 | 24,177 | **-174** |
| C++ tests and test file lists (outside production scope) | 12,606 | 12,901 | +295 |

The test increase includes preservation of the independent procedural reference plus focused sampling/cache lifecycle coverage. Moving the original reference to a test oracle is not counted as deleting that maintained algorithm. Audit tooling, its unit tests, this document, and the explicit exception manifest are outside production scope and add maintenance lines; the production subtotal is not a claim that total repository text shrank.

## Validation

Validated on Windows x64, O3DE profile configuration:

- Built `Terrain.Static`, `TerrainCompositor.Static`, `TerrainCompositor.Tests`, `TerrainCompositor.Editor.Tests`, `TerrainCompositor`, and `TerrainCompositor.Editor` in the standalone consumer build. This compiles the maintained mesh manager and detail material manager overrides as engine code.
- **425 runtime tests passed**, with 4 existing disabled profiling tests left disabled. Includes new successful geometry/cache replacement tests for both model roles, the before/after Loading reentrancy tests, independent bitwise bilinear/degenerate-dimension checks, transformed-bounds checks, and procedural reference sweeps.
- **46 editor tests passed**, including typed reflection/removal, preview lifecycle, configuration, diagnostics, identity conversion and entity export.
- The actual D3D11 shader comparison passed on hardware: **142,560 mesh-gap boundary classifications, zero mismatches**. Normal/depth shader tests also passed.
- Engine override generation/negative hash checks, the exact duplicate gate and **7 Python audit tests passed**.

No public UUID, serialized field/version, default configuration, component service, or user-visible diagnostic text was changed. No live editor interaction was required for this source refactor; the editor lifecycle and serialization checks above use the real component classes.

The maintained patches were regenerated from the pinned engine inputs. `EngineOverrides.cmake` pins the new output hashes. `Code/Tests/EngineOverridesTests.cmake` validates successful generation and rejection of wrong input, wrong output, and wrong query-header contents. Generated build output is not checked in.

Local evidence is under ignored `build/issue11`: `audit-checked.json`, `audit-baseline-final.json`, `loc-before.json`, `loc-after.json`, `build-all.log`, `build-completion.log`, `runtime-final.xml`, `editor-final.xml`, and `override-tests-final.log`. The standalone build uses the installed O3DE engine and already-built engine dependencies; its copy steps warn that some dependency DLLs are absent from the standalone output directory. Tests therefore run the rebuilt standalone test DLLs with the matching engine runner/dependencies from the existing TG project on `PATH`.

### Engine DLL linkage correction

The initial validation built `Terrain.Static` but did not link `Terrain.dll`. The regular compositor tests link both static libraries and therefore masked the dependency introduced when the engine collider began calling `IsTerrainMeshHeightGapAdmitted`. The gap admission and identity predicates now have their single inline implementation in `TerrainExistenceSampling.h`, available to both modules without a reverse dependency on the compositor library. Their comparisons and admission behavior are unchanged.

`TerrainCompositor.EngineBoundary.Tests` links `Terrain.Static` and the test framework, deliberately excluding `TerrainCompositor.Static`. It calls the engine-facing collision helper with matching, mismatched, stale, and collision-only activations. This target catches a regression to an out-of-line compositor dependency at link time. Consumer validation must also include the `Terrain` DLL target, not just `Terrain.Static`.

The correction was verified in TG's Windows profile build: `Terrain`, `Terrain.Editor`, `TerrainCompositor`, `TerrainCompositor.Editor`, and `TGProject` all linked successfully with rebuilt static/object dependencies. All 425 runtime tests and the new engine-boundary test passed; the four existing profiling tests remain disabled. The generated boundary-test link inputs were also checked to exclude `TerrainCompositor.Static`. Logs and XML results are under TG's ignored `TGProject/build/windows/terrain-link-*.log`, `terrain-link-runtime.xml`, and `terrain-engine-boundary.xml`.
