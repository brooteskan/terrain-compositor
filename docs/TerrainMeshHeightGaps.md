# Terrain mesh height gaps

This document records the authoring, immutable publication, and GPU masking
contract for terrain mesh height stamps with intentionally uncovered cells
(phases one through four).

## Authored domain

A stamp is a regular Cartesian grid of XY sample points. Every point in the
rectangle established by the distinct X and Y coordinates must be present,
although FBX processing may produce several render vertices at one XY point
because of normal, tangent, UV, material, or mesh-section splits. Those render
vertices are welded by XY during height-data construction and must agree on Z.

Each grid cell must contain either:

- no triangles, which is an authored uncovered cell; or
- exactly one complementary triangle pair using either cell diagonal.

An incomplete pair, duplicate or overlapping projected triangle, conflicting
height, vertical or degenerate face, nonadjacent triangle, nonfinite position,
invalid index, irregular spacing, or missing Cartesian point invalidates the
entire stamp. The builder publishes no partial height data and retains at most
eight offending point/cell/triangle details while counting every detected
topology offense.

Cell ownership is half-open at interior grid boundaries: a point on a boundary
belongs to the cell on the positive-X or positive-Y side. The exact maximum X
or Y domain edge belongs to the last cell. Points outside the proven rectangle
are neutral.

## Uncovered-area policy

`Preserve Lower Terrain` is the default and the migration result for serialized
version-one and version-two stamps. `Cut Out Terrain` records author intent.
Under either policy, an uncovered cell contributes no mesh height. The
rendering and collision/query cutout effects obey the independent
`Affect Terrain Rendering` and `Affect Terrain Collision / Queries` flags.
Rendering-enabled gaps require successful GPU admission before their coupled
CPU removal becomes eligible. Collision/query-only gaps do not require a renderer.

Strength, Feather, and Relative Edge Blend affect covered-cell height only.
Edge Inset crops the stamp's height contribution; it neither creates holes nor
changes the stored uncovered-cell classification.

## GPU layout and sampling

Each rendering instance has a 96-byte, 16-byte-aligned structured descriptor:
world XY rejection bounds, translation and yaw, local grid origin and inverse
spacing, inverse uniform scale and authored domain maximum, cell dimensions,
mask/tile offsets, tile width, and the full 64-bit asset revision. Layout is
shared with `TerrainMaterialSrg::MeshHeightGap`.

One immutable word buffer contains each distinct prepared asset revision once.
Cells are row-major (`y * cellWidth + x`), packed 32 per uint32, low bit first.
Unused tail bits are zero. Tile summaries cover 8 by 8 cells, including partial
edge tiles. Sixteen two-bit summaries share a uint32: 0 covered, 1 mixed,
2 uncovered. Outside bounds/domain and covered tiles reject immediately;
fully uncovered tiles accept immediately; only mixed tiles read a cell bit.
Instances sharing the same immutable height-data pointer share offsets. A move,
yaw, scale, priority change, or duplicate instance reuses the word buffer when
the unique asset set is unchanged. Identical descriptor tables are also reused.

CPU exact-gap/height sampling and the shader use an explicitly rounded float
world-to-local transform and reciprocal grid mapping. Shader `precise`
intermediates prohibit fused multiply-add contraction. Domain comparisons occur
before multiplication by the reciprocal, and final cells clamp at the authored
maximum; no tolerance expands holes outside the domain. Cell ownership is
defined in these common float grid coordinates, including at transformed edges.
World-bound comparisons use IEEE bit ordering in the shader so Direct3D's
subnormal flushing cannot admit the float immediately outside a zero boundary.
Nonfinite/unrepresentable descriptors are rejected, never partially uploaded.

The shared fragment predicate is included by forward and depth rendering;
shadow rendering uses the depth shader. Mesh-height gaps form a remove-only
union after the ordered closed-mesh Remove/Restore result, so a closed-mesh
Restore cannot refill a gap. GPU evaluation may early-discard that union before
performing the more expensive closed-mesh triangle work.

