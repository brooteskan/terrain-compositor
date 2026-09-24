# TerrainCompositorCanvas

Optional Material Canvas terrain tint output for the pinned O3DE revision supported
by `../o3de`. Enable both TerrainCompositor and TerrainCompositorCanvas in the
consumer project and include both integration directories in `external_subdirectories`.
Reconfigure and build the consumer's Editor and runtime targets, then restart Asset
Processor so it discovers the new Gem assets.

## Authoring and selection

Open an example from `Assets/MaterialCanvas/Terrain/Examples` in Material Canvas.
Compile the graph, then let Asset Processor finish its shader and material jobs.
The graph produces a tint include, a forward shader, and a material type. The
adjacent `.material` is user-owned and is never overwritten by graph compilation.
Edit its `tint.strength`, `tint.color`, and optional `tint.texture` in Material Editor.
Color is a raw linear working-space RGB vector, matching the original shader values.

Add **Terrain Tint Material** to an entity in the terrain's scene and select the
compiled example material. Only one component may own a scene's selection. Its
Status field reports loading, incompatibility, or ownership conflict. Clear the
selection or disable the component to restore the legacy terrain tint. Editor and
game contexts resolve their own scene and material instance.

The examples progress from a constant proof to the original procedural effect:

* `minimal_tint`: white-to-color interpolation, controlled by strength.
* `procedural_tint`: world XY divided by 64, seeded quintic lattice noise, derivative
  filtering, elevation fade from -3 to 3, strength, and RGB interpolation. These
  operations are connected graph nodes rather than one opaque tint function.
* `world_bands`: an alternate world-space sinusoidal formula.
* `texture_tint`: a sampled RGB texture; an unset texture binds neutral white.

For repeatable native graph compilation, launch MaterialCanvas with
`--rhi=null --runpython=<absolute-path>/Tools/CompileExamples.py --timeout=180000`.
The script locates its Gem automatically, limits each graph to 90 seconds, and exits
the tool when finished. Use `TC_CANVAS_GRAPH=procedural_tint.materialgraph` to limit
the run to one example. The null renderer avoids the stock mesh preview doing
unnecessary work. Canvas's native asset-status query can make Asset Processor
alternate Working/Idle rapidly even after compilation has finished; inspect job
results rather than treating those status transitions as repeated compilation.
The script writes `compile-results.json`; asset-processing success must be checked
separately. The normal material preview mesh is not a terrain preview. Use the
terrain scene to evaluate this output.

## Rendering contract

ABI version 1 uses a fixed, shared TerrainMaterialSrg. The graph controls executable
tint code; the output template exposes dedicated strength, color, and texture
bindings. Arbitrary Canvas material types are not accepted. Materials must retain
the forward/depth/shadow shaders, matching material/object/terrain SRG layouts,
detail parameters, cutout/gap properties, and the versioned tint properties.
Ordinary Canvas nodes that require unrelated mesh inputs or StandardPBR's bindless
material parameters are outside this contract. World Position and the supplied
Terrain Tint nodes use the terrain output's inputs.

TerrainCompositor owns the shared terrain forward implementation, default material,
and depth/shadow assets. This Gem generates uniquely named forward assets and adds
no competing override for those paths. Both direct detail sampling and clipmap
sampling feed the same tint call after detail/macro blending and before lighting.
The legacy and Canvas functions are mutually exclusive. While Canvas owns tint,
Procedural Ground's legacy Noise Tint Strength is inactive; `tint.strength` is the
single strength control.

Selection creates a scene-local material, validates the contract, copies the active
cutout/gap publication, base color, and detail settings, and waits for material/SRG compilation
before switching draw packets. Tint changes do not invalidate composition, height
queries, collision, terrain vertices, or clipmap content. A failed asset load or
incompatible candidate retains the currently selected valid material. Successful
material reloads are staged through the same path. Shader variant notifications
refresh draw packets without rebuilding terrain geometry.

## Validation

See [the implementation report](../../docs/TerrainCanvasTint.md) for checks actually performed and
any remaining live-rendering limitations. A successful native C++ build or graph
compilation alone does not establish visual parity or GPU frame cost.

The independent Windows GPU parity test compiles the retained legacy function and
the native-generated procedural graph into separate D3D11 pixel shaders:

```powershell
cmake -S integrations/o3de-material-canvas/Tests -B build/canvas-parity
cmake --build build/canvas-parity --config Release
ctest --test-dir build/canvas-parity -C Release -V
```

`Tools/ValidateEditor.py` runs in Editor with `--runpython <absolute-script-path>`.
It opens the saved DefaultLevel, temporarily creates a selector, captures all four
materials, exercises material parameter reload and invalid-contract rejection, and
enters game mode. It temporarily edits and restores the example material, but does
not save the level. Save open work before running this validation.
Set `TC_CANVAS_TEST_GRAPH_RELOAD=1` to also edit the procedural graph's noise scale,
compile it with the native Canvas tool, and restore/recompile the original graph.
The report records Canvas process exit codes separately: this pinned tool was
observed to fault during null-renderer shutdown after reporting successful
compilation. Fresh compiler reports and live rendering checks are required;
existing generated files alone are not treated as success.
