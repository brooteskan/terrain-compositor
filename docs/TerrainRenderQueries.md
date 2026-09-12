# Retained render-query ownership and execution

The foundation design and verification below record behavior through `920d3f6`.
TG #37 now adds a gated ordinary-query elimination path; its current execution
contract and differential proof are documented in [TerrainQueryElimination.md](TerrainQueryElimination.md).
The ordinary path described below remains the fallback and comparison baseline.

`TerrainRenderQueryRequest`, `TerrainRenderQueryCapability`, and
`TerrainRenderQueryPlan` make the renderer's ordinary-query-then-overlay contract
explicit. Issue #3 adds opt-in immutable procedural-source acquisition to the
issue #2 ownership boundary. It does not eliminate ordinary queries or change
synchronous sector scheduling, and makes no frame-time improvement claim.

## Complete sector sampling plan (issue #5)

Production capture and preparation consume `TerrainSectorSamplingPlan`. It owns
the existing publication, source set, scene channel and invalidation tickets,
alongside regular/CLOD layouts and the captured area-existence decision. Source
entity/session/generation and composition identity remain in the retained source
set; no second procedural value cache is introduced.

The worker assesses both potential gathers before sampling. Each layout declares
height **and** existence, row-major XY construction, sampler, dimensions, spacing
and remapping, including every normal-halo row, column and corner at that gather's
own spacing. Corners participate in the existing any-terrain decision. CLOD is
conditional on regular data during execution, but always participates in complete
eligibility when configured. RT decodes packed regular vertices without additional
source samples. Captured empty areas retain the full ownership assessment.

Planning uses inclusive first-match ownership for every required position.
Scalar mode assesses one explicit position per callback; batch mode preserves
the complete grid and contiguous ownership runs. Planning never acquires, probes
or samples a provider. Temporary planning positions are discarded; execution
still uses the ordinary callbacks' actual coordinates and Z and resolves those
positions before overlay. Callback order, sizes, scalar/batch dispatch and source
call counts therefore keep their existing meaning.

`CanAvoidOrdinaryResults()` and `CanExecuteAcrossFrames()` describe whole-request
eligibility, separately from the preserved `OrdinaryThenOverlay` and
`Synchronous` execution policies. `TerrainRenderQueryPlan` still requires ordinary
results. Partial height/existence ownership never becomes whole-sector eligibility.

### Proofs required before changing policy

Direct-XY sampler support does not establish TerrainSystem equivalence. The
separate per-channel `TerrainRenderOrdinaryEquivalence` defaults to no proof.
A future ordinary-query elimination path must prove:

- Identical coordinates, including grid construction and float rounding, and
  equivalent interpolation/clamping for each opted-in sampler.
- Identical final rendered heights/existence after composition, world-height
  clamping, image holes and unavailable-provider behavior.
- Equivalent collision-only fallback semantics, including ordinary surface Z
  consumed by external existence masks. Independent height alone is insufficient.
- Complete regular/CLOD and normal-halo coverage, preserving packed vertices,
  slopes, CLOD hole fallback, RT decode and bounds.

Production adapters declare none of these equivalence proofs. The composition
adapter clears any such declaration from a normalized procedural source, which
cannot certify final composed output. Legacy, unsupported sampler/grid/count,
unknown/live/unavailable sources, Z dependence, unowned points and split owners
all retain explicit fallback reasons. Configured masks also report `ExternalMask`.

Across-frame eligibility additionally requires retained area-decision and ordinary
query-service lifetimes. The enabled path still calls ordinary `QueryRegion`;
equivalent retained overlays do not retain that live service.
The current `TerrainAreaExistsInBounds` result has captured invalidation tickets,
but no promise to retain its external providers across frames. Even fully proven
synthetic gathers therefore remain ineligible with the current area contract.
Assessment reports missing/stale tickets, source-set/publication mismatch and
retired/replaced scenes without recapturing tickets. This is an observation, not
an admission lease: acceptance still revalidates under publication/dependency
locks and rejects changes during or after preparation. Deferred execution also
needs admission, cancellation, memory/fairness budgets and coverage policy.

Result-owned `TerrainSectorSampling` diagnostics report total required samples,
conditional CLOD samples, both-owned samples, complete eligibility and separate
ordinary/across-frame fallback masks. Reason bits follow `TerrainRenderFallback`
order; `PreservedPolicy` is excluded from eligibility. Existing executed-query
counters retain their meaning; there is no per-sample logging. This additional
planning is architectural work, not measured query elimination or a camera-flight
speedup. Performance acceptance remains TG #37.

