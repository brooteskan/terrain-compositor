# Terrain mesh cutouts

`TerrainMeshCutoutComponent` uses one validated closed Atom model as the canonical source for terrain rendering and terrain queries. `Cutout Mesh` supplies the model. Exactly one Atom Mesh instance using that model must exist on the component entity or one of its descendants; that instance supplies its complete world transform. The cutter is data only and contributes no mesh draw of its own.

At runtime the component hides the same uniquely matched Atom Mesh instance whose transform places the cutout. This keeps an imported cutter visible and selectable while authoring without rendering it in play mode. Other hierarchy meshes remain visible because matching is by model asset ID; zero or multiple matches fail open with an actionable status.

Model preparation is asynchronous. Only the latest accepted preparation in the
current asset lifecycle may publish. A load or reload failure retires older ready
callbacks and pending results so they cannot overwrite the error state. Subscribers
receive changes on the control thread; a later ready or reload event can prepare
the model again. Previously acquired immutable snapshots remain valid for their readers.

## Rendering contract

The composition control thread publishes immutable, revisioned cutout snapshots partitioned by Atom scene. `TerrainMeshCutoutFeatureProcessor` flattens each changed snapshot into one set of read-only GPU buffers. No upload, allocation, mutable bus call, or GPU readback occurs on an unchanged revision.

The project terrain forward, depth-prepass, and shadow shaders evaluate the same ordered closed-volume predicate at the terrain fragment's world position. A local-bounds test culls distant volumes before triangle work. The test is independent of camera position, supports the full positive-uniform-scale world transform, and applies remove/restore operations in priority and stable-key order. Because the resources are declared only in `TerrainMaterialSrg`, non-terrain draw packets cannot observe or apply the cutout.

The Gem-owned `TerrainMeshManager` integration retains the scene's immutable render-geometry query snapshot while sampling each sector. The query preserves ordinary authored terrain holes but omits mesh-cutout collision modifiers; this prevents `Affect Collision / Queries` from deleting render vertices before the pixel mask runs. The scene channel crosses the Terrain/TerrainCompositor module boundary without per-sample bus dispatch or component-lifetime coupling.

Globally reversed closed meshes are normalized to outward winding during canonical data construction. GPU publication is capped at 256 active cutouts and 262,144 triangles per scene; the feature processor reports active cutouts, vertices, triangles, uploads, and resource-limit rejections through `GetStatistics()` and emits a warning when a cutout is rejected.

## Collision and query contract

Terrain existence, height, surface, and ray queries use the immutable CPU BVH and the independently configured Collision Margin. The stock terrain physics collider converts `terrainExists == false` samples into heightfield holes. Collision preparation adds one live terrain-grid cell diagonal to the authored margin, conservatively covering cells intersected between height samples without enlarging the rendered opening.

`Affect Rendering` and `Affect Collision / Queries` are independent. `Render Margin` and `Collision Margin` are finite nonnegative world-space values. Version 3 data migrates the former `IntersectionMargin` value into both fields. Version 4 data retains its serialized `Cutout Mesh` and discards the removed entity-source and visibility fields.

The editor can preview the rendered footprint and conservative collision footprint separately with `Debug Render Mask` and `Debug Collision Mask`.
