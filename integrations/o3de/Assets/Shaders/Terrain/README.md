# Minimal terrain noise tint

These project-local shader/material overrides start from O3DE revision
`061180bf24f1666eb30315b35da292eb14f4659c`. The shader descriptors are unchanged.
`TerrainSrg.azsli` and `TerrainDetailHelpers.azsli` forward to stock declarations;
the terrain/detail texture-buffer layouts remain stock. The project also owns
terrain mesh-cutout integration overrides; see `Gem/Docs/TerrainMeshHeightGaps.md`.

The noise-tint rendering change is `TGTerrainNoiseTint(surface.position)`, multiplied
into the resolved terrain base color after detail/macro blending, before PBR lighting.
This applies tint once to both near detail and distant macro color; the camera-distance
detail fade does not attenuate the tint. Both direct-material and clipmap paths use it.
Noise uses world XY, with 64-meter lattice spacing and a fixed seed. Terrain's
existing world-space height drives its strength, smoothly from zero at -3 meters
to full strength at +3 meters. Unresolved noise is filtered toward its mean.

## Inspector control

`Procedural Ground Gradient > Noise Tint Strength` controls the existing tint:
0 restores the original base color, 1 gives full strength, and 0.75 preserves the
prototype default. The value is saved with the component; old levels default to
0.75 without changing their height settings. Scale, seed, tint color, and height
modulation remain shader constants for now.

The terrain renderer asynchronously loads `Materials/Terrain/DefaultPbrTerrain.azmaterial`
and creates a distinct material/SRG for each scene. The component resolves its
owning entity context and updates that scene's actual terrain material instance.
On the main tick it updates `settings.noiseTintStrength` only if the value differs.
The renderer handles material compilation. EditorEntityEvents enables this path
in the generic editor wrapper as well as at runtime; material/shader reloads
reapply the inspector setting. No CPU noise evaluation or texture is added.

This is one terrain-wide base-tint setting per scene, not per-source/per-surface
appearance selection. Use one ground component per scene to control it. Stopping the
component releases its update subscriptions but does not overwrite the shared
material; set strength to zero to turn the tint off. The tint does not alter height
generation or lighting calculations. The separate normal correction below fixes
the renderer inputs used by lighting.

## Terrain normals and bounds

`TerrainCommon.azsli` is a copy of the pinned stock helper with normal reconstruction
routed through `TerrainNormal.azsli`. Both forward and depth/shadow passes include
this local helper. It clamps the Z reconstruction before the square root and
normalizes the result, including when SNORM rounding puts XY outside the unit disk.
The material and vertex/SRG layouts are unchanged.

The maintained `TerrainMeshManager.cpp.patch` normalizes both heightfield slopes
with one shared length before packing XY. The RT conversion also normalizes its
decoded vector to match the shader. Sector minimum and maximum heights are updated
independently, so a first-sample maximum or descending traversal cannot produce
undersized or empty culling bounds.

`TerrainRenderingTests` checks production gather/packing/CLOD/RT output
against analytical plane normals and checks bounds against independent point
accumulation. `TerrainNormalGpuTests` compiles the shared normal helper and tests
all 65,025 XY pairs in [-127, 127] at five CLOD blend fractions (325,125 outputs)
on D3D11 hardware or WARP.

## Shared shader layout

The project `PbrTerrain.materialtype` binds the new float to
`TerrainMaterialSrg::m_noiseTintStrength`, appended to the stock material SRG.
The project default material selects that type. Forward, depth, shadow, and both
clipmap compute shader descriptors are overridden together so every consumer
uses the same material SRG layout. Depth/shadow share the local terrain vertex helper;
the two compute source wrappers include the stock passes from the project
include root. They do not add tint to clipmaps: tint is still evaluated in the
forward shader, so inspector edits do not require regenerating clipmap textures.

The cutout fragment stage shared by depth and shadow returns the rasterized
`SV_Position.z` through a `precise SV_Depth` output. Its position input uses `sample`
interpolation so every MSAA sample keeps its own depth. Writing pixel-centre depth
to all samples makes the forward pass reject samples on slopes; the reflection
resolve then averages the unwritten samples' gray clear color into terrain lighting.
World position keeps the forward shader's pixel interpolation for cutout decisions.
A void fragment return compiles to GPU code but fails this engine's output-layout
reflection, so an explicit depth output is retained.

`TerrainDepthGpuTests` compiles the production depth interface and fragment function
and checks forward-pass coverage against fixed-function depth on flat and signed
slopes, including a cutout, at 1x, 2x and 4x MSAA. The original pixel-centre depth
implementation fails this test on multisampled slopes.

Rebuild the TerrainCompositor Gem using the consuming project's existing `build/windows` directory, then reopen
the editor and let Asset Processor finish processing the project shader/material
assets. After introducing or changing the material parameter layout, fully restart
the editor once processing finishes: this engine can retain an old parameter layout
during hot reload. Shader-only changes that preserve the layout need no C++ rebuild.
The project-owned Terrain C++ integration is built through the stock Terrain
target. Keep the Terrain gem enabled and rebuild both `Terrain` and
`Terrain.Editor` when changing the project renderer overrides.
