# TerrainCompositorCanvas

Author procedural terrain surfaces in O3DE Material Canvas using the existing terrain
renderer. Terrain surface assignments, height composition and collision remain in place.

## Authoring

Enable both TerrainCompositor and TerrainCompositorCanvas. TG already enables both.
Build MaterialCanvas and the affected Editor/runtime modules, then restart the tools.
This Gem applies a hash-checked native compiler override for context defaults on
unconnected sockets; use that rebuilt MaterialCanvas, not another engine installation.

1. Open `Assets/MaterialCanvas/Terrain/Examples/wet_terrain.materialgraph`, or start
   from `surface_passthrough.materialgraph`.
2. Use stock **World Position** and **World Normal** under **Vertex**. World Position's
   Z output supplies elevation; `1 - saturate(World Normal.Z)` supplies the existing
   Z-up slope mask. Use **Terrain Surface Inputs** for composed material channels.
3. Connect procedural results to **Terrain Output**. Unconnected channels preserve
   incoming terrain values. Connect a Constant node for an explicit fixed value.
4. Compile the graph and let Asset Processor finish shader and material jobs.
5. For a new graph, create an adjacent user-owned `.material` referencing its generated
   type, for example `{"materialType":"my_surface.materialtype"}`. Examples include
   these files. Graph compilation never writes a `.material` file.
6. Add **Terrain Material** to an entity in the terrain scene and select the processed
   material in **Material**. Status should read **Canvas terrain surface active**.
7. Inspect the actual terrain viewport. The stock Canvas mesh preview is not a terrain
   preview. Edit graph constants and recompile to change the selected surface.

Only one selector owns each scene. Clear its selection or disable it to restore legacy
terrain. The component UUID, serialized `Material` field and automation bus are retained.

## Channel contract

The graph runs once per terrain fragment after direct/clipmap sampling, composition and
detail fading, before decals and lighting. Explicit graph effects remain visible beyond
the detail fade distance. Incoming channels have no legacy noise tint; a Canvas graph
owns its surface effect exclusively.

| Channel | Meaning |
| --- | --- |
| Base Color | Linear renderer working RGB (ACEScg); finite and nonnegative, with values above one retained. |
| Normal | World-space shading normal, normalized with fallback to the incoming normal for invalid/zero vectors. |
| Roughness | Perceptually linear roughness, clamped to 0-1. |
| Metalness | Metallic-workflow factor, clamped to 0-1. |
| Specular Factor | Dielectric factor, clamped to 0-1; Atom maps this to F0 = 0.08 times the factor. |
| Ambient Occlusion | Existing diffuse ambient occlusion, clamped to 0-1. Inheritance preserves terrain's current fade policy. |

World position is in meters. Use geometric normal for slope masks; composed shading
normal includes the existing material detail. **Normal From Height** under **Math Functions**
takes explicit world position and base world-space normal, scalar height in meters,
and dimensionless strength. For terrain, wire World Position and Terrain Surface Inputs'
Normal into those inputs. Zero strength preserves the normalized base normal.
It changes shading only. Filter small noise features with Pixel Footprint to avoid distant
shimmer, as demonstrated by the examples. Compatible stock World Position, World Normal,
constant and math nodes work. Color vectors are working RGB, not display sRGB.

## Examples

- `surface_passthrough`: all incoming channels preserved.
- `wet_terrain`: filtered world noise darkens existing color and lowers roughness.
- `procedural_ground`: procedural color, roughness and bump normals.
- `slope_elevation`: ground/rock/snow colors selected by slope and elevation.
- `procedural_normal`: filtered noise changes only shading normals.
- `surface_channels`: explicit values for all six channels, also used by hardware tests.
- `minimal_tint`, `procedural_tint`, `world_bands`, `texture_tint`: existing tint graphs.

## Migration

Terrain Output retains the original node UUID and a hidden `inTint` compatibility
connection. Old graphs still multiply incoming base color exactly once. Generated
material-type paths and existing material parameter names are retained.

To make the old connection visible as Base Color multiplication:

```text
python Tools/MigrateTintGraphs.py path/to/graph.materialgraph --engine-root <o3de>
python Tools/MigrateTintGraphs.py path/to/graph.materialgraph --engine-root <o3de> --write
```

The first command is a dry run. Writing creates an exclusive `.materialgraph.bak` backup,
rewires the tint expression through a multiply node, and resets the compatibility input.
Existing explicit Base Color connections are preserved. Run on closed documents, reopen
in Canvas and compile. Migration is idempotent and does not modify `.material` files.

Use `Tools/MigrateReusableNodes.py` with the same arguments to replace legacy math,
geometry, and implicit terrain normal nodes with stock math/geometry and explicit
normal inputs. It preserves legacy defaults, typed values, connections, graph IDs,
and layout metadata. Unsupported narrowing conversions are reported without changing
that graph; add the explicit stock conversion before retrying. Both tools validate
the selected engine's stock node contracts, default to dry-run, and refuse existing
backup files. Run on closed documents. Existing graphs work without migration.

Legacy UUIDs remain registered under **Compatibility**. New graphs use stock
**Linear Interpolate**, **Multiply**, and **Smooth Step**. Noise and World Bands are
under **Procedural**; Pixel Footprint, World XY / Scale, and Normal From Height are
under **Math Functions**. Terrain Output is under **Material Outputs**. Procedural helpers
declare their own includes and also work with ordinary mesh outputs. Pixel Footprint
and Normal From Height require fragment derivatives and uniform evaluation across
derivative quads; they are not vertex/displacement operations.

