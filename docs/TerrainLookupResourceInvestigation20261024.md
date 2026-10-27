# Terrain lookup compiler/resource investigation — 24 October 2026

On the Ryzen 7 5700G integrated GPU, the 1 KiB packed lookup prototype retained almost all of the earlier 4 KiB lookup's speed improvement, while reducing additional per-process DXGI local memory from 123 MiB to 27 MiB. These are shared-memory allocations, not dedicated VRAM.

| Matched final clean comparison | GPU frame median | Additional local usage |
|---|---:|---:|
| Original shader, three passes | 40.05–40.96 ms | baseline |
| 1 KiB conservative selection, two passes | 28.45–28.48 ms | 27 MiB |
| Same compiled option, invalid publication key; original selection executes | 28.75 ms | same allocation |

The invalid-key control is particularly useful: it bypasses tile classification and retains the original expensive selection, yet remains fast. Rejected contributors therefore do not explain the bulk of this improvement.

## Compiler evidence and limits

Controlled DXC/Radeon GPU Analyzer source probes show that dynamically indexed constant arrays become per-invocation private scratch. The final clean probe changes from 784 to 1,808 scratch bytes and from 255 to 194 vector registers. Earlier full-table probes added exactly 4,096 scratch bytes. These are offline source probes, not captured live driver pipeline ISA. They support a compiled resource-allocation explanation, but do not establish the precise driver heuristic or a measured occupancy increase. In particular, a smaller 256-byte table reduced registers in one probe but did not run faster in the renderer.

The initial full-table comparison measured 39.22 ms original versus about 27.68 ms lookup, with 123 MiB additional local usage. A 256-byte packed table used 3 MiB extra but ran about 39.6 ms. Tiny arithmetic, branch-only, explicit branch hints, and recomputing cheap UVs without retaining query arrays did not retain the speed gain. Samples were not eliminated by the lookup-only control: the relevant controlled DXIL probes contained the same 289 texture-sample calls.

## Appearance and controls

All performance variants matched the original captured view exactly across 701,014 RGB pixels after excluding the fixed HUD/gizmo mask. This verifies the tested view, rather than every terrain, camera or edit. The conservative 32/64-grid classifiers also passed 300,000 random selector comparisons each and packed-label round trips for all cells.

The camera was position (1025.073242, 488.140839, 43.456932), rotation (-13.292666, 4.552838, 161.428360), with a 1203×656 viewport, 2× MSAA and FP16 height arithmetic. Terrain6/7 remained visible. Sky atmosphere, HDRI sky, both cloud volumes and aerial atmosphere were disabled; scene lighting remained. Each case captured 240 frames after a 12-second initial warmup and 6-second settling period. Balanced original/candidate repeats limit ordering effects. Read-only AMD clock telemetry was equal in the measured timing interiors. No clock or power settings were changed.

All camera, material, visibility, precision, pacing and atmosphere/cloud changes were restored. The saved level's SHA-256 remained `faec5fd3f9b69643b918af3a917623776d31bf7cc962485cfba712313a36a922`. The task-owned benchmark editors were closed. Thirteen focused overlap tests and three bounds tests passed; this is not a full current regression-suite validation. A pre-existing nonfatal image-index assertion also occurs in baseline startup.

## Retained prototype

The initial prototype was opt-in (`contributorSelection.enabled=false`, `key=0`). Tile rejection is valid only for the frozen Terrain6/7 publication with a matching key and supported material configuration. It has no general edit/streaming invalidation system and must not be enabled with a stale nonzero key. The invalid-key fast pipeline is independent of that frozen data. See the subsequent default-rollout report for the table-free follow-up and the final adoption settings.

Detailed measurements and restoration records: `TerrainLookupResourceInvestigation20261024.json`. Source/probe snapshots: `tools/LookupResourcePrototype/`. No portable compiler binaries are bundled with these artifacts.

References for the tools and terminology: [AMD Radeon GPU Analyzer](https://gpuopen.com/rga/), [AMD GCN instruction set architecture](https://gpuopen.com/download/AMD_GCN3_Instruction_Set_Architecture_rev1.1.pdf).
