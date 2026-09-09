# TerrainCompositor O3DE Gem

Register this directory as an O3DE external subdirectory and add
`TerrainCompositor` to the consuming project's enabled Gems. The Gem owns the
runtime and editor components, immutable query snapshots, data caches, terrain
engine integration sources, material, and shaders as one lifecycle boundary.

The engine overrides are based on O3DE revision
`061180bf24f1666eb30315b35da292eb14f4659c`. Configuration fails deliberately
if the expected upstream Terrain sources cannot be found; silently compiling
against a different Terrain implementation would make render, query, and
collision semantics disagree.

Public headers are under `Code/Include/TerrainCompositor`. The initial
extraction changed the C++ namespace from `TGProject` to `TerrainCompositor`
while retaining serialized UUIDs and component display names.

