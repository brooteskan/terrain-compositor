# TGProject extraction inventory

## Baseline

TerrainCompositor was extracted from `brooteskan/TG` commit
`425908e44a3d38a88a9e9f7ee12b12b4b4db8f1a`. This document records the
initial ownership boundary; later development need not remain byte-for-byte
identical to that revision.

## Owned runtime boundary

The external Gem owns:

- image height data, caching, placement, reconstruction, and ordered sampling;
- surface composition and terrain-existence sampling;
- image stamps, mesh cutouts, mesh-height stamps, and their editor adapters;
- composition gradient, height-provider, and surface-provider components;
- immutable render/query/collision snapshots and their feature processor;
- the version-pinned O3DE Terrain renderer, raycast, and collider overrides;
- the matching terrain material and shaders.

The consuming TGProject retains game-specific code: tank input and behavior,
the Cognition-to-ScenePolytree bridge, and the terrain-follow camera. Sample
levels, authored heightmaps/meshes, and acceptance scripts also remain with
the game project because they describe that project's content rather than the
Gem's reusable implementation.

## Compatibility contract

All extracted runtime component, editor component, configuration, reflected
value, and cache-interface UUIDs retain their TGProject values. The C++
namespace and include root changed to `TerrainCompositor`, but component class
names remain unchanged, allowing existing prefab `$type` names to resolve
without an asset migration.

New UUIDs were allocated only for the `TerrainCompositor` runtime module,
editor module, and system component.

## Dependency direction

```text
O3DE AzCore / AzFramework / Atom / GradientSignal / Terrain
                              |
                              v
                    TerrainCompositor Gem
                              |
                              v
                      consuming game Gems
```

TerrainCompositor has no dependency on TGProject, ScenePolytree, Cognition,
tank gameplay, camera behavior, or project-authored levels.

