# Terrain shader comparison at cloud-and-terrain camera — 2026-10-25

Benchmarked in the user's existing editor, preserving the new camera and 1267×761 viewport, MSAA 2×, FP16 and existing pacing. This viewport differs from the previous 1203×656 run; compare shaders within this run. Atmosphere, aerial atmosphere and both volumetric clouds were enabled; renderer counters verified two active volumes in all cases. Two 240-frame captures per shader, same six-case ordering and settling protocol. No cloud quality or voxel data changes.

| Shader | Mean of two median GPU frame times |
|---|---:|
| Original fallback | 301.281 ms |
| Previous faster shader | 285.669 ms |
| New default | 282.294 ms |

New default saves 3.375 ms versus previous fast and 18.986 ms versus original. Cloud pass medians by case: {"complete": 245.333, "previous-fast": 246.2855, "default": 245.54649999999998, "previous-fast-repeat": 245.772, "default-repeat": 246.16750000000002, "complete-final": 245.922}.

The user reported roughly 210 ms before profiling. After full restoration, the editor HUD was observed at 211.9 ms. These instrumented captures were higher; no isolated test was performed to attribute that discrepancy. Compare the three shaders under this run's matched instrumentation rather than treating the table as an uninstrumented live-frame promise.

DXGI local process resident usage ranged from 1761.71 to 1768.71 MiB. This is the existing editor with persistent allocator state, not independent fresh-process memory totals for each shader. Full per-case memory and appearance differences are in the JSON. No extra lookup cache was introduced. The comparison mask excludes the established HUD/gizmo regions; it was designed for the earlier viewport, so some editor overlays may remain in this new framing.

Effective cloud quality: {"direction": [-0.260139, -0.371031, 0.891439], "illumination": [4.82078, 2.08879, 0.621313], "skyAmbient": [0.8, 1, 1.3], "groundAmbient": [0.12, 0.1, 0.08], "viewSteps": 192, "lightSteps": 16, "cloudAttenuation": true}. The explicit scene controls override per-volume sample budgets. At occupied view samples the shader can run sun, sky and ground optical-depth marches across the volumes. It already clips cloud intervals against opaque depth, allocates samples over the occupied union, rejects outside bounds and ends low-transmission view rays. Optical-depth loops currently take their full sample count and density lookup explicitly uses mip 0. Voxel size does not determine the view sample budget.

The active density grids are 161×39×178 at 15.839 m and 53×21×54 at 40.002 m, totaling 4,711,056 bytes (4.493 MiB) of R32 density samples. Reducing voxel resolution may help memory/cache behavior, but does not by itself remove the nested march work. A useful first controlled experiment is to vary effective light steps and view steps separately, with image captures; a subsequent architectural candidate is cached/precomputed light transmittance to replace repeated secondary marches. No such optimization is claimed as implemented or measured here.

All restoration checks passed; original material bindings, camera, visibility, height precision, atmosphere/cloud controls, profiling toggle and saved level were restored. Shader source hash stayed unchanged. The new optimized shader remains the default with detail normals disabled; original fallback remains available. The user's editor stays open. No implementation changes or new regression tests were needed.

Raw captures: `D:/diversion/TG/TGProject/user/TerrainSceneProfile/20261025-cloud-terrain-camera`.
