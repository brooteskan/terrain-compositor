# TG consumer delivery

This directory owns the TG-side updates shipped with graph material clipmaps. TG uses this repository as `TGProject/Gems/terrain-compositor`; its consuming root is managed by Diversion. Updating only a detached local Gem checkout does not publish changes: the source must be committed and pushed here.

## Updating TG

Update this checkout from GitHub `main`, then build TG normally. The base Gem's CMake configuration invokes `SyncProject.py` for projects containing the VueLiveLink importer. Changes to the manifest and managed files trigger reconfiguration. No extra TG CMake patch or machine-local installer is required. A pinned Git submodule update follows its parent's recorded commit; use a remote update or advance that pin to consume a newer main commit.

```text
git -C TGProject/Gems/terrain-compositor switch main
git -C TGProject/Gems/terrain-compositor pull --ff-only origin main
cmake --build TGProject/build/windows --config profile --target TGProject.Editor MaterialCanvas
```

To synchronize or inspect without building:

```text
python TGProject/Gems/terrain-compositor/integrations/tg/SyncProject.py --project TGProject
python TGProject/Gems/terrain-compositor/integrations/tg/SyncProject.py --project TGProject --check
```

## Owned consumer files

`manifest.json` lists 23 managed copies. `project/` contains their canonical source:

- VueSimpleTerrain and MountainExport2 generated clipmap shaders/material types, plus the Vulkan forward-shader target setting.
- Vue terrain normal generation in signed RGBA16F, independent RGBA32F height settings for retained-terrain overlap, and the full-strength normal smoothing shortcut.
- The two already-converted normal DDS fields and their image settings, retaining the same asset paths and ownership.
- Small test entry points that delegate to the canonical tests in `tests/editor` and `tests/tg`. The editor camera settings live in `tests/editor/VueSceneBenchmark.json`.

Normal terrain input, graph source, levels and unrelated importer code remain consumer-owned. This integration does not change geometry LOD or saved levels. Normal-field serialization and generated material updates are the same ones already validated in TG; synchronizing them is idempotent.

The synchronizer preflights every destination before writing. It accepts the recorded prior source revisions, the current canonical source, and equivalent JSON formatting for recorded shader/material data. Unknown consumer edits stop configuration with the conflicting paths and remain intact. Make future changes in the canonical source here and add the previous release hashes to the manifest when updating managed files. This avoids silent project-only forks.

## Checks

`python tests/tg/TestProjectSync.py` covers installation into a fresh consumer, idempotence, previous-version replacement, read-only inspection, path confinement and preservation of unknown edits with no partial update.

The TG normal tests can run from this checkout inside TG. For another checkout, set `TG_PROJECT_ROOT` to the consuming project and run `python -m unittest discover -s tests/tg -p "test_*.py"`. The full importer tests also exercise unchanged TG importer modules, so they require that consumer source.

The editor checks can run directly from `tests/editor` using Editor `--runpython <script>`, or through TG's generated compatibility entry points. Outputs default to the consumer's `user` directory; report-root environment overrides are documented in the scripts. They restore temporary edits and do not save authored levels.