## Request and ownership

A request describes world XY positions with **ordinary terrain surface Z**, the
ordinary sampler (exact, clamp, or bilinear), explicit positions or a regular grid
(start, spacing, width, height), and whether bulk callbacks are enabled. Positions
are borrowed for synchronous resolution and execution; their storage and XY must
remain unchanged. [Owned sector requests](TerrainSectorPreparation.md) retain the
publication and own preparation inputs; cross-frame scheduling remains deferred.

`ResolveTerrainRenderQuery` retains the exact render publication. It selects the
first matching height callback and the first matching existence callback
independently, using inclusive XY bounds and the publication's existing order.
Region Z never determines ownership. Contiguous equal-owner runs retain both
owners, their sample ranges, callback dispatch, and fallback reason bits. The
owners carry the registry's composition session/revision and declared source
entity identity (which is informational, not an external-source lifetime lease).
The first run is stored inline so a scalar query or a single-owner sector does not
allocate a run list. Resolution does not call or probe a source.

Registry publication still copies value-owned queries and preserves its existing
admission/order: an entry requires an existence callback to enter the scene
aggregation. This slice does not newly admit formerly ignored height-only
registrations. Plans also handle height-only entries in caller-provided snapshots
and height ownership split from an earlier existence-only provider. Legacy
scalar callbacks and the raw-snapshot `ApplyTerrainRenderGeometry` API remain
available. New renderer work explicitly captures `TerrainRenderQuerySources`
before querying ordinary terrain, then supplies it to the resolver and executor.
Resolution without a source set preserves the original live-query behavior and
does not acquire sources. A source set from a different publication is rejected.

## Procedural-source snapshots

`TerrainProceduralSnapshotRequestBus` is an optional, entity-addressed shared
dispatch bus. `AcquireTerrainSnapshot` returns a `shared_ptr<const
TerrainProceduralSnapshot>` or null on acquisition failure. Its contract requires
value-owned callbacks, source entity identity, an activation UUID, and a generation
ticket captured atomically with the values. An entity ID, composition pointer,
callback owner, or invalidation authority alone does not satisfy this contract.
Callbacks must remain safe after provider destruction. The final reference
retires the values; retirement of the provider invalidates acceptance, not reads.

The built-in procedural component copies its query configuration once under the
same lock used for configuration publication. The height kernel retains that copy
and evaluates the existing noise function without buses or component access.
Height changes invalidate its authority before publishing new values. Removal
retires that authority before disconnecting; reactivation, including reuse of the
same entity ID, creates a new UUID and authority. Old snapshots remain readable
but their original tickets cannot admit resource commits. Tint-only changes do
not invalidate numerical terrain results. Existing serialized fields and UUIDs
are unchanged.

Height and existence each declare their own source availability, input-Z and
ordinary-result dependence, explicit/regular-grid support, sampler support, sample
counts, and coordinate range. For the built-in supported channels:

- XY is finite world space, with `abs(X)` and `abs(Y)` at most `1e12` meters. This
  bound keeps the density-scaled integer noise cells and neighbors in range.
  Z is ignored. Heights use the same parameter and normalized-result clamps as
  live height queries, followed by existing composition/world mapping.
- Scalar and batch sampling are equivalent. Empty batches succeed; mismatched
  spans and unsupported positions return false without modifying any output.
  Counts from zero through the representable span size are supported, including
  255/256/257; no snapshot dispatch chunking is introduced.
- Exact, clamp, and bilinear mean direct evaluation at the supplied XY after the
  ordinary sampler produces those positions. The snapshot does **not** promise to
  reproduce TerrainSystem's grid interpolation or collision fallback.
- Finite height settings are required for height acquisition. Existence is
  retained as `true` only when no procedural mask is configured, independently of
  height availability. Image-hole stamps are composed afterward as before.

Every configured procedural mask remains a live existence dependency, including
missing, cyclic, disabled-looking, or invalid-threshold masks. Its sampler,
threshold, Z input, transforms, and transitive dependencies do not inherit the
height kernel's guarantees. Height may still use its retained snapshot, but
existence keeps the original fail-open, hierarchy, and reentrancy behavior. A mask
change or provider change invalidates pending requests through the composition's
existing notification authority. General retained mask-chain support is deferred.