## Exact queries, conservative collision, and raycasts

Logical height/existence and surface queries continue to classify the exact
authored uncovered cell. Collision uses a separate immutable sparse cell view.
On the control thread, every uncovered authored rectangle is transformed by the
supported translation, yaw, and uniform scale, then tested against candidate
terrain heightfield cells with a separating-axis intersection test. Every
intersecting heightfield cell is stored as a sorted global integer cell address.
This covers rotated stamps, unequal authored X/Y spacing, and openings smaller
than the live terrain spacing without widening the logical query or render mask.

The prepared collision view records mesh revision, live grid spacing, terrain
region bounds, conservative world bounds, and conservative cell count. The
authored data separately retains exact uncovered bounds and uncovered-cell
count. Composition republishes the view when the asset, transform,
configuration, contributor set, terrain region, or live grid spacing changes.
Preparation is capped at 4,194,304 cells per gap. If a collision-enabled gap in
a valid live terrain region cannot prepare a complete view, that gap is not
published with partial or exact-only collision behavior.

The project-owned terrain physics collider retains one scene snapshot and GPU
admission activation for each asynchronous heightfield update. Its worker
callback performs allocation-free binary lookup of the prepared address and
emits `QuadMeshType::Hole` for each removed cell. Rendering-enabled gaps require
the matching admitted scene revision; collision/query-only gaps are independent
of Atom. Replacement mesh colliders remain separate scene geometry and are not
modified by terrain heightfield removal.

The project-owned terrain raycast checks exact existence at each triangle's
actual candidate intersection. It evaluates both triangles independently,
selects the nearest existing candidate, and continues grid traversal when a
cell's candidates are removed. A missing high-quality terrain normal leaves the
valid triangle normal intact. Public terrain APIs can still return a numeric
fallback height for nonexistent terrain, so callers and tests use the existence
result as validity. The composition surface provider publishes no weights at an
exact logical removal.

The acceptance terrain region must set `UseGroundPlane = false`; otherwise the
engine's ground-plane fallback can manufacture terrain where the provider
reports no data. `Levels/DefaultLevel/DefaultLevel.prefab` has this setting.
The raycast and physics copies are based on O3DE revision
`061180bf24f1666eb30315b35da292eb14f4659c`. CMake replaces the matching stock
sources in `Terrain.Static` and stops configuration if any expected upstream
source is absent, keeping the integration reproducible without changing the
external engine checkout.

## Publication and failure behavior

The scene registry publishes immutable snapshots. Scalar and batched CPU
queries retain an immutable activation snapshot once per request, not a mutable
per-sample ready bit. Identity includes composition lifetime, entity, retained
asset revision, placement, and rendering eligibility. A material/GPU allocation
or binding failure leaves newly rendering-enabled gaps neutral for coupled CPU
removal. Existing captured batches finish against their retained activation;
new batches observe the replacement. Rendering-only gaps never remove CPU
terrain; collision/query-only gaps remain independent of GPU admission.

The feature processor prepares/binds resources at begin-prepare-render and
acknowledges admission only at render end, after the material and SRG have
compiled and only if the exact scene publication/material still matches.
An activation change queues collision and surface invalidation on the next
control tick. Failed allocations are not retried every frame; a new publication
or material replacement permits a retry. Empty tables use persistent dummy
buffers with zero count. Deactivation clears eligibility, bindings, and owned
resources; old readers retain only their own immutable snapshots.

Terrain sector jobs retain one scene snapshot for both regular and CLOD
sampling. Results are staged until jobs finish; a publication-locked generation
check rejects stale results and requests a rebuild instead of uploading them.
The retained render query supplies both composed height and image-only
existence, preventing collision holes (including the engine's ground-plane
fallback) from changing render vertices or normals. Ordinary authored image
holes remain render-topology holes. Each scene receives a unique terrain
material/SRG so one scene cannot overwrite another scene's mask bindings.

