# Material Canvas follow-up implementation plan

Status: implemented on both local `main` working trees on 2026-09-24.
See [the validation record](TerrainCanvasFollowupValidation.md) for results and
the retained MaterialCanvas shutdown limitation. The sections below retain the
implementation sequence and acceptance criteria used for this work.

This plan addresses the [follow-up corrections on issue #15](https://github.com/brooteskan/terrain-compositor/issues/15#issuecomment-5809305511): items 1–5 as a coordinated node-library repair, the outstanding Output category change (6), compiler maintenance (8), and terminology (11). It does not represent completion of the broader [issue #15 acceptance criteria](https://github.com/brooteskan/terrain-compositor/issues/15).

## Baseline and findings

The inspected terrain-compositor revision is `69bc332301b21d962111785954f7a73e81be98f4`. The engine checkout is `061180bf24f1666eb30315b35da292eb14f4659c`. The primary checkout is `D:/wzmono/terrain-compositor`; TG consumes a separate checkout at `D:/TG/TGProject/Gems/terrain-compositor`.

Both checkouts already have the uncommitted `Terrain Output` category edit from `Terrain` to `Outputs`. TG also has modified procedural-ground and procedural-tint assets and additional generated files. Preserve those edits; do not synchronize the checkouts by overwriting the example directories.

The source inspection establishes the following implementation constraints:

| Finding | Consequence |
| --- | --- |
| Stock Linear Interpolate, Multiply, and Smooth Step use `slotDataTypeGroups`; their default types are generally `float4`. | A UUID substitution alone is insufficient. Migrate slot names, explicitly preserve old defaults, and verify native scalar/vector promotion. |
| Stock World Position and World Normal use `O3DE_MC_POSITION_WS` and `O3DE_MC_NORMAL_WS`. The terrain template already supplies both. | Reuse these nodes for new terrain graphs. World Position already has a Z output for elevation. |
| Terrain Geometry's slope is exactly `1 - saturate(geometricNormal.z)`. | Preserve this dimensionless Z-up mask; replacing it with an angle, `abs(z)`, or a shading-normal calculation would change appearance. |
| The normal helper accepts explicit arguments, but the node supplies `context.worldPosition` and `incoming.normal` implicitly. | Most of the work is in the node contract, compatibility adapter, and shader dependencies. |
| `SurfaceHelpers.azsli` includes the terrain surface header to obtain `TC_SafeNormal`. | Declaring an include on the node is not enough; remove the helper's transitive dependency on terrain. |
| Native Canvas collects node `includePaths` into generated includes, including settings from disconnected nodes. | Declare helper ownership on each relevant config and test helper inclusion with disconnected and repeated instances. |
| Existing GPU tests strip `#include`/`#pragma` lines and concatenate terrain helpers. | Keep these numerical tests, but add real Canvas → Asset Processor → AZSL tests to detect dependency failures. |
| The pinned DynamicNode config/palette implementation exposes no per-node hide/deprecation switch. | Retain compatibility definitions in a clearly labeled category. Do not invent an unsupported `hidden` field or patch the palette just to hide them. |
| Standard PBR converts its Normal input through `GetWorldSpaceNormal((real2)inNormal, ...)`. | A world-space normal cannot be connected directly to that socket as a portability test. Its normal encoding must be handled explicitly. |
| The compiler patch contains both disconnected-value inheritance and the optional source-generation-only workaround. | Review these two maintenance obligations separately. Removing one does not automatically remove the other. |

## Intended boundaries

Keep terrain-specific inputs and renderer integration in the existing TerrainCompositorCanvas Gem. Store reusable configs under a proposed `Assets/MaterialCanvas/Procedural/Nodes` directory and reusable helpers under `Assets/ShaderLib/MaterialCanvas/Procedural`. Palette categories describe operations, independently of disk location.

This change makes these operations usable with non-terrain outputs. It does not claim that installing the Gem becomes independent of TerrainCompositor: the Gem dependency remains for its terrain output and scene component. A separately distributable generic Gem is unnecessary for these corrections.

New reusable nodes must use explicit sockets, declared shader includes, and stock Canvas context inputs where appropriate. They must not reference `incoming`, `context`, `TerrainSurfaceChannels`, `TerrainMaterialSrg`, or terrain-only shader headers. Only the terrain output, surface inputs, fixed renderer-resource inputs, and explicitly marked legacy terrain adapters may depend on that context.

## 1. Establish compatibility fixtures and a controlled baseline

Before implementation:

1. Record repository/engine revisions, dirty files, the actual MaterialCanvas executable path and hash, compiler patch hashes, and relevant generated assets. Store implementation evidence under a new `build/issue15-followup` directory rather than replacing the earlier record.
2. Capture immutable copies of the current node definitions and representative serialized graphs before changing defaults. Include all ten checked-in examples and focused legacy-node fixtures. Keep fixtures separate from examples that will be migrated.
3. Record old config UUIDs, input/output names, literal defaults, value types, and graph/UI metadata. Use the checked-in definitions as the migration source contract, not whatever defaults happen to exist in the engine at migration time.
4. Prepare a disposable graph directory inside a project scan folder for native compiler tests. Avoid using TG's edited examples as test input. Stock Standard PBR generation also writes a `.material`; these tests must use disposable materials.
5. Refresh the existing migration/numerical baseline before changing shader math. Record source generation, asset processing, and process exit as separate results.

**Acceptance:** original graphs and user-owned material bytes are recoverable, baseline provenance is recorded, and implementation tests operate on controlled assets.

## 2. Package reusable shaders and expose explicit bump-normal inputs

Primary files: `integrations/o3de-material-canvas/Assets/ShaderLib/TerrainCanvas/{LatticeNoise,SurfaceHelpers}.azsli`, the current `noise` and `normal_from_height` configs, and `Terrain/Templates/MaterialGraphName_Tint.azsli`.

1. Move the noise implementation into `MaterialCanvas/Procedural/LatticeNoise.azsli`. Keep its hash constants, interpolation, signed-coordinate handling, seed type, function signatures, and output range unchanged.
2. Put the derivative normal operation in `MaterialCanvas/Procedural/NormalFromHeight.azsli`. Remove the include of `TerrainCompositor/TerrainSurface.azsli`. Give the operation a small, uniquely named safe-normal helper with the existing finite/length checks. Keep the renderer's validation helper in the base Gem; do not make the base renderer depend on its optional Canvas integration.
3. Retain old helper paths as thin include forwarders and retain callable function names for already-generated source. Use include guards/`#pragma once` so old and new include paths can coexist without duplicate definitions.
4. Add native `settings.includePaths` to the noise config and both the new and legacy normal configs. Have each config name the helper it uses. Existing generated source can still use the old forwarders.
5. Remove the unconditional noise and bump-helper includes from the terrain template once node declarations cover every caller. The terrain output continues to include its own SRG/surface contract. A pass-through graph should not acquire procedural helpers unless such nodes are present in the document.
6. Add a new reusable Normal From Height node with a new UUID and the following contract:

| Socket | Type | Contract |
| --- | --- | --- |
| `inPositionWS` | `float3` | Explicit world-space position, in meters. |
| `inBaseNormalWS` | `float3` | Explicit base shading normal in world space; normalize safely. |
| `inHeight` | `float` | Scalar height in meters, evaluated at the same fragment. |
| `inStrength` | `float` | Dimensionless multiplier; default 1. |
| `outNormalWS` | `float3` | Unit world-space shading normal with safe fallback. |

Use neutral literal defaults for unconnected position/normal/height and describe the required position and normal wiring. Do not use `unconnectedValueExpression` or implicit terrain inputs on the new node. Keep the current surface-gradient calculation, determinant threshold, and zero-strength behavior. Use distinct internal slot names to avoid native substitution collisions.

7. Retain the old normal UUID and its `height`, `strength`, and `outNormal` sockets as a legacy terrain adapter. It still supplies terrain world position and the incoming **shading** normal to the generic helper. Label its terrain-specific behavior explicitly and place it under `Compatibility`.
8. Migrate an old normal node to the new config plus explicit stock World Position and Terrain Surface Inputs → Normal connections. Preserve its old height/strength literals and connections. Reuse suitable source nodes already in the graph where possible. Never substitute geometric normal for the old incoming shading normal.

Pixel Footprint and Normal From Height are fragment-stage operations. Document derivative requirements, including uniform evaluation across derivative quads. Do not advertise them for vertex/displacement evaluation.

**Acceptance:** fresh native shaders using the generic noise and normal nodes compile without terrain headers or symbols, including a graph containing both helpers and several instances of each. Legacy generated files still resolve through old includes. Numerical normal and noise behavior remains within the established tolerance.

## 3. Consolidate math and geometry; correct the palette

Use this final authoring map:

| Current node | Preferred authoring form | Category/compatibility treatment |
| --- | --- | --- |
| Lerp RGB / Lerp Scalar | Stock Linear Interpolate | `Math Functions`; retain old UUIDs under `Compatibility`. |
| Multiply RGB / Multiply Scalar | Stock Multiply | `Math Functions`; retain old UUIDs under `Compatibility`. |
| Smoothstep Scalar | Stock Smooth Step | `Math Functions`; retain old UUID under `Compatibility`. |
| Lattice Value Noise 2D | Retain its deterministic operation | `Procedural`; same UUID, explicit helper include. |
| Pixel Footprint | Retain its coordinate-footprint operation | `Math Functions`; same UUID, document fragment derivatives. |
| World Bands | Retain the small coordinate-driven convenience operation | `Procedural`; same UUID. Document that it uses its position input's X component. |
| World XY / Scale | Retain XY projection with protected denominator | `Math Functions`; same UUID. Document `max(scale, 0.0001)`. |
| Normal From Procedural Height | New explicit Normal From Height | `Math Functions`; new UUID, old terrain adapter under `Compatibility`. |
| Terrain Geometry | Stock World Position, World Normal, and scalar math | Stock inputs remain in `Vertex`; old config retained under `Compatibility`. |
| Terrain Surface Inputs | Keep composed terrain-channel access | `Terrain`. |
| Terrain Tint Parameters / Terrain Tint Texture | Keep fixed terrain-resource access | `Terrain`; explain the retained legacy tint bindings accurately. |
| Terrain Output | Keep terrain output | `Material Outputs`, using the existing stock Canvas category. |

World Bands and World XY / Scale are useful composites with defined behavior, rather than exact one-node duplicates of the three stock math functions. Retaining them avoids an unnecessary graph expansion. Revisit them only if a stock node with the same complete contract is identified. Do not change bands to a different period convention or change scale guarding while reorganizing nodes.

For Terrain Geometry migration:

- `position` → stock World Position `outPosition`.
- `elevation` → stock World Position `outZ`.
- `normal` → stock World Normal `outNormal`; the terrain template maps this to `context.geometricNormal`.
- `slope` → stock World Normal `outZ`, Saturate, then Subtract from 1. Preserve `1 - saturate(z)` exactly.

Create only the replacement nodes required by connected outputs, reuse suitable existing sources, and preserve all fan-out connections. The legacy geometry config can use `O3DE_MC_POSITION_WS`/`O3DE_MC_NORMAL_WS` in place of direct terrain context access, keeping its UUID, socket names, and formulas. Label it as legacy geometry rather than presenting ordinary geometry concepts as terrain-specific features.

Moving a config file must not leave two registered definitions with the same UUID. Serialized graphs reference `configId`, but also check tools, templates, docs, and fixtures for file-path references before moving files. Preserve legacy configs in a dedicated compatibility directory; a shader include forwarder is safe, while a copied node config would register a duplicate.

**Acceptance:** new authoring uses stock math/geometry where equivalent; new general-purpose nodes are outside Terrain; each old graph opens without missing configs; each registered UUID is unique. Items 1–5 are accepted together only after the portability and migration checks below pass.

## 4. Implement explicit, optional graph migration

Add a migration tool, for example `Tools/MigrateReusableNodes.py`, and extend `Tests/TestMigration.py` or add a focused companion suite. Share the graph-editing machinery with `Tools/MigrateTintGraphs.py` where useful, without changing the old tool's established compatibility contract accidentally.

The verified stock targets in the pinned engine are:

| Replacement | Config UUID | Slot remapping |
| --- | --- | --- |
| Linear Interpolate | `{8DD887CA-DA8E-4F55-9DFE-1B89EA0310AB}` | `a → inValue1`, `b → inValue2`, `weight → inValue3`, `result → outValue` |
| Multiply | `{7DE0F2F0-1ADC-4B4B-ADDB-CA21B8D97816}` | `a → inValue1`, `b → inValue2`, `result → outValue` |
| Smooth Step | `{8C2C6CEF-76DB-4567-A9E5-F39491A23454}` | `low → inValue1`, `high → inValue2`, `value → inValue3`, `result → outValue` |
| World Position | `{20D18D54-9A1D-4C4E-B676-F72097AC3A93}` | Geometry position/elevation to `outPosition`/`outZ` |
| World Normal | `{F1C815E8-0B44-43EA-8176-D075B5B2EA41}` | Geometry normal/slope source to `outNormal`/`outZ` |

Implementation rules:

1. Accept explicit graph paths, default to dry-run, and report changes by old node type. Do not scan and rewrite a user's project automatically.
2. Verify the target engine's config IDs, socket schemas, and relevant type-group behavior before writing. If the target contract is unsupported, stop with a useful report and leave that graph unchanged.
3. For direct replacements, preserve graph node IDs, `toolId`, unrelated data, UI positions, and connection metadata. Rewrite both endpoints as needed and retain fan-out. For geometry/normal expansion, allocate collision-free IDs and place new nodes near the old node.
4. Materialize every omitted legacy input default before switching configs. Examples: RGB lerp defaults are white/white with weight 1; scalar lerp defaults are 0/1 with weight 0; both multiply inputs default to 1; smoothstep defaults are 0/1/0. Preserve `float` versus `Vector3` values and raw working-RGB numbers.
5. Verify scalar weight broadcast for RGB interpolation, scalar/vector connections, and effective output widths through native compilation. Some old fixed-width slots may accept connected types that the stock grouped node promotes differently. Insert stock conversion nodes where necessary; otherwise report and skip the unsupported graph atomically. Never silently change its effective type.
6. Preserve unconnected terrain-output inheritance, connected constants, existing explicit Base Color expressions, and the hidden `inTint` multiplication exactly once.
7. Update `MigrateTintGraphs.py` to emit stock Multiply, using the same validated mapping. It currently creates `multiply_rgb`, so leaving it unchanged would reintroduce duplicates. Support applying the new reusable-node migration to graphs already migrated by the old tool.
8. Validate the complete transformed document before writing. Reject unresolved endpoints, duplicate IDs, unsupported graph variants, or multiple incoming connections where the schema permits only one. An unknown unrelated node is preserved; it is not deleted or guessed at.
9. Make writes per-file atomic, create an exclusive backup, refuse to overwrite an existing backup, and leave `.material` files untouched. A second migration must make no changes or new backups. Return a failure status for refused/failed files, with an explicit report of any other completed files.
10. Migrate repository-owned examples and regenerate their products using native Canvas. Retain unmodified legacy fixtures so compatibility continues to be tested. Reopen and save migrated graphs in Canvas to verify that their serialization and wiring survive a native round trip.

**Acceptance:** migration preserves appearance, defaults, connections, and metadata; dry-run writes nothing; repeat runs are idempotent; refusal paths leave original bytes intact. Existing graphs remain usable without migration.

## 5. Review and contain the compiler override

Primary files: `EnginePatches/MaterialGraphCompiler.cpp.patch`, `EngineOverrides.cmake`, `CMakeLists.txt`, and the output config/template in `integrations/o3de-material-canvas`.

Perform a bounded source/prototype review before choosing a replacement. The pinned compiler's value substitution methods are private, and MaterialCanvas creates the compiler through its application-owned document factory. The inspection did not establish a supported Gem hook that can replace only disconnected-value handling.

Evaluate existing template/node settings, graph/document preprocessing hooks, and compiler/document factory extension points. An acceptable alternative must:

- Distinguish a disconnected socket from a connected constant equal to its displayed default.
- Preserve per-channel inheritance and the legacy tint connection.
- Work for interactive compilation and headless automation, including document reload and multiple graphs.
- Avoid editing saved graphs merely to synthesize defaults.
- Avoid a second compiler implementation, fragile generated-text rewriting, numeric sentinels, or a larger engine override.

Record the candidate, prototype result, and rejection/adoption rationale. If no smaller supported mechanism satisfies these conditions, retain the current hash-pinned patch and mark that as the explicit outcome. Do not remove working inheritance merely to remove the patch.

For a retained override, add dedicated checks:

1. The pinned input hash and patched-output hash match; changing either source or patch produces an actionable configuration failure. Preserve the existing LF normalization and `.gitattributes` rules.
2. The build replaces exactly the intended source and compiles one MaterialGraphCompiler implementation. Confirm host-tool builds and runtime-only Gem configuration.
3. A native capability fixture verifies all disconnected channels inherit, each explicitly connected constant overrides, and stock non-terrain output defaults remain unchanged. Include zero and values equal to nominal defaults, so literal/sentinel implementations cannot pass accidentally.
4. Record executable path/hash, engine revision, patch fingerprints, and native fixture results in the compile report. A semantic probe, rather than filename alone, detects an executable lacking the inheritance capability. Fail the terrain validation preflight clearly instead of accepting generated literal defaults.
5. Document an upgrade procedure: inspect upstream changes, reassess whether the extension is still needed, update expected hashes deliberately, rebuild the tool, and run the same fixtures before adopting the engine revision.

Review `SourceGenerationOnly` separately. Preserve default interactive asset-status behavior. Continue to label the option as generation-only and record shader jobs and tool exit independently; the pre-existing shutdown fault is not fixed by this work. If an established supported mechanism can replace this workaround, validate it separately before removing its patch hunk.

**Acceptance:** either a demonstrated smaller supported implementation passes the same semantic tests, or the retained override has reproducible compatibility checks and an executable/upgrade procedure. A speculative extension proposal is not completion.

## 6. Correct terminology without breaking asset identity

Primary files: `integrations/o3de/EngineOverrides/TerrainRenderer/TerrainTintMaterial.inl`, Canvas component descriptions, validation scripts, and authoring docs.

Use neutral language for loading, staging, and failures before the selected contract is known:

| Current wording | Proposed wording |
| --- | --- |
| Loading terrain tint material | Loading terrain material |
| Tint asset failed; retaining the current valid terrain material | Terrain material asset failed; retaining the current valid terrain material |
| Incompatible tint material; retaining the current terrain material | Incompatible terrain material; retaining the current terrain material |
| Preparing terrain tint bindings | Preparing terrain material bindings |

Keep accurate distinctions such as `Canvas terrain surface active`, `Canvas terrain tint active`, and the legacy fallback status. Update exact string assertions in both Editor validators in the same change. Audit tooltips and documentation for descriptions that incorrectly characterize all surface operations as tinting.

Preserve serialized component UUIDs, `Material`, existing service/bus identities, hidden `inTint`, `tint.*` property identifiers, generated `.materialtype` references, and `_Tint.azsli` filenames in this correction. Explain that the generated filename is retained for compatibility even when the implementation evaluates a surface. A filename migration would add asset-reference risk without fixing user-facing terminology.

**Acceptance:** failures and loading states describe terrain materials accurately; legacy materials, component assignments, automation buses, and generated references still resolve.

## 7. Validate the complete repair

Implement focused tests around the changed contracts, then reuse the current broader terrain regression tools. Category changes themselves do not require a new C++ test framework.

| Layer | Required coverage | Evidence/pass condition |
| --- | --- | --- |
| Config/dependency audit | Unique IDs, correct categories, declared helper ownership, no terrain symbols/includes in reusable configs/helpers, valid compatibility forwarders | Inspect definitions and generated include closure; no duplicate registration or hidden terrain dependency. |
| Migration tests | All five duplicate math configs; omitted/default/custom literals; scalar/RGB promotion; fan-out; geometry output subsets; normal expansion; old tint migration composition; malformed graph; backup refusal; idempotence | Exact structural expectations plus unchanged original bytes on failure and native round-trip checks. |
| Native generation | All ten current examples, unmigrated legacy fixtures, migrated equivalents, independent non-terrain fixtures | Fresh generated-file list and hashes tied to the input graph/config/tool revisions. No stale assets counted as new output. |
| Real shader/material compilation | Noise, footprint, bands, XY/scale, normal, and their combinations feeding stock Standard PBR; repeated/disconnected helper nodes | New successful Asset Processor jobs and AZSL products; no Terrain Output or manual terrain includes in the fixture. |
| Shader mathematics | Old/new scalar and RGB math; same noise values; world normals on horizontal, sloped and rotated surfaces; zero strength; constant height; degenerate derivatives; invalid-input fallback | Existing tolerance of `1e-5` for floating-point normal/surface comparisons unless a separately justified case requires another bound. Preserve deterministic noise expectations. |
| Compiler contract | Each disconnected channel; each explicit constant; hidden tint multiplication; stock outputs; wrong executable capability; patch hash mismatch | Native semantic fixtures and clear failure behavior, not only source-string assertions. |
| Terrain rendering | Legacy tint vs migrated tint; baseline vs pass-through; wet/ground/slope/normal examples; direct and clipmap near/far; selection/reload | Matched camera/settings, stabilized frames, recorded image comparison tolerances and successful active-material status. |
| Compatibility/ownership | Existing scene assignments, material parameters, failed candidates, generated paths, restored test edits | Same assets resolve; usable material retained after failure; user materials and saved levels remain untouched. |

Non-terrain normal validation needs two distinct fixtures:

1. **Dependency fixture:** route the world-space normal into a visible color expression such as `0.5 * (n + 1)` using stock math. This proves that the operation and its includes compile through an ordinary output, but is not sufficient evidence of correct lighting.
2. **Lit mesh fixture:** transform the result to the tangent basis expected by Standard PBR, using stock world normal/tangent/bitangent and math nodes where possible. Match the pinned Atom decoder: it consumes signed normal XY and swaps the input's Y/X components before reconstructing Z. Verify the required normal-override option is active. Test an ordinary rotated/sloped mesh and a known analytic height gradient; do not merely plug the world vector into Normal. Account for basis orientation/handedness and the decoder's positive-Z restriction.

Use separate draw cases or full derivative quads for normal tests. The old divergent-pixel test problem is already corrected; preserve that correction. Retain the existing corrected Editor arguments, component lookup, property matching, and camera types.

Update `Tools/CompileExamples.py` to report validation inputs and the output contract under test. Keep terrain and Standard PBR fixtures in separate directories: Standard PBR can generate `.material` files, whereas the terrain templates deliberately do not. Keep numerical shader tests separate from include-resolution tests, because their current source concatenation masks packaging defects.

Run and record the existing standalone test commands after extending their registration:

```text
cmake -S integrations/o3de-material-canvas/Tests -B build/canvas-followup-tests
cmake --build build/canvas-followup-tests --config Release
ctest --test-dir build/canvas-followup-tests -C Release --output-on-failure
```

Use the rebuilt native MaterialCanvas and `Tools/CompileExamples.py` with `TC_CANVAS_GRAPH_ROOT` pointing to the controlled scan folder. Inspect current Asset Processor jobs separately. Reuse `Tools/ValidateSurfaceEditor.py` for the terrain scene checks after updating its expected terminology. Extend the test harness with an ordinary-mesh check for the generic normal operation.

A source-generation success plus a crash on exit must be reported as those two facts. Existing whole-Forward-pass timings do not establish an isolated graph cost; do not claim a performance improvement from this refactor. Any unexpected visual difference must be explained or fixed before compatibility is accepted.

## Delivery sequence and completion criteria

Use reviewable commits with these dependencies. The node correction is released only after its integrated validation passes.

1. **Baseline fixtures and migration contract.** Capture old configs/graphs and establish native comparison cases. Preserve and include the existing Output category correction when assembling the implementation change.
2. **Reusable helper ownership and explicit normal node.** Add generic includes, old-path forwarders, and the legacy normal adapter; establish the first non-terrain native compile fixture.
3. **Stock-node consolidation, graph migration, and final categories.** Implement mappings, geometry expansion, example migration, and the tint migrator update together. Regenerate repository-owned products.
4. **Compiler maintenance decision and checks.** Record the extension review; implement the chosen smaller mechanism or retained-override tests. This can be investigated independently, but any compiler change must precede final regeneration and validation.
5. **Terminology and authoring documentation.** Update renderer status strings and matching validators. Rewrite examples/instructions to use stock geometry/math and explicit normal inputs. Explain compatibility nodes, migration commands, retained filenames, and executable selection.
6. **Integrated verification and consumer handoff.** Run the matrix, review generated diffs and material hashes, then synchronize only reviewed changes into TG while preserving its local edits. Rebuild/restart affected tools, rerun consumer smoke checks, and record the exact tested revisions. Publishing commits or updating TG's submodule reference is a separate implementation delivery action.

Update `docs/TerrainCanvasSurface.md` and `integrations/o3de-material-canvas/README.md` to reflect the final architecture. Keep their historical validation record clearly separated from new results. Replace unqualified claims that issue #15 is implemented with the actual supported scope and remaining work.

The follow-up is complete when:

- [x] New graphs use stock equivalent math/geometry, while legacy UUIDs and graphs still load and compile.
- [x] Each reusable operation has no implicit terrain shader dependency and passes real non-terrain compilation; normal lighting also passes the mesh fixture.
- [x] Migration is optional, typed, backed up, idempotent, and appearance-preserving, including the old tint migration path.
- [x] Terrain Output is in Material Outputs in the delivered working trees; no generic operation is newly presented as terrain-specific.
- [x] Compiler inheritance has a documented, tested maintenance path.
- [x] Status wording is corrected without changing serialized or generated asset identities.
- [x] Native generation, Asset Processor jobs, GPU math, live rendering, and tool exits have separate recorded outcomes.
- [x] TG's local asset changes and user-owned materials remain intact.

Emissive, arbitrary named parameters/multiple textures, POM, clear coat, displacement, a new renderer, and resolution of the existing headless shutdown fault remain outside this follow-up. Track unmet original issue #15 requirements separately; these corrections do not justify closing the broader issue automatically.
