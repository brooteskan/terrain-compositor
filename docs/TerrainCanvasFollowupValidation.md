# Material Canvas follow-up validation

This record covers the corrections in [issue #15's follow-up](https://github.com/brooteskan/terrain-compositor/issues/15#issuecomment-5809305511).
It does not close the broader issue's outstanding feature requests.

Implementation is applied to `main` in both local terrain-compositor checkouts:
`D:/wzmono/terrain-compositor` and `D:/TG/TGProject/Gems/terrain-compositor`.
The baseline repository revision is `69bc332301b21d962111785954f7a73e81be98f4`;
the tested O3DE revision is `061180bf24f1666eb30315b35da292eb14f4659c`.
Validation was performed on the local working trees before committing; use Git
history for the resulting implementation commit and TG's submodule reference.

## Delivered behavior

- New authoring uses stock Canvas interpolation, multiplication, smooth step,
  world position and world normal. Optional migration preserves old literal
  defaults, slot connections, node identities and UI metadata. Unsupported type
  conversions are refused; writes create exclusive backups and are idempotent.
- Reusable procedural nodes own independent shader dependencies. Normal from
  Height accepts explicit position and base-normal inputs. Its legacy UUID still
  reads the incoming terrain shading normal through a compatibility adapter.
- Legacy UUIDs remain registered under Compatibility. Terrain Output is under
  Material Outputs. Old helper include paths forward to the reusable implementations.
- The native compiler override remains hash-pinned, with source-drift, patched
  output-drift and disconnected-versus-explicit-value checks. See the
  [maintenance decision](TerrainCanvasCompilerCompatibility.md).
- Renderer messages use terrain-material wording while serialized identities,
  material properties and generated `_Tint` filenames remain compatible.

## Automated and native checks

Eight CTest suites pass: retained tint parity, surface math, native graph migration
parity, tint migration, reusable migration, node-library contracts, compiler
contracts and compiler-override compatibility.

The retained tint test compares 1,179,648 GPU pixel pairs with maximum error 0.
All ten independently native-compiled legacy/migrated graph pairs also match
exactly. Surface-gradient tests include tilted, mirrored and vertical geometry,
explicit shading normals, zero strength and degenerate derivatives; maximum error
is 0.0000012517 against a 0.00001 tolerance. Texture sampling in the numerical
migration harness uses a deterministic surrogate; real shader binding and include
closure are validated separately by Asset Processor.

Native Canvas generated and saved 28 distinct controlled graphs: ten legacy,
ten migrated, six ordinary Standard PBR fixtures and two compiler-default probes.
Round trips preserve node identities and connections. The initial five generation
runs exited 0. Each run records executable/source hashes separately from asset results.
The executable comes from `TGProject/build/windows/bin/profile`.

After rebuilding every consumer module, an additional run of both compiler probes
generated and verified the expected inherited/explicit assignments, then exited
with `0xC0000005` during shutdown. This reproduces the previously observed tool
shutdown fault; it is not a clean tool-process result. Both compiler processes
used by the live graph-reload test exited 0 with the rebuilt executable.

All 174 jobs associated with these fixtures finished with completed status and
zero errors, including native AZSL processing for the Standard PBR pipelines.
Two reference-material jobs initially ran before their shader dependencies were
ready; a subsequent AssetProcessorBatch run completed them and exited 0.

## Live Editor checks

The dedicated terrain Editor run exited 0. Direct and clipmap material selection,
incompatible-material fallback, graph reload and game-mode activation passed.
The blue reload probe changed 1,059,980 pixels; restoring the graph restored the
exact image below row 160. Both reload compiler processes exited 0.

Stationary baseline/pass-through/restored comparisons cover 1,071,532 RGB pixels
below row 160 at each near/far camera. Direct mode is exact at both distances.
Clipmap mode differs in one pixel by one 8-bit channel value at each distance,
against both initial and restored baselines.

The ordinary-mesh lighting run exited 0 after six captures on a temporary plane.
Across 1,060,598 pixels below row 160, the tilted procedural-normal material
matches its analytic reference exactly. The flat case differs by one channel
value in three pixels. Zero strength changes 99,635 pixels in the flat pose and
140,445 in the tilted pose, confirming that the bump input affects lighting.
The initial fixture run exposed an integer/float Python binding mismatch; the
validator now supplies float arguments and the corrected run passes. The entity
is deleted and the level is never saved by these scripts.

## Preservation and evidence

The consumer's 16 pre-existing edited/untracked asset paths are protected by
baseline hashes. The initial Output category correction is incorporated. Source
synchronization skips the protected assets and verifies their bytes; older graphs
continue to work through the compatibility nodes. User-owned example `.material`
files are not regeneration targets.

Local detailed evidence is under `build/issue15-followup`: baseline backups,
native generation/provenance reports, asset-job records, build logs, CTest output
and image-comparison results. Editor captures are under TG's
`user/TerrainSurfaceValidation` and `user/CanvasPortableValidation`.
Generated fixture sources are archived under `build/issue15-followup/native`,
outside TG's asset scan folder. The local CTest configuration points at that
archive. To repeat native/Editor checks, copy it back to the controlled
`TGProject/Assets/CanvasFollowupValidation` folder and process its assets.

The reproduced headless shutdown fault remains outside this change. No isolated
graph-performance claim is made.
Emissive, arbitrary named properties/multiple textures, POM, clear coat and
displacement remain outside this follow-up. Broader scene-regression boundaries
are described in [the surface report](TerrainCanvasSurface.md).