Initial safety limits are 256 render instances, 256 unique masks, and 16 MiB of
combined packed mask/tile storage. These are bounded defaults, not measured
performance targets. Rejections follow stable scene publication order. Rejected
rendering-enabled contributors also lose coupled CPU removal; collision-only
contributors are not counted against GPU limits. Statistics distinguish active
instances, unique masks, mask/tile bytes, descriptor/mask uploads, activated
revision, resource rejections/failures, and stale activations. No publication
change means no geometry/mask upload.

## Verification and remaining acceptance work

`TerrainMeshHeightGapGpuTests` covers packing, tail bits, multiword tile summaries,
sharing/reuse, reload, empty state, deterministic limits, independent flags,
immutable admission, stale publication/removal/scene rejection, and retained
render height. On Windows it also compiles the checked-in predicate and SRG
descriptor as a D3D11 compute shader (hardware, with WARP fallback), then compares
CPU results at translated/yawed/scaled boundaries and neighboring floats.
This is numerical shader verification, not a substitute for an end-to-end
terrain framebuffer test. Asset Processor must separately compile the actual
AZSL forward/depth/shadow products.

Phase four adds public scalar/batch height and surface queries, vertical and
oblique raycasts, both-triangle selection, later-hit traversal, boundary cases,
sub-grid conservative cell preparation, grid-spacing rebuilds, immutable
admission, and the actual physics heightfield provider callback to the focused
suite. Phase five's complete authored acceptance scene remains a separate
follow-up. Multi-LOD color/depth/shadow captures, live collider rebuild timing,
allocation-failure injection in a live scene, and repeated editor/game/reload
acceptance should be recorded there; the unit/compute tests do not claim to
verify those behaviors.

### Local validation, 2026-09-08

- Profile builds succeeded for `TGProject`, `TGProject.Editor`, `TGProject.Tests`,
  `Terrain`, and `Terrain.Editor`.
- Forty-eight focused terrain tests passed with a clean process exit. They
  include public scalar/batch height and surface queries, vertical/oblique/
  boundary/later-hit raycasts, conservative collision preparation, the actual
  heightfield-provider callback, and independent admission behavior. The
  hardware D3D11 test compared 142,560 classifications with zero mismatches.
- Asset Processor processed the forward, depth, shadow, material-type, and
  default-material assets: five successful, zero failed. Its summary included
  warnings/errors, including builder-disconnection diagnostics during shutdown;
  this is not a claim of a warning-free Asset Processor run.
- The full 92-test CTest invocation still fails in existing allocator/reflection
  coverage (`TerrainFollowCameraComponentTests` and `TankDriveSystemComponentTests`).
  The legacy mesh-height configuration test passes its assertions in isolation
  but its process crashes on shutdown, so it is excluded from the clean 40-test
  result. These test-infrastructure issues were not changed by this implementation.

Reproduce the focused run using `AzTestRunner TGProject.Tests.dll AzRunUnitTests`
with filter `TerrainMeshHeight*:TerrainMeshCutout*:TerrainRaycastExistence*:`
`TerrainPublicExistenceTests.*:TerrainPhysicsColliderGapTests.*-`
`TerrainMeshHeightConfigurationTests.Legacy*`.
Local build/test/asset logs are under `build/windows/gap-*.log` (not source controlled).

### Camera-flight sector-update optimization

Retained render queries now batch composed heights and image-only existence over
contiguous samples owned by the same composition. The ordinary terrain query is
still performed first: unowned samples, scalar-only providers, independently
owned height/existence, and overlapping region boundaries preserve their existing
fallback semantics. This removes per-vertex source-bus dispatch and configuration
locking from the retained query; it does not eliminate the ordinary height query.
The procedural source also batches its optional hole-mask sampling, retaining
one configuration per batch and using bounded scratch storage.

CLOD interpolation and ray-tracing position/normal decoding now run in the sector
workers. Workers only prepare CPU data. GPU buffer updates and ray-tracing mesh
registration remain behind the publication-locked generation check. The staged
expanded data consumes additional temporary memory until that sector batch commits.

For a same-route editor comparison, use these console settings:

