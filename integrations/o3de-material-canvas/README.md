# Graph-defined terrain materials

Terrain uses one graph-defined material per scene. Every forward fragment evaluates the graph after geometry and cutout/gap checks, then validates its channels before existing decals and lighting. Depth and shadow use the same material declaration and geometry checks without evaluating appearance.

## Authoring

1. Enable TerrainCompositor and TerrainCompositorCanvas and build MaterialCanvas from the project. The pinned compiler extension emits the native `MaterialParameters` declaration for direct terrain shaders.
2. Open `Assets/MaterialCanvas/Terrain/Examples/default_terrain.materialgraph`, `procedural_terrain.materialgraph`, or `textured_terrain.materialgraph` in Material Canvas.
3. Connect ordinary Canvas math and material input nodes to **Terrain Output**. Use **Float Input**, **Color Input**, and **Sample Texture 2d** to expose named properties. Set each input's Name; values appear in the generated material type and can be edited in Material Editor.
4. Compile the graph and let Asset Processor finish. Forward, depth and shadow wrappers share the generated `_Material.azsli` and `_Parameters.azsli` declaration.
5. Create or edit a `.material` instance, then select it with the **Terrain Material** component. Only one enabled component may own the scene's material. Edit named values and assign images on that material instance.

Compilation never writes `.material` files. Graph formulas belong in the graph; instance values belong in the selected material. Formula and parameter edits do not invalidate terrain height or collision. Clearing selection restores the bundled default graph material. The base Gem ships that material and its generated shaders; it does not require Canvas at runtime.

## Channels

| Channel | Disconnected default | Meaning |
|---|---|---|
| Base Color | White | Linear ACEScg, nonnegative HDR values allowed |
| Normal (World) | Zero socket value resolves to geometric normal | Normalized world-space shading normal |
| Roughness | 1 | Perceptual roughness |
| Metalness | 0 | Factor |
| Specular Factor | 0.5 | Dielectric specular factor |
| Ambient Occlusion | 1 | Factor |

Nonfinite colors fall back to white. Invalid or zero normals fall back to a safe normalized geometric normal. Nonfinite factors use their channel defaults; finite factors clamp to [0, 1]. Color Input uses Atom's color conversion; texture samples use the stock node's source/destination color-space controls. Missing images use Atom's material-system null texture, rather than an unbound sample.

The procedural example exposes scale, ground color and roughness, and derives a world-space shading normal from procedural height. The textured example exposes two independent images and coordinate scales, a blend factor, and a color. Sampled appearance always runs per fragment, including far terrain; texture derivatives and normal derivatives use the fragment's geometry.

Emissive, POM, clear coat, transparency, displacement and per-detail-material graphs are unsupported. Surface tags for physics/composition and geometry LOD remain available. Macro/detail material components, appearance clipmaps, noise-tint controls and incoming-surface nodes are removed.

## Binding and reload contract

`TerrainMaterialSrg` owns fixed renderer buffers/counts/revisions and graph parameters in `m_params`. Its cutout/gap buffer names and element strides remain fixed. Graph layouts may differ. Graph property groups `settings` and `terrain`, and shader binding names beginning with `m_`, are reserved for the renderer. The current hidden marker is `terrain.contractVersion = 3`; there are no historical contract branches.

Selection validates required renderer bindings, object/terrain layouts, and forward/depth/shadow layouts. Only publication fields and buffers transfer to a candidate. Activation waits for compilation and an unchanged source publication, candidate change ID and layout. Failed candidates retain the active material; incompatible active reloads restore the bundled default.

## Validation

Run the CMake project in `Tests` for GPU channel/normal tests, node portability checks, source-contract verification and pinned compiler drift checks. Generate examples using the rebuilt executable:

```text
MaterialCanvas --project-path=<project> --rhi=null --regset=/O3DE/TerrainCanvas/SourceGenerationOnly=true --runpython=<gem>/Tools/CompileExamples.py --timeout=600000
```

`CompileExamples.py` records native source generation provenance and checks all generated declarations. Asset Processor completion is a separate gate. Run `Tools/ValidateSurfaceEditor.py` in a dedicated Editor to capture each material near/far, switch layouts rapidly, clear selection, enter game mode and capture pass timings. It restores temporary scene edits without saving. Inspect screenshots and timing outputs separately; successful capture is not visual correctness.

Also verify incompatible reloads, compilation failures, edited/saved/reloaded properties and independent texture assignments, nonzero cutouts and height gaps in every raster pass, unchanged collision/raycast results, and a standalone packaged runtime. Record native generation, Asset Processor, Editor, runtime and timings independently; do not substitute unit tests for runtime evidence.

Current local verification and limitations: [Terrain graph validation](../../docs/TerrainGraphValidation.md).
