# Terrain material overlap

Issue #19 adds optional source-aware blending to the terrain material graph. It
blends base color, world-space normal, roughness, metalness, specular factor and
ambient occlusion before the existing lighting evaluation. Height composition,
terrain existence, collision, mesh gaps, cutouts and depth/shadow geometry keep
their existing implementation.

## Authoring

1. Select the generated `overlap_terrain.material` from the Material Canvas Terrain
   Examples directory in the Terrain Material scene component, or add the new
   Terrain Overlap Surface node to a terrain graph. Connect all six outputs to
   Terrain Output. Connect the previous appearance to the six fallback inputs.
2. Expand Material Overlap on Terrain Composition Gradient. Enable it and set
   Transition Width in world metres. Disabled is the serialized default; zero
   width also disables the extension.
3. Bind every rendered height stamp in that composition to a unique material slot
   from 1 through 4. A binding identifies a stamp entity instance, not a surface
   tag or a mesh material-slot index. Two instances of the same asset remain
   separate contributors. Collision-only mesh stamps cannot participate.
4. Optionally enable Flat Ground Contributor and supply its actual world height
   and a unique material slot. Ground counts against the four-source limit. This
   option supplies material context; it does not create geometry or sample an
   arbitrary procedural base-height gradient.
5. Supply each slot's PBR channels. Terrain Overlap Texture evaluates a texture
   in that source's local planar XY coordinates. Scale is local metres per UV
   repeat; each node has its own texture, sampler and scale. Color is converted
   from linear sRGB to the renderer's working space. Normal output decodes RG
   tangent normals, reconstructs positive Z, and rotates into the source's
   world-space frame. Use a linear normal-map image preset.
6. Terrain Overlap Coordinates exposes source position, local XY and source
   normal for procedural material graphs. Graphs may use different material
   formulas per slot. The example uses distinct roughness and texture scales.

The supported evaluator is source-local planar mapping with translation, yaw and
positive uniform scale, matching prepared height stamps. Imported arbitrary mesh
UVs, tangents, arbitrary existing material graphs and per-triangle material
assignments are not automatically imported. Author their appearance explicitly
in the graph or implement an adapter; this is not a general mesh-material converter.
The example's eight image properties target the bindless material texture path;
backends using the engine's six-texture fixed array require a smaller graph.

## Selection and blending

The resolver evaluates source heights at the same world XY, independent of the
fragment's Z. Mesh samples honor cell diagonals and uncovered-cell masks; image
samples use the prepared reconstruction where available. The world-distance
transition also fades inward from source footprints, multiplied by authored
strength and feathering. A missing or uncovered source contributes zero.

Ordered mode follows prepared stamp priority and persistent-key ordering, never
registration or material-binding order. Each successive source composites over
the accumulated appearance, with a half-strength blend at equal height and full
authored coverage outside the vertical transition. This preserves the ordered
stamp model even when a later stamp is lower than an earlier one.

Retained Surface Envelope mode computes, for each source i:

`score[i] = coverage[i] * product(j != i, 1 - coverage[j] * smoothstep(0, width, height[j] - height[i]))`

Scores are normalized. Equal-height sources share weight; separated upper
surfaces dominate. All participating shaders must use this same resolver and
material slot meanings. The remaining uncovered fraction blends back to the
graph's fallback appearance. Normals are normalized with a geometric-normal
fallback; invalid PBR channels are sanitized by the existing surface contract.

## Retained-mesh integration contract

`TerrainMeshCutoutFeatureProcessor::RegisterOverlapMaterial(material)` registers
a retained material instance for the same immutable scene publication. Provide a
read-only `StructuredBuffer<uint> m_terrainOverlapWords` in its material SRG and a
uint `settings.overlapRevision` material property. Use a distinct material instance
when its binding lifetime differs from other meshes. Unregister it on teardown.

Before including `TerrainCompositor/TerrainOverlap.azsli`, define
`TC_OVERLAP_WORDS` to that SRG buffer. Resolve at world position, evaluate the same
slot appearances using each source's mapping, and call `TC_BlendOverlap` before
lighting. Callers retain responsibility for geometry, depth ownership, clipping,
draw submission and matching source bindings. This API does not retrofit existing
Vue shaders or register mesh materials automatically. TGProject provides a Vue
adapter in `TGProject/Gem/Source/Editor/VueLiveLink/terrain_overlap_editor.py`,
documented in `TGProject/Docs/VueTerrainOverlap.md`. That adapter publishes baked
height-query textures and per-instance material parameters using the same retained
weight resolver, since Vue's retained meshes are not compositor height stamps.
Retained-mesh scene visual acceptance remains a separate validation step.

## Compatibility and limits

The old surface-label buffer prefix and terrain graph v4 SRG remain intact. A
versioned footer identifies the optional extension. Old graphs ignore it and new
graphs fall back when it is absent. Existing component serialization defaults to
disabled. Invalid bindings, missing stamps, duplicate slots, unsupported source
counts and collision-only sources produce composition diagnostics. An enabled
set requires two to four contributors, including explicit ground.

Intersecting composition regions are rejected rather than selected by iteration
order. The scene supports at most 64 nonintersecting overlap regions and a 64 MiB
publication budget, including the existing prefix. Exceeding a scene resource
limit disables the extension for the publication. The feature processor exposes
overlap region, rejected-region and publication-byte counters. A graph without
the overlap contract marker produces a warning. No contributor is silently
dropped to meet a resource limit.

Source descriptors and height data are uploaded on publication changes, not
rebuilt every frame. Each shader evaluates at most four sources and scans at most
64 region headers; the Canvas nodes share one resolved set per material evaluation.
Height payloads currently copy into the publication and are not deduplicated.

## Validation and performance

The standalone Windows test suite executes the production resolver and blend
helpers on D3D11. It checks 4,014 GPU/reference cases, including continuous
three/four-source sweeps, invalid and zero-width inputs, transformed source
coordinates, normal rotation, mesh triangle diagonals and gap exclusion, PBR
channels and legacy-buffer fallback. Native
O3DE unit tests cover configuration validation, canonical packing order, budget
failure and prepared geometry data. Native Material Canvas generates the example
shaders; Asset Processor builds its material and forward/depth/shadow assets.

Run `TerrainOverlapGpuTests.exe --benchmark` for the bounded resolver microbenchmark.
One local run at 1920x1080 measured median 0.069/0.072/0.104/0.135/0.168 ms for
zero through four contributors (11 measured samples after warmup). It reads a
444-byte synthetic height publication and writes 33,177,600 bytes. These are
resolver-only GPU timestamps, not end-to-end terrain frame timings: texture
fetches, lighting, realistic height-map bandwidth and retained-mesh overdraw are
excluded. Re-run on target hardware before using these values as a budget.

Full Editor visual acceptance, MSAA captures, scene-level moving/removing-source
checks and full material GPU measurements are still required. Compiling depth
and shadow shaders does not establish visual parity for those passes.
