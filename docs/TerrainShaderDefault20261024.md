# Terrain shader default and table-free follow-up — 24 October 2026

> Updated 25 October: the faster default now exposes an accepted Detail Normal Maps quality setting, defaulting off. See [the texture sampling follow-up](TerrainTextureSampling20261025.md) for the additional gain and original-fallback validation.

The measured faster pipeline is now the default for the terrain compositor's TG material adapter. Normal contributor selection remains active: the frozen classifier's publication key defaults to zero. Existing owned terrain materials inherit the new default without scene edits or republishing. Disable **Terrain Shader → Optimized Shader** (`contributorSelection.enabled=false`) to compile the original fallback.

## Table-free experiments

Three reversible loop structures were tested in a balanced eleven-case run, with original and fast controls interleaved and repeated. None retained the speed gain without the table.

| Variant | Median GPU frame, ms |
|---|---:|
| Original | 39.20 |
| Material shading loop | 39.25 |
| Query and material loops | 40.35 |
| Height query loop | 41.45 |
| Known fast path; rejection bypassed | 26.51 |

All captures matched exactly after the fixed HUD/gizmo exclusion. Table-free variants allocated no additional local memory before first use of the fast pipeline. The fast pipeline allocated 25.5 MiB more in that run; allocations remained resident when returning to the original.

Offline source probes also tried DXC optimization levels O1/O2, flow-control preference Gfp, and noinline function boundaries. Valid probes still used 255 vector registers. The three loop probes reported 832/864/848 scratch bytes rather than the original 784. The noinline probes failed DXIL validation and were never installed; Gfa required an additional resource-binding contract and was not pursued. These are offline source probes, not live pipeline ISA. The investigation does not establish that a table-free solution is impossible.

## Shipping-default verification

Every experimental loop clone/option was removed. The original clean shader body was restored, changing the existing shader-option and material-property defaults to true. The `key` default stays zero. No frozen Terrain6/7 rejection is enabled, so edited or transformed sources continue through the original selection/shading logic. The 1 KiB indexed constant array remains in the compiled pipeline to retain the observed compiler/resource configuration; this is a measured workaround rather than a general compiler fix.

| Final clean source, matched view | GPU frame median | Additional DXGI local usage |
|---|---:|---:|
| Explicit original fallback | 39.24 ms | original fresh-process reference |
| Material inheriting new default | 27.64 ms | 29.0 MiB |

The default reduced GPU time by 29.6%. An explicitly enabled material was included as an inheritance control. Every new-default and fallback capture matched exactly across 701,014 RGB pixels. The new-default image also matched the original from the previous clean investigation. This verifies the tested camera and scene, not all authored landscapes or GPUs.

Both phases used the same camera, 1203×656 viewport, 2× MSAA and FP16 terrain heights, with atmosphere, HDRI sky, clouds and aerial atmosphere disabled. They used 240 frames per case, 12 seconds initial warmup, 6 seconds settling, CPU/GPU frame capture and additional enabled-pipeline timestamps. Memory is per-process DXGI local CurrentUsage on the integrated GPU, not dedicated VRAM; its allocation can vary with the compiled variants and persists until process exit. The final editor opens the enabled default before the first explicit fallback capture, so that fallback already retains the fast allocation. Its memory reference is the original fresh-process usage (1,790,799,872 bytes), independently identical in the preceding clean and loop runs. We do not interpret the final within-process memory plateau as a zero-overhead result. Raw per-case CPU/GPU times, memory, image comparisons and restoration records are in the accompanying JSON.

Fourteen focused overlap tests passed, including the generated-layout default, original-fallback connection and zero-key contract. Canonical delivery and installed consumer sources match. Shader/material asset jobs completed before captures. This is not a full current regression-suite pass. The pre-existing nonfatal baseline image-index assertion remains outside this change.

Camera, material bindings, visibility, precision, pacing and atmosphere/cloud settings were restored; no level was saved. The saved level SHA-256 remained `faec5fd3f9b69643b918af3a917623776d31bf7cc962485cfba712313a36a922`. Temporary benchmark material sources were archived and removed. Probe summaries and reproduction scripts are retained in `tools/LookupResourcePrototype/default-rollout/`; compiler executables and large transient shader dumps are not bundled.

Earlier evidence: [lookup resource investigation](TerrainLookupResourceInvestigation20261024.md). Raw final results: [JSON](TerrainShaderDefault20261024.json).
