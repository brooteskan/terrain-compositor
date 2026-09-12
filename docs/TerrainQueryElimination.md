# Retained-only sector queries (TG #37)

The renderer skips the ordinary `TerrainSystem::QueryRegion` for completely
certified sectors. `r_terrainRetainedOnlyQueries` defaults to true; false retains
the ordinary-query-then-overlay reference path for matched measurements.
Scheduling and whole-group GPU publication remain synchronous.

## Eligibility and numerical contract

The first supported case is the built-in `ProceduralGround` snapshot with valid
height configuration and no configured external existence mask, using the
renderer’s CLAMP sampler. The composition adapter issues its own final-output
certificate; it never copies a normalized source’s equivalence flags. Custom
kernel identities, live/unavailable/Z-dependent channels, unsupported samplers,
partial or split ownership, invalid layouts and stale publications/dependencies
retain the ordinary path. Every required point in both potential regular and
CLOD gathers, including each gather’s complete normal halo and corners, must
qualify before either gather may omit ordinary sampling.

The pinned TerrainSystem builds each region position as `start + float(index) *
spacing`, and returns that original XY in its callback. CLAMP changes the
ordinary sampling location and values, but does not replace callback XY. The
retained source and prepared image/mesh height contributors evaluate that original
XY. Render existence composes the retained base with image holes; collision-only
cutouts/gaps do not change it. Neither supported final channel reads ordinary Z
or an ordinary result. Both values are overwritten even when the ordinary path
returns no terrain. The renderer’s existing world-height clamp, missing-height
sentinel, normal calculation, vertex remapping, CLOD interpolation/hole fallback,
RT decoding and bounds calculation are shared by both paths.

`TerrainSectorSamplingLayout::Position` mirrors the pinned TerrainSystem float
operations. The engine-override generator also verifies TerrainSystem.cpp’s
normalized SHA-256, so an upstream change requires revisiting this proof. This
does not assert equivalence to a direct reconstruction of the ordinary CLAMP
grid; it establishes equivalence to the final ordinary-query-then-overlay result.
EXACT and BILINEAR deliberately keep fallback in this initial implementation.

The owned preparation settings capture the comparison switch once per update.
The worker assesses the entire sampling plan, then selects `RetainedOnly` or
`OrdinaryThenOverlay` for both gathers. The direct path supplies the same callback
with constructed XY and an unused zero Z. It then executes the same scalar/batch
overlay and packing code. Source acquisition, captured area decisions,
cancellation, generation validation, publication locks, collision querying and
atomic raster/RT publication retain their existing contracts.

## Verification and diagnostics

Differential tests run the actual TerrainSystem region sampler and real procedural
composition against the direct path. They compare original XY, packed heights and
normals, CLOD output, RT vertices/normals, bounds and emptiness. Cases include full
128×128 sectors, fractional spacing, signed and large coordinates, scalar/batch
dispatch, rotated image/mesh height stamps, image holes, collision-only holes and
world-height clamping. Separate tests exercise whole-sector halo fallback, empty
results and source invalidation during retained execution. Existing fallback,
source lifecycle, generation, collision/render and hardware boundary tests remain
part of validation.

Optional `TerrainRenderQuery` diagnostics add `skipped-ordinary`; `ordinary`
continues to count actual ordinary callbacks. `TerrainSectorSampling` prints the
selected policy. Low-level overlay fallback buckets still describe the query
resolver’s conservative contract; the full sector certificate and selected policy
are the authority for ordinary-query elimination. Timing remains off by default.

Performance and build results are recorded in TG’s
`Gem/Docs/TerrainFlightPreparation.md` and its query-elimination measurement record.
Use the same warmed camera route, viewport and authored resolution/CLOD/RT settings
with the switch disabled/enabled. Keep diagnostic traces separate from frame
distributions, and compare hitch counts and worst frames as well as p95/p99.
The preparation target is at least 35% less matched worker time; eliminating the
synchronous worker wait across frames remains a separate optimization.
