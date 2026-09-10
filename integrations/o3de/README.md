# TerrainCompositor O3DE Gem

Register this directory as an O3DE external subdirectory and add
`TerrainCompositor` to the consuming project's enabled Gems. The Gem owns the
runtime and editor components, immutable query snapshots, data caches, terrain
engine integration sources, material, and shaders as one lifecycle boundary.

Engine patches target O3DE revision `061180bf24f1666eb30315b35da292eb14f4659c`.
Configuration requires Git, verifies each upstream file's SHA-256 (with normalized
line endings), and applies `EnginePatches` into the private build directory.
It also verifies the generated output hashes before replacing Terrain sources.
Unexpected engine changes or patch results fail configuration; the engine checkout
is never modified. Original license notices remain in the generated sources.

Public headers are under `Code/Include/TerrainCompositor`. The initial
extraction changed the C++ namespace from `TGProject` to `TerrainCompositor`
while retaining serialized UUIDs and component display names.
