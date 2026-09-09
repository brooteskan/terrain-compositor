# Terrain stamp surface composition

Issue [#13](https://github.com/brooteskan/TG/issues/13) extends each heightmap stamp with optional categorical
surface data while leaving O3DE's material assets, Terrain Surface Materials List, renderer, and shaders intact.
Existing stamps with no surface set publish exactly the same height contribution and no surface modification.

## Component wiring

Use three separate responsibilities:

1. On the **Terrain Composition Gradient** entity, keep the procedural source and terrain-region references. Add
   one Surface Palette entry for every exported integer ID and optional Base Surface Weights.
2. On **Terrain Region**, keep the Terrain Layer Spawner, shape, **Terrain Composition Height Provider**, and stock
   **Terrain Surface Materials List**. The height provider references the composition entity. Replace the
   stock Terrain Surface Gradient List with **Terrain Composition Surface Provider** and point it at the composition.
3. On each **Heightmap Stamp**, keep the existing placement/height controls. Under Surface Maps, assign ID A alone,
   or assign ID A, ID B, and Blend Mask. ID B without Blend Mask (or the reverse) is invalid and contributes no
   surface data; it does not disable a valid height contribution.

Only one component can provide `TerrainSurfaceProviderService` on Terrain Region. The dedicated provider enforces
that stock single-provider constraint and implements `TerrainAreaSurfaceRequestBus` at the region entity ID. It
forwards directly to the composition's immutable query state; it never queries final terrain from inside the provider.

The composition palette maps `uint16 exported ID -> O3DE surface tag`. Configure the same tags in O3DE's stock
Terrain Surface Materials List to map them to material assets. There is intentionally no second material-asset mapping
in the TerrainCompositor components. Changing a tag's material or reloading that material remains an O3DE appearance update;
it does not recompose stamp weights or invalidate height geometry.

## Concrete initial image route

The supported initial route is a separate unsigned, single-channel, 16-bit image for each logical channel:

- ID A: required for a surface set; exact unsigned values 0 through 65535.
- ID B: optional, but valid only with Blend Mask; same dimensions as ID A.
- Blend Mask: optional, but valid only with ID B; same dimensions as ID A; decoded as `raw / 65535`.
- ID 0: transparent/no surface modification. IDs 1 through 65535 are persistent authoring labels.

ID-map dimensions are independent of the height image. A, B, and Blend within one surface set must match each other.
All surface channels use the stamp's world translation, yaw-only rotation, positive uniform scale, local footprint,
edge inset, and top-left source orientation. Left/right maps to local -X/+X; top/bottom maps to local +Y/-Y.

Use TIFF or another source format that Asset Processor can import through these exact settings:

- Texture preset: `GSI16`
- color space: Linear to Linear
- product format: `R16_UNORM`
- resolution reduction: 0 / Use Max Res
- one unsigned channel; no RGB conversion, gamma transform, range normalization, or lossy compression
- runtime sampling always reads mip 0; ordinary averaged ID mips are not sampled

Source inspection of the installed O3DE revision `061180bf24f1666eb30315b35da292eb14f4659c` found no Terrain
Editor workflow that exports categorical surface-ID maps. The reproducible initial route is therefore an external
GIS/DCC/data export or the repository's deterministic TIFF generator, followed by the settings above. The generator
requires no external packages:

```powershell
.\tools\GenerateHeightmapSamples.ps1
```

The generated files are in `Assets/HeightmapStamps` with checked-in `.assetinfo` settings. Asset Processor import was
not run by the generator and must finish before selecting the StreamingImage products in the editor.

## Palette and base rules

Palette ID 0, duplicate exported IDs, unassigned surface tags, nonfinite base weights, and a base ID missing from the
palette invalidate the palette. A surface map containing any nonzero ID absent from the palette invalidates that stamp's
entire surface set. Invalid/loading/missing/malformed surface inputs publish no surface modification and an actionable
`TerrainComposition`/stamp status diagnostic; a valid height map remains active.

Base entries are resolved through the palette, merged when multiple IDs name the same tag, and normalized. Configure an
explicit base, such as grass weight 1, when stamps should blend with grass. O3DE's Default Material is used only when the
final returned list has no positive mapped surface. It is never treated as the unused fraction of partial stamp strength.

## Sampling and accumulated Replace

IDs are categorical. The decoder retains exact `uint16` mip-zero values alongside the existing normalized height samples.
At a filtered sample, the compositor gathers the four neighboring texels in a fixed order. Each neighbor contributes its
own `(ID A, ID B, blend)` tuple using the bilinear spatial weight. It never interpolates numeric IDs, and it never
interpolates a blend value independently across changing ID pairs. Equal A/B IDs and duplicate palette tags merge.

For a fully nontransparent decoded distribution `S` and stamp coverage `a`, composition is:

```text
next[tag] = (1 - a) * accumulated[tag] + a * S[tag]
```

Coverage is existing strength multiplied by the footprint feather mask and feather exponent. Surface composition does
not use height's elevation-dependent relative-edge arithmetic. Outside the footprint, in the zero-contribution inset,
or at zero coverage, the accumulated result is unchanged. When filtering crosses ID 0, the transparent interpolation
mass reveals the same proportion of the accumulated lower surface rather than creating a terrain hole.

Stamps run in the exact height priority/order-key order. All positive candidates remain in an unbounded temporary vector
through complete overlapping-stamp composition. Only at the provider boundary are duplicate tags merged, nonpositive
weights removed, candidates sorted by descending weight then descending surface-tag CRC, the strongest 16 retained, and
the retained distribution renormalized. Scalar and batch queries call the same function against one immutable snapshot,
so their arithmetic and one-time reduction are identical. O3DE then selects its final two detail materials for each
surface-grid sample.

## Reproducible demonstration

Create four surface tags (for example grass, rock, dirt, and sand), assign material assets for those tags in Terrain
Surface Materials List, and add palette mappings 1 through 4. Add an explicit base of ID 1 with weight 1.

For a single-ID demonstration, assign `surface_id_a_gsi16.tiff` as ID A and leave B/Blend empty. The quadrants resolve to
IDs 1, 2, 3, and transparent 0; the transparent quadrant reveals the explicit base. No intermediate numeric IDs appear
at boundaries: spatial filtering mixes neighboring tags.

For a two-ID demonstration, assign all three generated surface files. `surface_id_b_gsi16.tiff` supplies ID 4 in the top
half and ID 2 in the bottom. `surface_blend_gsi16.tiff` supplies raw bands 0, 16384, 32768, and 65535, exercising blend
values 0, approximately 0.25, approximately 0.5, and 1. Move, yaw-rotate, uniformly scale, and resize the stamp to verify
the surface boundary follows the same placement as height. Overlap a second copy with a different persistent order and
priority, then vary strength, feather exponent, and inset to observe accumulated Replace and lower-layer restoration.

To inspect the provider-boundary capacity manually, overlap stamps whose constant ID maps resolve to at least 17 distinct
tags at one point. The query result must contain the strongest 16, CRC-resolve equal-weight ties, and sum to 1 after
renormalization. This repository does not add or run an automated test for that scenario without separate test
authorization; the implementation performs the reduction once in `ComposeSurfaceStamps`.

## Invalidation and lifecycle

Height and surface invalidation are independent:

- placement/coverage/order changes cover old and new footprints for both data classes;
- height image/height parameters and procedural-source changes notify only `HeightData` through the existing dependency path;
- surface image state/reloads cover every dependent stamp footprint and refresh only `SurfaceData`;
- palette/base edits refresh `SurfaceData` across the old and new terrain regions;
- terrain-region reference or shape changes cover both data classes in their retained old/new region contexts;
- provider activation, deactivation, or retargeting refreshes its complete terrain area.

The compositor publishes the replacement immutable height/surface state before queuing notifications. Shared cache
revisions fan out across height, ID A, ID B, and Blend roles before that publication. Query callbacks do no disk access,
asset loading, mutable registration sorting, editor access, or final-terrain queries. Shared-dispatch bus disconnection
drains in-flight surface queries during provider/composition teardown.

Terrain surface query resolution controls boundary detail independently from height resolution. The existing renderer's
near detail materials still fade toward the project's existing macro appearance; this feature does not generate macro
color/normal data or change terrain shaders. ID 0 is surface transparency, not terrain nonexistence. Unified holes remain
owned by [issue #23](https://github.com/brooteskan/TG/issues/23); when final terrain existence is false, O3DE should not
render, collide with, or return a surface there, and this provider deliberately does not query that final result recursively.