## Capability and fallback

The capability declares supported coordinates, sampler/grid forms, sample-count
ranges, and separate height/existence source and input-Z dependencies. These are
guarantees for ownership decisions, not new restrictions on existing overlay
callbacks. A bulk callback, a shared coordinator, or an immutable composition
pointer supplies no implicit independence guarantee.

Unacquired and unsupported composition channels declare live external sources,
ordinary surface Z input, and required ordinary results. Acquisition guards
invalid identities, missing providers, cycles, reentrant calls, null results, and
malformed identity/ticket contracts. Acquisition rejects entry from any active
gradient, existence, or snapshot dispatch; legacy overlay reentrancy rules remain
unchanged. Legacy providers are not asked to implement
the new bus. Retained callbacks replace only supported channels; unsupported
requests use the original callbacks and ordering. Missing height sources still
produce the region minimum and suppress stamps; missing existence sources still
fail open and apply image holes.

Fallback reasons distinguish missing channel owners, split owners, undeclared
legacy capabilities, unsupported requests, unknown/live/unavailable sources,
input-Z dependence, ordinary-result dependence, and the preserved execution
policy. Even an explicitly independent supported owner retains the final policy
reason: **every request still requires ordinary terrain results**. Multiple
reasons may apply to one sample.

## Renderer execution

`GatherMeshData` retains one publication for its request and performs exactly one
ordinary `QueryRegion` with the original grid and sampler. With batching enabled,
it captures those surface positions and resolves/executes the overlay after the
ordinary callback stream finishes. Incomplete ordinary batches return before any
retained callback. With batching disabled, it resolves one sample and executes
existence followed by height inside the original ordinary callback. Scalar
callbacks are never silently replaced by bulk calls in that mode.

The executor leaves unowned channels untouched. A run with the same owner for
both channels and a bulk callback receives one bulk invocation with the original
run size. Other runs invoke existence, then height, for each point. Unsupported
capabilities keep this legacy dispatch; there is no callback regrouping or
256-point chunking introduced here. Source calls inside composition batches
remain height-before-existence, and existence still sees ordinary surface Z,
including collision-only hole fallback Z. Image holes determine render topology;
mesh cutouts and mesh-height gaps retain their separate render treatment.
Height callbacks return absolute world-space Z; normalized composition mapping
remains inside the callback and the final world-height clamp remains in the renderer.

The intended source-dispatch change is that supported channels evaluate their
captured kernels without per-sample or per-batch gradient/existence/hierarchy bus
calls. Acquisition performs one guarded source request per composition per owned
sector, before ordinary queries. A partially unsupported bulk run falls back as
one original bulk invocation, preserving source order and callback size. Scalar
and independently owned channels can fall back separately. Ordinary queries and
configured live-mask dispatch are retained.

## Classification before ordinary queries

`WorldXY` describes positions whose Z is not yet available. A channel accepts that
form only with an explicit retained sampling declaration and Z independence.
The regular-grid descriptor must describe the entire supplied position span.
Ownership remains first-match inclusive XY, with independent height/existence
owners. Source acquisition itself grants no permission to skip ordinary queries.

A later query-elimination decision must enumerate **all** regular and CLOD XY
samples and each gather's one-sample normal halo at its own spacing. It must prove
both channels supported at every position, including split owners, inclusive
region edges, holes, sample-count limits, sampler/grid semantics, and source
availability. A regular interior grid or one retained height owner is insufficient.
The built-in direct-XY overlay contract must also be distinguished from ordinary
terrain-grid reconstruction. This change provides the owned source sets and
per-channel guarantees for that analysis; `RequiresOrdinaryResults()` still always
returns true. Cross-frame scheduling additionally requires a retained contract for
remaining ordinary/live dependencies and the existing acceptance checks.

Clamping, halo normals, vertex remapping, CLOD inputs/interpolation, ray-tracing
decoding, packed vertices, publication validation, and uploads retain their
existing implementation. Only maintained engine patches are edited; generated
output hashes and rejection tests are updated together.

## Diagnostics and verification

