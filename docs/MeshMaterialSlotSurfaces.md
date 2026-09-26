# Mesh material slots as terrain surface labels

The first editor workflow supports face material assignments on the existing regular-grid mesh height stamps. Blender material appearance is not imported. Exact, case-sensitive imported material names are the labels; mesh-section ordering is irrelevant. Use unique names. A rename needs an explicit mapping edit. Missing, duplicate or unmapped names make no surface contribution, and height continues to work.

## Authoring

1. Assign materials such as `Rock`, `Soil`, and `Road` to faces of a supported grid mesh in Blender and export FBX. Keep the existing yaw-only, positive uniform-scale and complete-cell topology requirements.
2. Assign the imported model to a **Terrain Mesh Height Stamp** and its matching **Mesh** entity. The Mesh entity supplies placement. Hide the source Mesh to inspect the composed terrain.
3. On the target **Terrain Composition Gradient**, add **Surface Palette** entries mapping exported IDs to surface tags. Add an explicit base surface weight if feathered stamps should reveal a background surface.
4. On the stamp, click **Add Imported Material Slots**. Set each entry's **Surface ID** to the corresponding palette ID. Zero leaves lower surfaces unchanged. Existing entries are preserved; old names are not silently rebound after a rename.
5. In Material Canvas, use **Terrain Surface Weight** nodes with those exported IDs. The output is a normalized weight for the surface tag mapped by that ID in the containing composition. Combine color, roughness, normals or procedural formulas using ordinary graph math and connect them to **Terrain Output**.
6. Compile the graph, wait for Asset Processor, and select its material with the **Terrain Material** component.

The supplied `mesh_surface_labels.materialgraph` uses IDs 1/2/3 for Rock/Soil/Road. The example's **Three Surface Example** node exposes editable colors and demonstrates different roughness values. It uses green where none of those three surfaces contributes.

The Blender source and exported FBX are under `integrations/o3de/Assets/Models/TerrainSurfaceLabels.*`. Regenerate them with `tools/GenerateMeshSurfaceExample.py` using Blender's `--background --python` option and an output directory after `--`.

## Behavior

- Each covered cell retains the two imported face labels through vertex welding and section merging. Height and labels use the same diagonal ownership rules and placement.
- Image and mesh surfaces share the existing priority/order stream, strength, feather, inset, base weights and final strongest-16 normalization. Relative Edge Blend remains height arithmetic.
- Reimport refreshes labels with the immutable model revision. Empty mappings preserve height-only behavior. A gap has no surface label; existing existence/collision/render masking still owns removal.
- The initial GPU implementation uploads an immutable word buffer on scene publication and evaluates weights from those source labels. It has no surface texture resolution or baked appearance. It favors a working authoring path; repeated graph lookups and large overlapping stamp sets have not been optimized or benchmarked.
- Graph material contract version is now 4. Recompile existing custom terrain graphs to acquire the added surface buffer/count/revision bindings. Generated default and example materials are updated with the contract.

## Local demonstration

TGProject has a separate `Levels/MeshSurfaceLabels/MeshSurfaceLabels.prefab` demonstration. The source Mesh is hidden, the stamp maps the three imported names, and the material displays their weights. The ordinary DefaultLevel is not modified by demonstration setup.

Editor gems, native Canvas generation, and forward/depth/shadow/material Asset Processor jobs were checked for the initial workflow. Broader regression testing, GPU parity at adjacent floats, and performance measurement remain follow-up work.
