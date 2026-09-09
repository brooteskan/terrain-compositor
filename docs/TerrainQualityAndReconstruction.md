# Terrain quality and heightmap reconstruction

This document covers the opt-in controls from [issue #25](https://github.com/brooteskan/TG/issues/25).
They address two independent sources of visible terrain faceting: the O3DE terrain query/renderer mesh density and reconstruction
of sharp boundaries in a stamp's source pixels.

## Compatibility defaults

Terrain Composition Configuration contains a collapsed **Terrain Quality** group. **Override Terrain Quality** defaults off.
When off, the compositor does not write the terrain height query resolution or renderer mesh configuration. Terrain World and
Terrain World Renderer remain the service and feature-processor owners.

Heightmap Stamp defaults to **Bilinear** with a **Reconstruction Radius** of 0 source texels. Older serialized configurations do
not contain these fields, so their default construction preserves the previous output. Surface Data Query Resolution and terrain
height bounds are deliberately not part of this profile.

## Terrain quality profile

When **Override Terrain Quality** is enabled, the compositor exposes:

| Control | Meaning |
| --- | --- |
| Height Query Resolution | World-meter spacing used by O3DE terrain height queries; minimum 0.1 m |
| Override Renderer Mesh Settings | Enables the context-local mesh controls below |
| Mesh Render Distance | Maximum terrain mesh draw distance |
| First LOD Distance | Coverage of the closest terrain LOD |
| CLOD Enabled | Continuous blending between mesh LODs |
| CLOD Distance | Closest-LOD blend distance |

The controller waits for Terrain World and the owning context's Terrain Feature Processor, captures their stock values, applies
the requested values, and verifies height-resolution readback. It restores the captured values when disabled, deactivated, made
invalid, or placed into conflict. It never enables a feature processor or creates a second Terrain World/renderer component.

The terrain height setting is process-global in the current O3DE bus, so exactly one active compositor may claim the profile.
If multiple valid compositors opt in, every override is suppressed and `TerrainQuality` reports each claimant entity/context.
Removing or disabling claims leaves the sole remaining owner pending until stock restoration is observed, then reapplies it.
The component Status line reports Disabled, Pending, Applied, Invalid, or Conflict.

A successful resolution write queues normal whole-region height invalidation. Renderer-only changes use
`TerrainFeatureProcessor::SetMeshConfiguration`; stamp reconstruction changes continue through old/current footprint invalidation.

## Reconstruction modes

**Bilinear** reads the original mip-zero `HeightmapData` and preserves the existing sample arithmetic. **Smooth Cubic** creates a
separable cubic B-spline prefilter on the control thread. Weights are nonnegative and normalized; every output is clamped to the
minimum/maximum of its contributing neighborhood. Constant plateaus remain exact and hard steps cannot ring or overshoot.

The radius is expressed in source texels and accepts 0 through 8. Radius 0 bypasses reconstruction. Prepared buffers are cached by
source object, source revision, mode, and radius, then held by the immutable query snapshot. Reloading an image or changing either
control creates the buffer once during publication. Terrain workers perform only the existing four-sample bilinear lookup.

## Initial tuning

For the current 512 x 512 image over a 512 x 512 meter stamp, start with:

| Control | Initial value |
| --- | ---: |
| Height Query Resolution | 0.5 m |
| First LOD Distance | 256 m |
| CLOD Distance | 32 m |
| Reconstruction Radius | 1 to 2 texels |

Keep the renderer override optional if the stock mesh profile is already suitable. A 0.25 m height grid is available but is a
higher-cost choice: halving spacing can approximately quadruple closest-LOD sample density, while quartering it can approach a
sixteen-fold increase. Reconstruction smooths source transitions; mesh density only reduces additional terrain approximation.

## Verification and performance

`HeightmapReconstructionTests` generates constant, sharp-step, two-dimensional extrema, and 65 x 65 radial-terrace fixtures in
memory. The radial fixture is sampled at 1.0, 0.5, and 0.25 texel spacing and verifies a wider bounded transition with rotational
symmetry. The suite also locks down exact Bilinear samples and scalar/batch-loop agreement. `TerrainQualityTests` covers serialized
field versions/defaults, invalid/disabled states, and deterministic multi-owner suppression.

For visual acceptance, use a disposable copy of the large Terrain Region and the curved 512 x 512 terrace image. Compare Bilinear
against Smooth Cubic at the same camera transform, then compare 1.0 m and 0.5 m query grids. Inspect terrace silhouettes in plan view
and grazing-angle mesh diagonals. Confirm procedural terrain outside the stamp does not change.

For performance acceptance, capture the same fixed camera path in profile builds after terrain settles. Record frame time, terrain
update time, and memory for 1.0 m/Bilinear, 0.5 m/Bilinear, 0.5 m/Smooth Cubic, and 0.25 m/Smooth Cubic. Repeat with one stamp and
several overlapping stamps. Include the one-time publication cost after changing radius or reloading the shared image separately
from steady-state query cost. Smooth Cubic should add prepared-buffer memory and publication work, not per-query multi-tap work.
