# Retained render-query ownership and execution

`TerrainRenderQueryRequest`, `TerrainRenderQueryCapability`, and
`TerrainRenderQueryPlan` make the renderer's ordinary-query-then-overlay contract
explicit. This implements section 1 of issue #2. It does not eliminate ordinary
queries or change synchronous sector scheduling.

## Request and ownership

A request describes world XY positions with **ordinary terrain surface Z**, the
ordinary sampler (exact, clamp, or bilinear), explicit positions or a regular grid
(start, spacing, width, height), and whether bulk callbacks are enabled. Positions
are borrowed for synchronous resolution and execution; their storage and XY must
remain unchanged. Owning cross-frame sector inputs is separate work in section 2.

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
available. New renderer work uses the retained plan and executor.

## Capability and fallback

The capability declares supported coordinates, sampler/grid forms, sample-count
ranges, and separate height/existence source and input-Z dependencies. These are
guarantees for ownership decisions, not new restrictions on existing overlay
callbacks. A bulk callback, a shared coordinator, or an immutable composition
pointer supplies no implicit independence guarantee.

Composition callbacks explicitly declare live external sources, ordinary surface
Z input, and required ordinary results for both channels. The composition,
stamps, and callbacks are retained; procedural gradient and existence handlers
remain live bus calls. Handler availability, cyclic hierarchy, and reentrant bus
use are evaluated at the same points as before. Missing height sources still
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

### Verification on 2026-09-11

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