For stock Standard PBR, transform the world-space normal to the world tangent basis
and pack signed tangent XY in the decoder's Y/X order before connecting Normal.
Enable the output's Normal input. See `Tests/Fixtures/Portable/normal_lit.materialgraph`
for an analytic height example and `procedural_helpers.materialgraph` for portable
noise/filtering. These are validation sources: compile copies into a disposable
project scan folder because stock Standard PBR also generates a `.material` file.

Generated `_Tint.azsli` names, `tint.*` material properties, and component/bus IDs
remain stable for compatibility. Surface graphs still evaluate the full supported
surface contract regardless of that retained filename.

Compare pass-through against terrain with legacy tint disabled; compare migrated tint
graphs against their original matching settings. These are distinct parity checks.

## Parameters, reload and scope

Surface contract v2 retains fixed TerrainMaterialSrg v1. Graph constants compile into the
shader. Existing `tint.strength`, `tint.color` and `tint.texture` remain material properties;
an unset texture binds neutral white. Tint color remains a raw multiplicative RGB vector.

Arbitrary named material properties and multiple texture bindings are follow-up work.
Do not use stock Material Input nodes requiring StandardPBR's separate `params`/bindless
material contract. Numeric procedural authoring uses graph constants and the existing
bindings without changing renderer resources.

Emissive, clear coat, opacity, transmission, subsurface, displacement and POM are not
exposed. Terrain's current parallax properties supply material-blend height, not POM.
Future POM needs view-dependent coordinate displacement before texture sampling and an
explicit clipmap strategy. The normal node's procedural height is bump shading, not POM,
blend height or geometry displacement. Per-detail-material graph execution and ray-traced
graph appearance are outside this terrain-wide forward surface extension.

Scene-local selection stages renderer settings and cutout/gap publications before
switching draw packets. Failed candidates retain a usable material; an incompatible
in-place shader reload restores the default. Formula and existing parameter edits do
not request terrain recomposition, collider rebuilds or clipmap content regeneration.
They normally reload without restarting Editor. Compiler/Gem changes require rebuilding
and restarting tools. After a scene ownership conflict, resolve it and reselect the
material; scene discovery retains its bounded retry behavior.

When changing the terrain engine override, rebuild all consumers that link it:
`Terrain`, `Terrain.Editor`, `TerrainCompositor`, `TerrainCompositor.Editor`,
`TerrainCompositorCanvas`, `TerrainCompositorCanvas.Editor`, and `MaterialCanvas`.
Rebuilding only the Terrain DLL can leave old behavior in another loaded module.

## Validation tools

```text
MaterialCanvas --project-path=<project> --rhi=null --runpython=<absolute-gem-path>/Tools/CompileExamples.py --timeout=600000
cmake -S integrations/o3de-material-canvas/Tests -B build/canvas-parity
cmake --build build/canvas-parity --config Release
ctest --test-dir build/canvas-parity -C Release -V
```

`TC_CANVAS_GRAPH` restricts the filename glob; `TC_CANVAS_GRAPH_ROOT` selects another
graph directory within project scan folders. The compiler report records generated hashes.
`TC_CANVAS_REPORT_ROOT` selects a separate report directory, and `TC_CANVAS_ROUNDTRIP=1`
enables native save/wiring checks on disposable graph copies. Reports include compiler
provenance and reject incorrect disconnected terrain defaults. Configure standalone
tests with `-DTC_CANVAS_ENGINE_ROOT=<o3de>` for compiler-patch compatibility checks.
See [the compiler compatibility decision](../../docs/TerrainCanvasCompilerCompatibility.md).
Set `-DTC_CANVAS_VALIDATION_ROOT=<native-fixture-root>` to enable GPU comparison
of freshly compiled `legacy`, `migrated`, and `compiler` fixture directories.
Use copies of `Tests/Fixtures` inside a project scan folder for native generation;
do not compile the immutable test fixtures in place.
For automated generation, add `--regset=/O3DE/TerrainCanvas/SourceGenerationOnly=true`
to bypass the native asset-status wait; this does not prove shader compilation succeeded.
Check Asset Processor jobs and live rendering separately. The pinned tool has a previously observed shutdown
fault/status-wait issue: record process exits separately from generation, and do not
count stale products as successful compilation.

Hardware tests cover retained tint parity, native pass-through, surface-gradient normals,
invalid values and channel bounds. Migration tests run when Python is available.
`Tools/ValidateSurfaceEditor.py` runs with Editor's `--runpython <absolute-script-path>`
on the saved TG DefaultLevel, captures near/far direct/clipmap views, checks selection
and game mode, tests graph reload, then restores temporary edits without saving.
`TC_CANVAS_SURFACE_PARITY_ONLY=1` instead captures baseline/pass-through/restored
baseline at stationary near/far cameras. Close unsaved work first.
`Tools/ValidatePortableEditor.py` captures explicit bump normals on a temporary
ordinary mesh, in flat and tilted poses, against zero-strength and analytic-reference
materials. Compile the `Tests/Fixtures/Portable` copies first; compare the captured
pixels separately from the script's capture-success result.
See [the surface implementation report](../../docs/TerrainCanvasSurface.md) for the actual
validation record and remaining scene coverage, and [the original tint report](../../docs/TerrainCanvasTint.md)
for historical checks.
The [follow-up validation report](../../docs/TerrainCanvasFollowupValidation.md)
records stock-node migration, non-terrain shader compilation, ordinary-mesh
lighting parity and the remaining native-tool shutdown limitation.
