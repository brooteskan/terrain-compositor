# Authorable terrain holes

Issue [#23](https://github.com/brooteskan/TG/issues/23) adds an explicit terrain-existence field to the existing
height composition. The final result is `(worldHeight, terrainExists)`: height keeps composing beneath a hole, while
O3DE receives `terrainExists = false`. Rendering, PhysX terrain collision, and terrain surface queries therefore use
the same result. Existing procedural ground and stamps have no hole input by default and remain unchanged.

## Required wiring

On **Terrain Region**, replace the stock Terrain Height Gradient List with **Terrain Composition Height Provider** and
set its Terrain Composition reference. Keep the Terrain Layer Spawner and axis-aligned box. The provider implements
O3DE's shared-dispatch `TerrainAreaHeightRequestBus`, clamps world heights to current terrain settings, and forwards
scalar and batch requests to the composition's immutable snapshot. `DefaultLevel.prefab` contains this wiring.

Keep the compositor on its separate Terrain Composition entity. `GradientRequestBus` remains as a normalized-height
facade for compatibility, but it cannot represent holes and is no longer the Terrain Region's height-provider path.
If categorical surface stamps are used, keep Terrain Composition Surface Provider as described in
[TerrainSurfaceComposition.md](TerrainSurfaceComposition.md); it returns no custom weights inside a final hole.

## Procedural authoring

Procedural Ground Gradient has two optional fields:

- **Terrain Hole Mask**: any gradient sampled in world space.
- **Hole Threshold**: inclusive normalized threshold, default `0.5`.

`mask >= threshold` removes terrain. An unassigned, inactive, cyclic, or otherwise unavailable mask fails open:
procedural terrain exists. The dependency monitor propagates mask and upstream-gradient changes through the
composition's bounded invalidation path. Height generation is independent of existence.

## Stamp authoring

Each Heightmap Stamp has a **Terrain Hole Mask** group with an optional GSI16/R16_UNORM StreamingImage, threshold,
and operation:

- **Remove Terrain** makes active samples holes.
- **Restore Terrain** makes active samples exist again.

The mask uses mip zero, bilinear normalized sampling, the stamp's centered footprint, source top-to-local-`+Y`
orientation, world translation, yaw, positive uniform scale, and Edge Inset. `sample >= threshold` is active.
Samples below threshold and samples outside/cropped by the footprint or inset are neutral: they reveal the accumulated
lower result. Height Strength, height Feather, surface transparency, and a missing height image never imply a hole.
A stamp may therefore be hole-only. Hole assets share the same immutable CPU cache, revision fan-out, save/load,
duplication, prefab export, Play-mode, and registration lease behavior as other stamp channels.

## Deterministic overlap

Existence stamps use the existing ascending `(Priority, stable ordering key)` order. Every active sample is an explicit
assignment. A later Remove sets existence false; a later Restore sets it true; a neutral sample leaves the lower value
unchanged. Height continues composing independently while hidden, so restoring terrain reveals the already-composed
height instead of fabricating a new elevation.

## Editor visualization

Select a Heightmap Stamp to see its thresholded local mask sampled over the transformed footprint. Red cells author
Remove; green cells author Restore. This overlay is at authoring Z and is separate from the height feather outlines.

Select Terrain Composition Gradient to see the final sampled hole result in red. A sparse region grid exposes
procedural holes, and a refined grid over every stamp-hole footprint shows overlap and Restore behavior at the
underlying composed height. The overlays are diagnostics only; they are not serialized and do not drive queries.

## Updates and safety

Hole asset readiness/reload/failure, threshold/operation edits, transforms, activation, removal, retargeting, ordering,
and collisions publish one replacement immutable state before notification. Previous and current existence footprints
are retained and sent to both height and surface invalidators. The height provider consumes the compositor's bounded
dependency notifications; surface changes use bounded Terrain refreshes. Shared-dispatch disconnects drain in-flight
height, existence, and surface queries before routing changes.

Pure sampling tests cover inclusive thresholding, neutral behavior, hole-only independence from height strength/feather,
deterministic Remove/Restore overwrite, and procedural scalar/batch agreement. Visual rendering, collision, and Asset
Processor import still require an editor/runtime acceptance pass with a processed hole mask.
