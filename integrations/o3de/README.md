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

## Default terrain collision

The Terrain Composition Height Provider automatically supplies PhysX collision
at runtime. **Enable Collisions defaults to true**, including existing provider
documents that omit the new field. Procedural ground, heightmaps, and registered
terrain-mesh height stamps all use the final composed heights and holes. No
per-surface collision allowlist or manually placed heightfield is required.
PhysX5 is a Gem dependency. Standalone props still use their own colliders.

Collision is streamed in stable, world-aligned 64-metre tiles around characters,
rigid bodies, and the active viewport camera. The default coverage radius is
128 metres per observer and physics samples are 0.5 metres apart. This avoids
allocating a full heightfield for very large terrain regions. New tiles finish
cooking before the physics step; adjacent tiles share sample endpoints and
existing heightfield notifications update them after terrain edits. At most
1,024 tiles are retained per provider. Collision Range (16–512 metres) and
Collision Grid Spacing (0.25–4 metres) are editable on the height provider.
Disable Enable Collisions when supplying a custom collision setup instead.
An existing PhysX Heightfield Collider on the provider's entity takes precedence
over automatic tiles, preserving its authored material and collision filters.

Only registered composition contributors affect the composed collision surface.
A rendered mesh that has not been registered as a terrain-mesh height stamp is
not implicitly converted into terrain. Holes remain non-colliding by design.
Editor preview does not start this runtime simulation; enter game mode to use it.

### Refreshing imported collision surfaces

The Terrain Composition Gradient inspector includes **Refresh collision
surface** and a progress label. The button is disabled while a refresh is in
progress. This is an Editor-only extension point for project source converters;
ordinary registered contributors still update through their existing change
notifications.

A project listens to `TerrainCollisionSurfaceRefreshNotificationBus` /
`OnRefreshCollisionSurface(compositionEntityId)`, then reports progress through
`TerrainCollisionSurfaceEditorRequestBus` addressed to that compositor:
`SetRefreshStatus(message, busy)`. Both buses are reflected in `azlmbr.editor`.
Set `busy=false` on success or failure. `RefreshCollisionSurface`,
`GetRefreshStatus`, and `IsRefreshInProgress` also support automation using the
same callback as the inspector button. No project handler produces an
actionable status instead of starting work. Requests never run at runtime.

TG's Vue integration captures current Editor placements, prepares assets in a
background worker, waits for Asset Processor, and validates the updated height
queries. Successful refreshes leave level saving to the user.
