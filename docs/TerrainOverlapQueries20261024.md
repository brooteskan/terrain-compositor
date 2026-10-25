# Retained overlap height-query optimization, 2026-10-24

The alternating editor comparison improved from **50.32 ms to 49.98 ms**, a
**0.34 ms (0.7%)** reduction, approximately **19.87 to 20.01 FPS**. This is a
modest gain. It does not remove the remaining roughly 4 ms of overlap cost.

## Changes

Owned overlap query textures now store only their sampled red channel:
RGBA16F becomes R16F, and RGBA32F becomes R32F. Each 256x256 field is reduced
from 512 KiB to 128 KiB in 16-bit mode, or 1 MiB to 256 KiB in 32-bit mode.
For the two retained terrains this saves 768 KiB or 1.5 MiB respectively.
All 65,536 height samples in each field are bit-identical to the previous red
channel. Normal fields retain their original format. The custom linear R16F
image preset is delivered through the canonical TG synchronization manifest;
the built-in LUT_R32F preset handles full precision.

The owned query path specializes the height field extent emitted by the same
publisher. It removes two texture dimension queries for two contributors,
plus their dynamic address calculations. The publication fingerprint and texture
filename include the field size, and tests verify that generated shader constants
and actual texture dimensions follow that size. Legacy/manual materials default
to the dynamic query path. The option adds no MaterialParameters/SRG fields.

For two contributors, both source footprints are now checked before either
height fetch. Outside their intersection a blend is impossible, so the shader
can skip height lookups and coverage calculations. The original height queries,
weight resolver, FP16/FP32 rounding, blending, endpoint tests, material evaluation,
and derivatives remain unchanged for pixels that can blend. Three- and
four-contributor selection retains its previous logic.

The fixed-height-extent option selects both owned query optimizations; disabling
it preserves the original query path for compatibility and controlled comparison.
The current precision selector still controls R16/native-half versus
R32/native-float height and weight arithmetic. Coordinate/gradient precision
remains float. Geometry, geometry LOD, clipping, normal reconstruction, ground
contact, render distances, and clipmap scheduling are unchanged.

## Editor measurements

AMD Radeon(TM) Graphics, DX12, 1203x656, MSAA 2x. Terrain6 and Terrain7 visible,
procedural ground active, identical camera and pacing. Each batch requests 240
frames. Reported comparison is the mean of each path's two median frame times.
Engine Cpu Time includes GPU/render waits, rather than isolated CPU work.
The full GPU frame envelope supports the small reduction; individual Forward
timestamps are unreliable in this SDK.

| Alternating case | Median frame ms | Median GPU frame ms |
|---|---:|---:|
| Original queries | 50.345 | 49.683 |
| Optimized queries | 49.904 | 49.364 |
| Original queries repeat | 50.303 | 49.791 |
| Optimized queries repeat | 50.059 | 49.392 |

The texture-only comparison was effectively unchanged: 50.169/50.200 ms before,
50.082/50.167 ms afterward. Dimension specialization alone also had no measurable
gain: 51.190/50.889 ms dynamic versus 51.055 ms fixed. The footprint rejection
accounts for the useful portion of this shader change. Ordinary clock/timing drift
explains why independent batches cannot be treated as a larger improvement.

An overlap-disabled diagnostic measured 45.841 ms against 49.889-50.076 ms
with the complete blend path, leaving about 4.0-4.2 ms of overlap overhead in this
view. This is a material ablation, not an isolated GPU pass measurement. It was
restored before subsequent cases. It changes visible pixels, so removing overlap
is not an equivalent optimization.

## Validation

All optimized 16-bit images match the initial baseline exactly over 701,014
pixels, excluding the performance HUD and editor transform gizmo. Forward and
Depth retain 2,426,734 primitives each, and Forward pixel shader invocations
remain 994,108. The final editor run tests 16-bit, 32-bit, then 16-bit again and
checks the actual published formats and restoration. Detailed final timings,
precision publications, image comparisons, GPU/CPU metrics, pipeline counts,
asset product evidence, and local raw capture locations are in the companion JSON.

All six production 2/3/4-contributor x 16/32-bit pixel shader combinations compile,
along with the original two-source query path. The compiled fixed-extent pair
contains four dimension queries versus six in the original path; remaining
queries belong to other material/normal processing. The actual cooked endpoint
helpers still compile to 12 half operations and zero float operations in 16-bit
mode, and 12 float/zero half in 32-bit mode. These are offline DXC audits of the
production HLSL, not a captured driver binary. Existing overlap, precision, and
synchronization regressions pass: **23 tests**.

The first extent-only capture used a shifted camera and was excluded from all
comparisons. During the initial material-type hot reload the SDK emitted transient
property-type/material-initialization errors; fresh immutable publications became
ready and all subsequent comparisons rendered correctly. No editor restart or
component reactivation was needed. The profiling harness also logs the SDK's
existing metrics-cvar range warnings when enabling/restoring capture settings.

All captures restore visibility, material bindings, precision, pacing, and saved
level content. Stationary timings do not characterize clipmap boundary spikes or
other GPUs. The editor is left open in 16-bit mode with both retained terrains
visible. Canonical source and the image preset live in terrain-compositor/main;
TG receives them through SyncProject.

Saved level SHA256: `70f2db2744cd2d2fa2c5808c157ca463913756744d1bae0025108b859cfc7a5d`.
