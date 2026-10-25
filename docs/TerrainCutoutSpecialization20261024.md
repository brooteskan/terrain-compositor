# Empty cutout specialization, 2026-10-24

The initial controlled editor comparison improved frame time from approximately
53.2 ms to 51.2 ms (about 3.7%). This is a modest scene-specific improvement,
not a solution to the remaining GPU bottleneck. Final restarted-editor timings
are recorded below; timing varies across sessions.

## Change and correctness

The native Vue ground material now specializes mesh cutouts and mesh-height gaps
from their existing published counts. Zero counts compile away the corresponding
loops, buffer reads, and discard path. Nonzero counts select the complete existing
predicates. The feature processor already updates these material properties when
publications change; the functor declares both dependencies. Materials without
this functor retain true defaults and the complete path. No properties or SRG
layout were added. The retained terrain overlap shader and weights are unchanged.

The production cooked-HLSL check compiles all four combinations with DXC and
native half support: neither feature has zero resource-load calls; cutouts alone
has 15, gaps alone 13, and both 28. These offline fixed-constant probes validate
dead-code removal and compilation, not captured driver machine code. Shader
specialization is applied by the engine at pipeline creation.

FP16/FP32 arithmetic and texture storage policies, blending, geometry LOD,
render distances, geometry, and cache scheduling are untouched. Both retained
terrains remain visible with the procedural ground. No saved level was written.

## Editor measurements

AMD Radeon(TM) Graphics, DX12, 1203x656, MSAA 2x, height precision 16, stationary
reference camera. Each case requests 240 native frame intervals. Engine Cpu Time
includes render/GPU waits; it is not isolated CPU work. GPU frame envelopes are
available, but individual Forward timestamps are not reliable in this SDK.

| Capture / case | Median ms | P95 ms | Approx. FPS |
|---|---:|---:|---:|
| before / baseline | 53.206 | 56.560 | 18.79 |
| before / overlap-off | 47.567 | 49.948 | 21.02 |
| before / specular-off | 52.974 | 56.365 | 18.88 |
| before / baseline-repeat | 53.108 | 56.266 | 18.83 |
| after / specialized | 51.143 | 53.742 | 19.55 |
| after / specialized-repeat | 51.228 | 53.916 | 19.52 |
| final / final | 51.585 | 54.479 | 19.39 |
| final / final-repeat | 51.508 | 54.142 | 19.41 |

The initial before/after Forward pixel-shader invocations fell from 1,311,993 to
994,105, while Forward and Depth input primitives remained 2,426,734. The initial
images are pixel-identical across 701,588 compared pixels after excluding the
statistics HUD and a small editor marker. Final images and exact error metrics,
native counts, timings, camera, and restoration checks are in the companion JSON.

Removing overlap evaluation entirely still saved roughly 5.6 ms in the initial
ablation, identifying remaining overlap work as a useful future target. There
were no active native height/surface stamps in this capture, and stationary
clipmaps did not update. These numbers do not profile moving-camera strip spikes.

## Reverted experiments and editor recovery

A conservative overlap skip-map prototype changed material layouts and produced
incorrect gray materials during hot reload. Its apparent faster captures are
invalid and excluded. The prototype and generated publications were removed from
active source; backups are in the local workspace. Attempts to recover that
reload left the original editor unresponsive. After a full memory dump and the
user's explicit approval, it was restarted from the unchanged saved level, with
the profiling camera, 16-bit precision, and both terrains restored in memory.
The final capture uses that restarted editor. Unsaved edits from the old process
are not claimed to have been recovered.

The post-restart Lua-only attempt to force a new baseline did not restore the
original invocation count, so it is excluded as a before/after measurement.
The original controlled comparison supplies the improvement estimate; final
captures verify the delivered state. All experiment source changes are reverted.

## Validation

- Production shader four-combination DXC probe passes.
- Existing overlap behavior tests: 13 passed.
- Consumer synchronization tests: 4 passed; current consumer check has no pending files.
- Height storage/arithmetic regressions: 6 passed.
- Final editor capture restores camera, visibility, bindings, precision, and saved level.

Saved level SHA256: `70f2db2744cd2d2fa2c5808c157ca463913756744d1bae0025108b859cfc7a5d`.
Raw images and captures remain under `TGProject/user/TerrainSceneProfile`.
