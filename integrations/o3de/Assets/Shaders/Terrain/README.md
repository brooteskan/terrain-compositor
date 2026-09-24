# Integrated terrain shaders

Geometry and cutout/gap checks feed a per-fragment graph material, followed by channel validation, decals and lighting. Each generated material owns forward/depth/shadow wrappers with a shared material declaration. Depth and shadow evaluate geometry and cutouts only.

`TerrainSrg` contains geometry mesh/LOD data. `TerrainMaterialSrg` retains renderer publication resources and adds graph-owned `MaterialParameters`; Atom supplies texture/sampler access and missing-image behavior. There are no shading clipmaps, macro/detail sampling paths, distance fades or legacy tint evaluation.

The default is bundled under `Materials/Terrain` and loaded through `DefaultPbrTerrain.material`. See `docs/TerrainCanvasSurface.md` for authoring and validation.
