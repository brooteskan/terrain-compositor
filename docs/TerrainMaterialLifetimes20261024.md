# Material-sampling intermediate lifetimes — 24 October 2026

Three focused lifetime variants were tested. None demonstrated a repeatable GPU-time improvement over the faster default, and none reduced measured resident memory. No experimental shader change was adopted. The established faster default, its zero publication key and the original fallback were restored byte-for-byte after the test.

## What changed in the experiment

1. **Color before the normal frame:** sample and accumulate base color before loading the geometric normal and building the tangent/bitangent frame. This avoids carrying that frame through color sampling.
2. **Normal frame last:** also consume roughness, metallic, specular and occlusion before building the frame and sampling the normal map, so non-normal source fields can die earlier.
3. **Late base surface:** combine the second variant with evaluating `VueOverlapSurfaceBase` after contributor accumulation. This avoids retaining the base surface through contributor samples, at the possible cost of retaining its geometric inputs longer.

The selection, contributor count, texture reads, explicit derivatives, precision and per-channel accumulation order were preserved. Only the FP16 material path was varied; the FP32 function was unchanged. A temporary material shader option selected variants. Mode zero retained the established fast path; disabling Optimized Shader retained the original fallback. The faster path stayed the material default throughout. The frozen tile-rejection publication key stayed zero.

## Matched GPU comparison

| Variant | First GPU frame median, ms | Range of repeated case medians, ms | Offline vector registers | Offline scratch bytes/thread |
|---|---:|---:|---:|---:|
| Original fallback | 39.40 | 39.40–41.92 | 256 | 560 |
| Faster default | 26.19 | 26.19–28.85 | 195 | 1584 |
| Color before normal frame | 26.21 | 26.21–28.67 | 188 | 1632 |
| Normal frame last | 26.20 | 26.20–27.79 | 195 | 1584 |
| Normal frame last and late surface | 26.53 | 26.24–26.53 | 177 | 1632 |

The default's three case medians were 26.19, 26.24 and 28.85 ms. The original fallback also slowed from 39.40 to 41.92 ms over the run. That drift is larger than the initial differences between lifetime variants. In the initial matched sequence, color-first and normal-last were effectively tied with the faster default; late-base did not improve it. Repeated captures did not establish a consistent advantage. We do not interpret the aggregate percentages as precise regressions attributable to a variant, or claim that all possible lifetime optimizations are exhausted.

The temporary option and shader clones alter the compiled shader layout. This experiment's baseline must be used for its internal comparisons; the lower initial time relative to the prior clean default's 27.64 ms is not a newly established optimization. The instrumented shader set was removed afterward.

The compiler figures are controlled DXC/Radeon GPU Analyzer probes for gfx90c, **not live driver pipeline ISA**. Other material/renderer options remain volatile in the probe. Delaying the base surface reduced vector-register use from 195 to 177, while adding 48 bytes of scratch. This did not translate into an observed renderer gain. Every probe retained the same 289 DXIL texture-sample calls. Lower register counts alone were insufficient evidence to adopt these changes.

## Memory and appearance

Every case reported exactly **1,817,538,560 bytes (1,733.34 MiB)** of per-process DXGI local CurrentUsage. There was no observed resident-memory saving or growth between the faster default and variants. On this integrated GPU this is shared-memory accounting, not dedicated VRAM. The editor opened the already-enabled faster default before the original-fallback capture, so the original case retains that allocation; this run cannot measure original-versus-fast memory overhead. Allocator pooling/residency also means these snapshots do not establish identical standalone memory requirements for each variant. Previous clean-default memory measurements remain the deployment reference.

All eleven screenshots matched the original control exactly across **701,014 RGB pixels**, excluding the fixed HUD/gizmo mask. The experiment's default image also matched the previous clean-default capture. This verifies this camera and scene, rather than all authored views and materials.

## Controls, restoration and validation

Hardware: user-reported Ryzen 7 5700G integrated graphics and 32 GiB system RAM. Terrain6/7, camera position (1025.073242, 488.140839, 43.456932), rotation (-13.292666, 4.552838, 161.428360), viewport 1203×656, 2× MSAA and FP16 height arithmetic were unchanged. Sky atmosphere, HDRI sky, cloud volumes and aerial atmosphere were disabled for measurement; lighting remained. Each case used 240 frame samples, 6 seconds settling after material changes, pass captures and 60 enabled-pipeline timestamp queries, after a 12-second initial warmup. Cases were original/default/three variants/default/reversed variants/default/original. No power or clock settings were changed.

Camera, bindings, visibility, precision, pacing and atmosphere/cloud controls were restored. No level was saved; its SHA-256 remains `faec5fd3f9b69643b918af3a917623776d31bf7cc962485cfba712313a36a922`. The task-owned test editor was closed. The canonical and installed shader, material type, generator and manifest were restored to their pre-experiment faster-default snapshots. All **14 focused overlap tests passed** after restoration, delivery synchronization reported no pending files, and diff formatting passed. This is not a full current regression-suite validation. The pre-existing nonfatal baseline image-index assertion remains outside this experiment.

Raw case times, CPU times, native probe summaries, image comparisons and restoration checks are in [the JSON results](TerrainMaterialLifetimes20261024.json). The experimental source, variant functions, benchmark materials and scripts are retained under `tools/MaterialLifetimePrototype/`. They are historical, machine-specific reproduction artifacts: running the preparation script modifies installed sources and requires a fresh output directory and an isolated editor session. Compiler executables and large transient ISA/DXIL files are not bundled.

The current adopted default and its original fallback are documented in [the default rollout report](TerrainShaderDefault20261024.md).
