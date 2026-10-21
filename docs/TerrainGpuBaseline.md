# Terrain GPU baseline — RX 7900 XTX

Investigation for [terrain-compositor #17](https://github.com/brooteskan/terrain-compositor/issues/17), measured September 24, 2026. This records the existing local build; it does not implement an optimization or establish a regression against an earlier release.

## Findings

The default material does not explain the whole GPU workload. In the final uncapped Editor run, the near/far/grazing views measured **4.09 / 4.69 / 4.82 ms** median GPU frame time. The near default forward pass measured **0.422 ms**. Windows process 3D-engine utilization was approximately **50%** near, falling to **35%** with a 60 FPS console limit and **18%** with a 30 FPS limit. GPU saturation was not reproduced under these settings.

Terrain contributes substantial geometry and shadow work: the near view submitted 2.62 million triangles to each of depth and forward, plus 5.35 million across four directional shadow cascades. **The largest individual far-view draws are non-terrain mesh pixel shaders**: two two-triangle draws cost approximately 0.84 and 0.82 ms in AMD counter replay and persist with terrain off. Removing terrain changes occlusion and GPU operating conditions, so the whole-frame on/off difference is a **net scene effect**, not an isolated terrain duration. The far-view median actually fell slightly with terrain enabled, within broad overlapping distributions; that is not evidence of negative terrain cost.

Filtered Noise 2D adds measurable forward shading work, especially near and far, but is not a prerequisite for the reported load. Geometry/shadow investigation and frame pacing should precede an assumption that procedural noise is the primary bottleneck.

## Reproduction and provenance

| Setting | Captured value |
|---|---|
| GPU / driver | AMD Radeon RX 7900 XTX / 32.0.31041.1004 |
| CPU | AMD Ryzen 7 2700 |
| Application | Existing Windows `profile` Editor, DX12 |
| Viewport / render scale | 1920 × 1080, matching render resolution, 1× |
| MSAA | 2×, confirmed by captured renderer overlay and GPU resources |
| VSync | `vsync_interval=0` during measurement |
| Uncapped | `sys_MaxFPS=-1`; **0 is not uncapped in this Editor** |
| Camera FOV | Editor preference 75 degrees |
| Level | Saved local `DefaultLevel`, no save during tests |
| Terrain region | Authored 64,000 × 64,000 × 2,048 m |
| Terrain World readback | Height grid 0.5 m, surface grid 1 m; min/max height −1024/+1024 m |
| Renderer component readback | Render distance 4096 m; first LOD 256 m; CLOD enabled, distance 16 m |
| Lighting | Existing HDRI sky/lighting and directional Sun; intensity 0.2 |
| Directional shadows | Enabled; four 2048² cascades, 500 m distance, filter method 1, 32 filtering samples, fullscreen blur enabled |
| Cutouts / gaps | One cutout, 8 vertices / 12 triangles; zero active gap instances and mask bytes |
| Ray tracing | Terrain ray tracing enabled; acceleration-structure pass included |

The source prefab also contains a compositor quality override of 9406 m render distance, 277.3 m first LOD and 28 m CLOD distance. That does **not** match the renderer component readback, and the editor-adapter composition-owner lookup returned no owners. Do not describe 9406 m as a verified effective renderer setting. The LOD experiment below changed the reflected renderer first-LOD value explicitly.

Camera positions and Euler rotations, in meters and degrees:

| View | Position | Rotation |
|---|---|---|
| Near | (16, 16, 32) | (−35, 0, 0) |
| Far | (16, −180, 180) | (−35, 0, 0) |
| Grazing | (16, −100, 32) | (−5, 0, 0) |

Moving cases translate along +X at 16 m/s. The six-second uninstrumented wall-time phase and the 240-frame GPU phase are separate routes from the same origin. Diagnostic pass samples restart from that origin; the pipeline-statistics frame is stationary. These are short GPU workload controls, not a long-flight hitch certification.

The engine commit is `061180bf24f1666eb30315b35da292eb14f4659c`; compositor HEAD is `f7a569f3c1c88f964d017077af974d60345094a7`; TG HEAD is `0dc48b33a897e8651041832847628178354ad6c9`. Both local source checkouts contain pre-existing changes. TG includes additional height-precision changes and a Filtered Noise version of the procedural example. The installed binaries were not rebuilt for this investigation. This is a baseline of those recorded binaries/assets, **not a clean f7a569f benchmark or proof that every dirty source was compiled**.

Level SHA-256 before and after the complete final run:
`83d74525d32b75bdfbcab08cc7df7379ca06dc82d2a2185b755440bcd32a8728`.

## Final measurements

GPU frame times are Atom's `Frame Gpu Time` timestamp envelope, in milliseconds. Each case requests 240 frames; initial unavailable GPU samples are excluded. Pass timings are medians of nine separate delayed captures. Parent/child timings are never summed to obtain frame time. These are whole scene passes, including non-terrain draws.

| Stationary view | Default GPU median / p95 | Terrain off median / p95 | Default DepthPass | Default Forward |
|---|---:|---:|---:|---:|
| Near | 4.090 / 4.851 | 3.433 / 3.731 | 0.292 | 0.422 |
| Far | 4.687 / 6.359 | 5.061 / 6.436 | 1.235 | 1.345 |
| Grazing | 4.824 / 6.023 | 3.984 / 5.057 | 0.896 | 0.906 |

With terrain off, far-view depth/forward remain **1.602 / 1.297 ms**; grazing remains **0.889 / 0.763 ms**. The near off-view has no nonzero depth/forward sample, but other passes still cost 3.43 ms overall. This prevents attributing an entire named raster pass to terrain.

| Moving view | Default GPU median / p95 | Terrain off median / p95 |
|---|---:|---:|
| Near | 4.538 / 5.633 | 3.169 / 3.614 |
| Far | 4.478 / 6.018 | 4.316 / 5.070 |
| Grazing | 4.815 / 6.179 | 4.074 / 4.737 |

| Material | Near Forward | Far Forward | Grazing Forward |
|---|---:|---:|---:|
| Default | 0.422 | 1.345 | 0.906 |
| Original lattice procedural example | 0.495 | 1.405 | 0.921 |
| Local Filtered Noise 2D example | 0.768 | 1.745 | 0.993 |

These are complete material comparisons, not identical noise kernels. The lattice reference is the canonical generated example, copied to a temporary separate asset path. The TG procedural example uses the in-progress 64-component filtered spectral function for base color, with zero height input to Normal From Height. Its source hashes and generated files were preserved with the evidence. Source/material differences and uncontrolled clocks limit precision of cross-case subtraction.

| Near default pacing | Observed Editor wall interval / approximate rate | Process 3D utilization | GPU median |
|---|---:|---:|---:|
| `sys_MaxFPS=-1` | 8.00 ms / 125 Hz | 50.0% | 4.090 ms |
| `sys_MaxFPS=60` | 16.00 ms / 62.5 Hz | 35.1% | 5.677 ms |
| `sys_MaxFPS=30` | 32.01 ms / 31.2 Hz | 18.3% | 5.574 ms |

The console limits are quantized by the existing engine limiter, so their measured rates are not exactly their labels. These wall intervals include Editor/Python scheduling and are not GPU timings or PresentMon measurements. Utilization is the median of 3–4 Windows GPU Engine samples during the uninstrumented phase, not a device-wide peak. The higher per-frame GPU duration when capped is consistent with changed GPU operating clocks; clocks were neither locked nor sampled. Lower utilization does not imply cheaper shaders.

The pinned `Code/Legacy/CrySystem/System.cpp` substitutes 60 FPS when `sys_MaxFPS=0`, VSync is off, and the legacy level state is unloaded or paused. Earlier exploratory runs using 0 are retained as diagnostics, not mislabeled uncapped results.

## Geometry, shadows, and masking controls

The near baseline's depth and forward each submit **2,621,440 triangles** and invoke the vertex shader **1,948,508 times**. The depth pass invokes the pixel shader **4,172,124 times**, about twice the viewport pixel count with sample-frequency 2× MSAA depth. This is not an overdraw ratio: MSAA, helper work, visibility and other draws affect invocation counts. Forward is approximately 2.7 million pixel invocations. All raw per-pass counters are retained.

Four shadow cascades submit **5,349,460 triangles** in total. Their individual median times are 0.028, 0.411, 0.558 and 0.393 ms. Significant shadow cost remains with terrain off (including 0.487 and 0.590 ms in two cascades), so these whole-pass numbers must not be called isolated terrain shadow cost.

| Controlled change, default material | GPU median | Interpretation |
|---|---:|---|
| Near baseline | 4.090 ms | Reference |
| Near directional shadows disabled | 3.665 ms | Scene-level shadow control; changes lighting |
| Far directional shadows disabled | 4.079 ms, versus 4.687 | Same qualification |
| Near first LOD distance 256 → 128 m | 3.893 ms | Geometry falls substantially; small/noisy net frame improvement |
| Near single cutout disabled | 4.166 ms | No measurable improvement established in this scene |

The LOD control reduces main-pass triangles to **765,952** and vertex invocations to **568,976**, about **71% fewer**. Depth/forward medians become **0.158 / 0.301 ms**. It reduces geometric detail and is only a diagnostic quality tradeoff; it was restored, not adopted as an optimization.

Removing the single cutout does not remove the shared depth/shadow pixel-shader contract. This scene cannot characterize many overlapping cutouts, heavy triangle containment, or nonzero gap masks. No gap-mask bandwidth conclusion is claimed.

Separate RenderDoc captures contain **428 near**, **377 far**, and **74 far-without-terrain** draw actions. These are submitted API draws, which differ from the Editor's draw-item overlay. They confirm substantial repeated geometry submission across passes; they are not CPU draw-call overhead measurements.

## Draw attribution and memory traffic

AMD vendor counters succeeded for all three RenderDoc captures. The following are **serialized replay diagnostics**, not native frame timings or values to add to the preceding pass table. AMD's `GPUTime` excludes parallel processing between draws; `VsGsTime` and `PSTime` can overlap. Results are converted from the API's seconds unit to milliseconds. Counters are from a single captured frame per view.

| Far-view draw | Indices | Replay GPU time | Pixel-shader busy time | Video-memory fetch |
|---|---:|---:|---:|---:|
| Terrain enabled, event 969 | 6 | 0.843 ms | 0.777 ms | 230,912 bytes |
| Terrain enabled, event 2539 | 6 | 0.817 ms | 0.752 ms | 232,800 bytes |
| Terrain disabled, event 552 | 6 | 0.845 ms | 0.778 ms | 231,040 bytes |
| Terrain disabled, event 1038 | 6 | 0.821 ms | 0.756 ms | 236,096 bytes |

These draws use mesh object/instance bindings, not terrain bindings. Their pixel shaders are `Shader {f3392722}` and `Shader {0a876938}`; the corresponding vertex shaders are `{d3733b27}` and `{4bd5d990}`. Each pair appears in two far-view draws, totaling **0.916 / 0.892 ms** of replay time. The expensive pair of individual draws spends about 92% of its measured duration with pixel shaders busy, versus about 2 microseconds of vertex/geometry activity each. Their memory-unit active cycles are only about 28–29% of command-processor busy cycles, including stalls. This favors investigating pixel shader execution rather than bulk video-memory bandwidth for these draws; it does not distinguish ALU work, divergence and texture latency. Shader source filenames and meaningful mesh buffer names were stripped, so a specific source material/entity is not yet proven.

All 377 far-view draws were inspected. Shader reflection identifies **303 terrain draws** through `TerrainMaterialSrg` cutout/gap resources, exactly matching the draw-count difference from terrain-off. Terrain depth/shadow share one shader pair; forward uses another:

| Far-view terrain shader group | Draws | Sum of replay draw time | Sum of VS/GS busy time | Sum of PS busy time | Video-memory fetch / write |
|---|---:|---:|---:|---:|---:|
| Depth + shadows | 217 | 0.617 ms | 0.312 ms | 0.287 ms | 57.05 / 21.82 MB |
| Forward | 86 | 0.598 ms | 0.283 ms | 0.414 ms | 61.44 / 28.91 MB |

All 428 near-view draws were also inspected: **283 terrain depth/shadow draws** sum to **0.673 ms**, and **92 terrain forward draws** sum to **0.348 ms** in replay. Their VS/GS busy sums are 0.297 / 0.151 ms and pixel busy sums are 0.341 / 0.248 ms; fetch/write totals are 78.32 / 57.53 MB and 64.21 / 32.95 MB respectively. This supports prioritizing repeated terrain depth/shadow work in the near view. Only the 40 most expensive terrain-off far draws were inspected for shader identity; counters cover all 74 draws.

MB here means 1,000,000 bytes. These are disjoint draw groups, not summed parent/child passes. Stage busy times overlap, and these sums do not reconstruct the native frame. They establish mixed terrain vertex/pixel work and appreciable traffic; they do not establish bandwidth saturation. The default forward shader still includes cutout/gap evaluation, lighting and normal reconstruction, so “default” does not imply a constant-color pixel program. The one-cutout and zero-gap controls remain necessary qualifications.

The near capture's largest individual draws also use a non-terrain mesh shader pair (`{3865226a}` / `{a35ded30}`): two 84-index draws take **0.293 / 0.247 ms**, with **0.268 / 0.225 ms** of pixel-shader busy time. This agrees with the native observation that notable shadow work survives terrain removal.

## Prioritized follow-ups

1. **Address the expensive non-terrain mesh pixel shaders separately.** The far-view shader pairs identified below account for 1.81 ms of serialized draw replay and persist with terrain off. Resolve their generic capture hashes to source materials/entities and evaluate shader loop/branch behavior; the captures lack source filenames. Target a 30% reduction in their matched replay time and a repeatable native far-view GPU improvement of at least 0.3 ms, at fixed clocks and preserved appearance. Verify with A/B/A captures before treating the target as achievable.
2. **Evaluate terrain shadow culling/LOD and repeated vertex work at preserved visual quality.** The counters establish large repeated geometry; the coarse-LOD control establishes available headroom but not a production-quality solution. Target at least 25% fewer terrain shadow vertex invocations and at least 0.2 ms reduction in the matched raster/shadow passes, with no regression in the 1080p frame p95, holes, cutouts or CLOD transitions. Validate against near/far/grazing captures and separately assess CPU submission cost.
3. **Optimize Filtered Noise only against its added forward cost.** Target a 30% reduction of its near/far forward-pass increment at fixed clocks, while retaining the approved filtering and appearance. The measured increment is approximately 0.35–0.40 ms in those views. Do not change its spectrum or sample count without an image-quality comparison.
4. **Defer masking and bandwidth-specific changes until corresponding counters justify them.** The one-cutout control did not establish a win, and there are no active gaps. Add representative dense cutout/gap scenes and measure cache traffic, memory stalls and occupancy before proposing packing or sampling changes.

These are proposed acceptance targets, not achieved optimizations. Implementation should be tracked separately from #17. Frame caps can reduce load, but choosing a production cap is a product/quality decision, not a renderer speedup.

Follow-up 1 was subsequently implemented in SilPOM: the expensive pair belongs to `silpom test` / `Assets/SilPOM/stonewall.material`. Exact power-of-two texel addressing reduced the matched draws by about 42%, with identical same-frame color/depth, and reduced stationary far-view native GPU time by about 0.55–0.56 ms. The separate report and full limitations are in `TGProject/Gems/silpom/Docs/MeshPixelPerformance.md`; other-view whole-frame results varied with GPU clocks. The tables above remain the original pre-optimization baseline.

## Evidence, limitations, and rerunning

[Compact measurements and counters](TerrainGpuBaselineData.json) accompany this report. Raw local evidence is under `D:/TG/TGProject/user/TerrainGpuProfiling/`: `final-01/`, `final-config.json`, `final-01-utilization.csv`, `final-01-editor.log`, `input-hashes.json`, source-diff evidence, script snapshots and `renderdoc-01/` captures. Exploratory runs are kept separately and are superseded by the final run. The final script waits through native collection without parsing a growing JSON file every frame; the earlier polling loop added avoidable CPU overhead.

The 24-case final run completed and preserved the authored prefab hash. This is one final session, with hundreds of GPU-frame samples per case and nine pass samples; it is not a confidence interval across independent runs. GPU clocks, power, temperature and competing desktop GPU activity were not controlled. Case order was fixed. Terrain-off unregisters the region's layer spawner while retaining the renderer; it removes terrain from rendering/RT and reveals previously occluded scene content. Unregistering the renderer itself caused frame capture to time out and was excluded.

The installed RenderDoc 1.25 successfully records and opens captures. Its built-in D3D12 counter replay fails with a device-lost diagnostic requiring Windows Developer Mode for stable power state; no OS setting was changed. **AMD vendor counters succeeded through a separate replay path** in all three captures. Those results support draw-level timing, vertex/pixel activity and video-memory traffic attribution. They do not measure occupancy or establish ALU utilization, and replay timing is not interchangeable with the native frame timestamp envelope. Native Atom timestamps and pipeline statistics succeeded independently.

Run [ProfileTerrainGpu.py](../tools/ProfileTerrainGpu.py) in a dedicated Editor with a fresh output directory, adapting [the example configuration](../tools/ProfileTerrainGpu.example.json):

```powershell
$env:TC_GPU_CONFIG = 'D:/wzmono/terrain-compositor/tools/ProfileTerrainGpu.example.json'
& D:/TG/TGProject/build/windows/bin/profile/Editor.exe `
  --project-path D:/TG/TGProject --autotest_mode `
  --runpython D:/wzmono/terrain-compositor/tools/ProfileTerrainGpu.py
python D:/wzmono/terrain-compositor/tools/AnalyzeTerrainGpu.py `
  D:/TG/TGProject/user/TerrainGpuProfiling/my-baseline
```

For utilization, sample `\GPU Engine(*)\Utilization Percentage` for the Editor PID to CSV columns `utc,engine,utilization_percent`, then pass `--utilization <csv>` to the analyzer. Keep other builds, shader processing and GPU captures out of timing runs. Inspect the PNGs and asset status; successful collection is not visual verification. A material path selects a catalog `.azmaterial`; an empty path clears selection to the built-in default. The recorded filtered example is local work, so do not label a stock `procedural_terrain` asset as Filtered Noise without inspecting its generated source.

For an existing AMD capture, set `TC_RDC` to its absolute `.rdc` path and run `qrenderdoc.exe --python tools/AnalyzeTerrainRenderDoc.py` using an absolute script path. [The counter analyzer](../tools/AnalyzeTerrainRenderDoc.py) writes `<capture>.amd.json` and closes its dedicated qrenderdoc process. Raw evidence also retains `IdentifyDraws.py`, `SummarizeDraws.py`, shader reflection and grouped draw data. Launch-time RenderDoc injection plus the profiling configuration's `renderdoc: true` records separate capture frames; never use injected runs as the native performance baseline.

The temporary lattice source assets were archived under `material-sources/lattice/` in the local evidence directory and removed from the project. Recreate their original `Assets/TerrainGpuProfiling/Lattice/` path, or adjust the material catalog path, before repeating the full material comparison. The filtered source snapshot is alongside them. The example configuration requires no temporary materials.

The analyzer handles the pinned engine event logger's exact leading-comma defect on later JSON batches while retaining the raw files. It preserves raw pipeline entries and selects the largest occurrence of a repeated pass name to distinguish the main scene from idle preview pipelines; it does not sum a pass hierarchy into GPU frame time. Engine debug names omit hierarchy, so inspect the underlying captures before extending that rule to another pipeline layout.