Stable profiler scopes are `Terrain::RenderQuery::ResolveOwnership`,
`Terrain::RenderQuery::ExecuteOverlay`, and the existing
`Terrain::GatherMeshData`, `QueryRegion`, and `RetainedGeometryBatch` scopes.
An optional `TerrainRenderQueryStatistics` belongs to the synchronous request's
caller. Its pointer on `SectorDataRequest` accumulates ordinary samples and total
preparation time. Enabling the existing `r_terrainSectorUpdateTiming` allocates
one accumulator per pending sector, shares it between regular/CLOD gathers, and
prints `TerrainRenderQuery` records on the control thread after completion and
publication-lock release. Fallback entries are `enum-index:sample-count` in the
order listed above. Disabled timing allocates no accumulators. Resolution/execution record channel coverage, explicitly
independent coverage, overlapping fallback buckets, retained scalar/batch samples,
callback calls, and stage time. The compositor's optional adapter counts actual
height, existence, and hierarchy bus calls and unavailable-source fallback
samples through a thread-local scope in the callback's module. Nested scopes
restore the previous accumulator. No shared atomics or per-sample logs are added.

Result-owned diagnostics additionally count supported height/existence snapshot
samples and acquisition outcomes per channel. Provenance records contain source
entity, activation UUID, generation, and separate height/existence outcomes.
`TerrainRenderSource` prints these once per captured query on the control thread;
`TerrainRenderQuery` includes `snapshot-height` and `snapshot-existence` coverage.
Acquisition outcomes follow `TerrainSourceAcquisition`: NotRequested, Acquired,
UnsupportedProvider, MissingProvider, InvalidIdentity, Reentrant, Cyclic, Rejected,
InvalidConfiguration, ExternalMask. Counts describe channels, not samples;
unsupported request coverage uses the existing fallback buckets. Source-call
counters describe overlay execution; acquisition outcomes are reported separately.
Disabled statistics create no provenance vectors or accumulators.

Differential tests compare the retained executor with the unchanged legacy
overlay, including callback traces, inclusive overlap edges, unowned and split
channels, scalar-only owners, 0/1/255/256/257 points, malformed spans, unsupported
requests, live source disappearance, reentrancy, and publication replacement.
Composition tests retain Z-dependent existence and image-hole/collision
separation coverage. Actual `GatherMeshData` tests compare all three ordinary
samplers, clamped and packed heights, halo normals, remapping, and regular/CLOD
inputs against a legacy-overlay oracle. Existing public-existence, grid,
ray-tracing, lifecycle, GPU, and engine-override tests provide adjacent coverage.

`TerrainRenderGeometryBatchTests.DISABLED_ProfileOwnershipPlanAgainstLegacyOverlay`
is an opt-in matched CPU comparison with the real procedural component, regular
and CLOD halos, 10/20/30-sector workloads, warmups, and alternating execution
order. It reports medians without a timing assertion. It measures ownership-plan
overhead, not camera-frame performance or an ordinary-query-elimination speedup.

### Issue #3 verification on 2026-09-11

- Built `Terrain.Static`, `TerrainCompositor.Static`, both terrain modules, both
  compositor modules, and both compositor test modules with MSVC 19.51, profile.
- **345 runtime tests and 46 editor tests passed** with clean process exits.
  The 32-test snapshot/sector-lifetime subset also passed. The two opt-in
  benchmarks remain disabled. D3D11 hardware compared **142,560 boundary
  classifications with zero mismatches**; this checks the gap predicate, not the
  complete terrain shader pipeline.
- Maintained override generation, repeat generation, and incorrect input/output
  hash rejection passed. All **153 implementation/test/build source files** and
  **16 shader/material assets** matched TG's temporary candidate inputs after
  normalizing line endings. Generated overrides and linked module hashes were
  recorded in the local verification manifest.
- The final Editor opened DefaultLevel, captured a DX12 frame, and exited with
  code **0**. Its optional diagnostics produced **1,282 query/source records**.
  A fully owned regular+CLOD sector covered 21,650 height and existence samples
  using snapshots, with two geometry callbacks and zero overlay height,
  existence, or hierarchy source-bus dispatches. The 21,650 ordinary samples
  were still queried. This is execution evidence, not a timing comparison.
- Batching, CLOD configuration, and ray tracing were preserved; diagnostic timing
  was restored to false. All **62 hashed authored files** were unchanged.

