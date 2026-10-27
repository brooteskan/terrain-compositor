> Superseded for the requested full-cloud comparison by TerrainAtmosphereVolumesEnabled20261025.md. The first run enabled the cloud components but renderer counters showed zero active volumes; its timings measure atmosphere and sky background only.

# Terrain shader comparison with atmosphere and sky background only — 2026-10-25

Same Terrain6/7 camera, 1203×656 viewport, MSAA 2×, FP16, pacing and scene settings as the prior disabled-effects benchmark. Native Sky Atmosphere, HDRI Sky, two Finite Cloud Volume components and Vue aerial atmosphere were enabled. Each shader received two 240-frame captures after settling. No ground material changes.

| Shader | Mean of run median GPU times |
|---|---:|
| Original fallback | 48.283 ms |
| Previous faster shader, detail normals enabled | 37.085 ms |
| New default, detail normals disabled | 34.026 ms |

New default saves 3.059 ms (8.2%) against previous fast, and 14.257 ms (29.5%) against original. GPU frame time implies about 29.4 frames/s at this camera.

DXGI process local resident usage began at 1730.3 MiB (new default loaded at startup), then rose to 1737.3 MiB after selecting detail normals: +7.0 MiB retained by the allocator throughout subsequent cases. These are process-wide shared-memory figures on the 5700G, not independent per-shader allocation measurements. The switching run cannot establish a clean standalone original-versus-new memory delta. No new cache or texture allocation was introduced by this benchmark.

All original/previous-fast captures matched within the existing terrain comparison mask. New-default image differences are recorded in the JSON; first capture changed 37,722 of 701,014 pixels by at most 1/255 per RGB channel. Captures include the enabled effects. This framing does not represent a close-up cloud-filled stress test.

Restoration checks passed for camera, visibility, material bindings, height precision, saved level hash, atmosphere/cloud component states, aerial setting and profiling toggle. Shader hash stayed unchanged. The optimized shader with detail normals disabled remains the inherited default; original fallback remains available. No implementation changes or new regression tests were needed.

Raw captures: `D:/diversion/TG/TGProject/user/TerrainSceneProfile/20261025-atmosphere-enabled`. Timing and memory details: companion JSON.
