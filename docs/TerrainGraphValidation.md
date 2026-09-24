# Terrain graph verification — 2026-09-24

Implemented directly on `main` in the canonical terrain-compositor checkout and the TGProject Gem checkout. The shared O3DE checkout was not edited. Generated default shaders and material type are included in the base Gem; all three maintained examples have native-saved graph files and generated sources.

## Verified

- Built `MaterialCanvas`, `Editor`, `TGProject.GameLauncher`, and `TerrainCompositor.Tests` in the existing Windows profile build.
- Passed all four standalone Canvas suites, including D3D11 channel/default/normal tests, node portability, generated contracts/material-instance property names, and compiler patch drift checks.
- Passed 30 focused native terrain tests, including variable material layouts, malformed renderer binding rejection, normals, cutouts, and height gaps. GPU classification compared 142,560 boundary samples with zero mismatches.
- Passed pinned Terrain override input/output hash and drift rejection checks.
- Native Material Canvas generated and saved all three graphs. Source checks and generation provenance are in `build/graph-surface/native-generation`.
- Asset Processor completed all 20 expected shader/material-type/material jobs with status Completed and zero errors. This includes the base Gem default and all three example forward/depth/shadow sets. Recorded in `build/graph-surface/asset-processor-results.json`.
- Editor validation on `TerrainMeshHeightGapAcceptance` passed all three selections, near/far captures, rapid switching, clearing selection, independently assigned textures, edited material values, missing-image fallback, incompatible contract rejection, formula/shader reload, an intentional shader compilation failure, recovery, and entering/exiting game mode. The script restores instance and generated shader files and never saves the level.
- That scene published one closed cutout (8 vertices, 12 triangles) and three gap instances sharing one 8-byte mask. Graph selections and appearance edits preserved the composition revision; shader reload re-bound the existing publication. New revisions during entering/exiting game mode are expected. Captured pass trees contain no shading clipmap passes.
- The standalone profile launcher loaded the acceptance level and published the same nonzero cutout/gap data. The smoke test used the local asset cache and was stopped after observation. It was not a packaged release-build test. The project's automatic level-load registry file was backed up and restored byte-for-byte; the standalone run used an explicit console command file to avoid repeated startup load commands.

Editor evidence: `D:/TG/TGProject/user/TerrainGraphGapValidation/results.json`, PNGs and timestamp JSON files. Native/runtime logs and test reports: `D:/wzmono/terrain-compositor/build/graph-surface`.

## Representative pass timings

AMD Radeon RX 7900 XTX, DX12, 1562 × 839, MSAA 2×, acceptance scene far camera. Median of three captures per graph, milliseconds. These are whole raster-pass measurements including other scene draws, not isolated terrain benchmarks.

| Material | DepthPass | Forward |
|---|---:|---:|
| Default | 0.2420 | 0.3625 |
| Procedural | 0.2395 | 0.4505 |
| Textured | 0.2410 | 0.4115 |

## Tooling limitation

Native generation and Asset Processor success are verified separately from process shutdown. Material Canvas completed graph compilation and save, but unattended shutdown did not consistently complete: observed exits included `0xC0000005`, and a live stack captured a shutdown wait in `AzToolsFramework::AssetBrowserComponent::Deactivate` joining its update thread. Clean unattended Material Canvas exit remains unverified. No shared-engine shutdown workaround or forced successful exit was added. The Editor rendering/reload checks above use the generated assets and passed independently.

The source tests and native checks do not constitute a full engine regression suite, a packaged-release certification, or exhaustive validation of every stock Canvas node. Stock world-position/normal, scalar/color inputs, texture sampling, procedural functions and ordinary arithmetic are exercised; the forward wrapper also declares ViewSrg before graph code for view-dependent expressions.