The first candidate Editor launch exposed missing shader-builder runtime files.
Building `AtomShader.Builders` staged the compiler tools and platform headers.
Reprocessing then exposed an existing void-return depth/shadow output-reflection
error. The shader now uses Atom's clipped-depth output pattern. Final processing
of depth, shadow, material type, and material succeeded: **four assets, zero
failed assets, zero errors**, with six warnings about the existing unresolved
`TGProject::HeightmapStampIdentityProcessor` serialization type. The Editor still
logged two DynamicDrawContext pipeline-state warnings during shutdown. This is
not a warning-free run or multi-LOD visual/performance acceptance.

Provenance: compositor implementation baseline and original TG compositor pin
`894ad31e43335436f8b36e1a3ce647bad5f14b1e`; TG
`6db89b4dcc60cf690c6008e94572f30016180702`; O3DE
`061180bf24f1666eb30315b35da292eb14f4659c`. The candidate used
`D:/TG/TGProject/build/render-query-candidate`, with unchanged engine dependencies
copied from the existing reference build and the affected targets rebuilt from
the synchronized compositor inputs. `D:/TG/TGProject/build/windows` was preserved.
The candidate's source digest, generated override hashes, binary hashes, test XML,
asset logs, frame capture, and authored-file hashes are retained locally under
`build/procedural-snapshot-session`. TG's integration pin remains separate from
this implementation; camera-flight performance acceptance remains TG #37 work.

### Historical issue #2 verification on 2026-09-11

- Built `Terrain.Static`, `Terrain`, `Terrain.Editor`, `TerrainCompositor.Static`,
  both compositor modules, and both test modules in MSVC 19.51 **profile**.
- **312 runtime tests and 46 editor tests passed.** The two opt-in benchmarks are
  disabled in the normal suite. D3D11 hardware verification compared **142,560
  boundary classifications with zero mismatches**. Engine-override valid/repeat
  generation and incorrect input/output hash rejection passed.
- The final Editor opened DefaultLevel and exited with code 0. Enabling sector
  timing produced **1,000 result records**. A fully owned regular+CLOD sector
  reported 21,650 ordinary and retained samples, two geometry callbacks, two
  height and two existence source dispatches, four hierarchy dispatches, and zero
  independent samples. Its fallback buckets were live source, input Z, ordinary
  dependency, and preserved policy. Batching and ray tracing remained true;
  timing was restored to false. All **62 hashed authored files were unchanged**.
- Matched all **148 implementation/test source files** between the standalone
  checkout and TG's submodule, normalizing line endings, and verified generated
  overrides and linked candidate module hashes.

The CPU comparison used the default real procedural component, original
131×131 regular and 67×67 CLOD sample sets, two warmup iterations, nine measured
iterations with alternating order, and three separate runs after builds and the
Editor had stopped. The table reports the median of the three run medians.

| Sectors | Legacy overlay | Ownership plan | Difference |
| --- | ---: | ---: | ---: |
| 10 | 86.467 ms | 88.357 ms | +2.2% |
| 20 | 173.568 ms | 178.791 ms | +3.0% |
| 30 | 254.130 ms | 260.718 ms | +2.6% |

This records a small retained-query CPU cost for the boundary; no speedup is
claimed. It excludes ordinary-query and frame-distribution measurements. Camera
flight acceptance and later query-elimination/scheduling performance targets
remain separate work.

Environment: Ryzen 7 2700 (8 cores/16 threads), 48 GiB installed memory, Radeon
RX 7900 XTX (driver 32.0.31041.1004), Windows 11 Pro 10.0.26200. TG was
`369f003348cce09157ae7be9faa75519b6fd141f`; its original submodule pin was
`e2281e2d39b6a7901a1ef6ad7a6d85cd070f96e1`. The implementation starts from
compositor `ae2cad020a180e9695bf49cdb548da087b8a5bde` and uses O3DE
`061180bf24f1666eb30315b35da292eb14f4659c`.

The existing `D:/TG/TGProject/build/windows` build was preserved. The candidate
used `D:/TG/TGProject/build/render-query-candidate`, configured through TG's
submodule, with copied unchanged engine dependencies and rebuilt affected
targets. Generated overrides are under `External/o3de-0dac79bc/EngineOverrides`;
the maintained SHA-256 outputs are
`70fbbf3c7d0418f1c53a3282102920cef61153b5725277b9987fbb83353db182` (CPP) and
`de7db29d6ac3180012d00fc33dcfa7f0d3003b350872de709150894f614e3d99` (header).
Raw XML, timings, Editor diagnostics, input hashes, and the verification manifest
are retained locally under `build/render-query-session` in the compositor checkout.