- `r_terrainSectorUpdateTiming true` logs sector count, total update time,
  preparation/worker wait, publication-lock wait, and upload time. It is off by
  default and only logs when sectors change. Logging itself adds overhead.
- `r_terrainBatchRenderQueries false` selects the legacy scalar retained-query
  path; `true` restores the optimized default. This isolates query batching, not
  the CLOD/ray-tracing worker move. Both paths retain generation checks.
- Fly the same warmed-up route at the same speed in each mode, avoiding terrain
  edits and asset processing. Compare batches with similar sector counts and
  record frame-time spikes as well as sector-update timings. Restore batching to
  `true` and timing to `false` afterward.

CPU profiler scopes include `Terrain::ProcessSectorUpdates`,
`Terrain::PrepareSectorWorker`, `Terrain::GatherMeshData::QueryRegion`,
`Terrain::GatherMeshData::RetainedGeometryBatch`, `Terrain::PrepareSectorLodData`,
`Terrain::PrepareSectorRayTracingData`, `Terrain::WaitForSectorWorkers`, and
`Terrain::CommitSectorUploads`.

Regression coverage includes scalar/batch composition parity, authored holes,
collision-cutout independence, source fallback, overlapping XY ownership, retained
snapshot replacement/removal, CLOD remapping/hole fallback, and ray-tracing decode.
The opt-in `TerrainRenderGeometryBatchTests.DISABLED_ProfileRetainedSectorQueries`
benchmark uses the actual procedural source and retained-query factory with
21,650 regular/CLOD halo samples. Run it with `--gtest_also_run_disabled_tests`
and its exact `--gtest_filter`. It compares CPU query times, not editor frame times.

Local optimization validation (2026-09-08):

- All five profile targets (`TGProject`, `TGProject.Editor`, `TGProject.Tests`,
  `Terrain`, `Terrain.Editor`) built successfully. Existing MSBuild intermediate
  directory warnings remain.
- 55 focused tests passed with a clean exit, including the missing-provider
  shutdown guard and 142,560 hardware shader classifications with zero mismatches.
  The previously documented legacy/reflection exclusions still apply; the full
  suite was not rerun for this optimization.
- Three benchmark runs measured scalar medians of 14.702 / 14.434 / 14.364 ms
  and batch medians of 8.509 / 8.359 / 8.295 ms: approximately 1.73x throughput
  (42% less retained-query CPU time), with identical height outputs. Each run
  discarded two warmups, then computed each median from nine measurements per
  path with alternating execution order. This excludes ordinary terrain queries,
  CLOD/ray-tracing preparation, uploads, and real editor frame timing.
- Build, test, and benchmark logs are under `build/windows/terrain-stutter-*.log`.
  A same-route live editor flight remains necessary to confirm the stutter impact.

Issue #37's subsequent live measurements, signed sector-index correction,
replay scripts, and remaining performance work are recorded in
[TerrainCameraFlightProfiling.md](TerrainCameraFlightProfiling.md).

## Asset Processor verification

The included probes are 5-by-5 Blender grids exported as FBX:

- `Assets/FBX/MeshHeightAdjacentGaps.fbx` omits two adjacent interior cells.
  O3DE Asset Processor produced 28 faces and a final Atom position stream with
  79 render-split vertices representing all 25 unique XY grid points. This is a
  complete Cartesian domain with two valid uncovered cells.
- `Assets/FBX/MeshHeightBoundaryGap.fbx` omits the upper corner boundary cell.
  Asset Processor produced 30 faces and a final Atom position stream with 81
  render-split vertices but only 24 unique XY grid points. The unreferenced
  corner was discarded, so the processed model does not prove a 5-by-5 domain
  and must report `MissingGridPoint` instead of fabricating its height.

These results were recorded from Blender 5.0.1 and the project's profile Asset
Processor using `--platforms=pc --debugOutput`. The `.dbgsg.xml` products were
used for face counts, and the uncompressed final `POSITION0` Atom buffers were
inspected for unique XY coordinates. Cache products are generated and are not
source-controlled.
