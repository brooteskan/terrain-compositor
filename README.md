# terrain-compositor

`terrain-compositor` is an O3DE Gem for deterministic, ordered terrain
composition. It layers image and mesh height stamps over any procedural
gradient source, composes surface tags, represents terrain holes and mesh
cutouts, and publishes matching render, raycast, and heightfield-collision
state.

The repository is intentionally separate from any consuming game project.
Its O3DE adapter lives at `integrations/o3de` as an external Gem.

## Source checkout

Clone recursively, or run `git submodule update --init --recursive` after cloning.
All repository dependencies are pinned submodules; no local checkout paths or
CMake network fetches are required. To add this Gem to another repository, use
`git submodule add https://github.com/brooteskan/terrain-compositor.git Gems/terrain-compositor`
and then initialize its submodules recursively.

## O3DE integration

Add the Gem directory to a project's `external_subdirectories` and enable
`TerrainCompositor`:

```json
{
  "external_subdirectories": [
    "Gems/terrain-compositor/integrations/o3de"
  ],
  "gem_names": [
    "TerrainCompositor"
  ]
}
```

The Gem depends on O3DE's `GradientSignal` and `Terrain` Gems. It carries the
terrain material and shader extensions required by render cutouts and mesh
height gaps, and applies version-pinned Terrain source overrides from its own
integration directory.

## Compatibility

The initial extraction retains every public runtime, editor, configuration,
and data-cache UUID from TGProject. Existing serialized levels and prefabs can
therefore load the same component types after the owning Gem changes. Public
C++ APIs now use the `TerrainCompositor` namespace and
`<TerrainCompositor/...>` include path.

See [the O3DE integration notes](integrations/o3de/README.md) and the
[composition contract](docs/TerrainComposition.md) for details.

The [coverage traversal contract](docs/TerrainCoverageTraversal.md) documents
pinned polytree/algo submodules, incremental
selection and resource-aware raster/RT change batches.

The [architecture refactoring plan](docs/ArchitectureOptimizationReadiness.md)
ranks the work needed to prepare for terrain-query and sector-scheduling
optimization, using the camera-flight investigation in TG #37.
