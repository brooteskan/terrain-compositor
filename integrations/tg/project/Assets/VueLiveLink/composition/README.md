# Vue terrain walking surface

`VueTerrainSurface.tif` contains the topmost surface of the saved Terrain6 and
Terrain7 meshes, combined with the level's flat ground at Z=0. It is a linear,
unsigned 16-bit heightmap. `VueTerrainSurface.json` records source hashes,
transforms, bounds, height scale, sampling spacing, and verification points.

The level's **Vue Terrain Walkable Surface** heightmap stamp targets **Terrain
Compositor**. Its heights feed terrain queries and the compositor's automatic
PhysX heightfield collision. The first-person root has a 1.8 m capsule, gravity,
0.3 m step height, and a 45 degree walkable slope limit.

The original Vue mesh/material entities remain visible. **Vue Terrain Render
Mask** contains a closed box that hides the overlapping native terrain geometry
above Z=0.002. Its **Affect Collision / Queries** option must remain disabled.
The placement mesh is hidden in edit mode and automatically by the compositor
in play.

After moving or rebaking Vue terrains:

1. Exit play mode and select **Terrain Compositor**.
2. Click **Refresh collision surface** in **Terrain Composition Gradient**.
3. Wait for **Collision surface refreshed** in the inspector, then save the level.

A progress dialog stays open through building, asset processing, and checking the
new surface. It blocks editor interaction, including entering Play, until the
operation completes or is cancelled. **Cancel** stops the worker and retains the
previous surface, restoring its bindings if publication already began. Completion,
cancellation, and errors remain visible with a **Close** button. Save the level
after a successful refresh to keep the new collision registration.

The button captures current, unsaved Editor transforms. Preparation runs in a
background worker using the same Python selection as Vue Live Link
(`TG_VUE_LINK_PYTHON`, the bundled runtime, or Python on PATH; numpy and Pillow
are required). It waits for Asset Processor and verifies the queried heights
before reporting completion. Duplicate clicks are ignored while busy. Changing
source terrains during preparation requires another refresh. Existing bindings
are restored if publication fails. The button does not move the character or
save unrelated level edits.

Each revision has its own assets under `revisions/`, so loading new data cannot
mistake an older processed heightmap for the new one. Worker diagnostics are in
`user/VueLiveLink/collision-refresh/`. Other projects can provide a source
converter through the compositor's Editor refresh notification bus.

The original command-line workflow is also available:

1. Save VueLiveLink and exit play mode.
2. Run `Editor/Scripts/prepare_vue_composition.py` with Python, numpy, and Pillow.
3. Allow Asset Processor to finish.
4. Run `Editor/Scripts/register_vue_composition.py` in the Editor. It updates the
   two existing registration entities and saves the level.

`Gem/Tests/verify_vue_composition_editor.py` checks source-reference heights,
capsule grounding, and uphill traversal on both terrains without visual input.
This registration is a saved snapshot; a LiveLink rebake alone does not refresh
the compositor heightmap.
