# Terrain CPU kernels (TG #39)

The default procedural source uses an immutable, clamped kernel configuration
and a request-owned cell cache. A batch invokes one retained callback and reuses
deterministic cell centers and bump/depression classification in 2,176 bytes of
data scratch. A 64-entry cache backs a nine-cell neighborhood. Eviction, batch
splitting and scalar calls preserve cell coordinates, hash semantics and the
original row-major hill accumulation order.

The exact path rejects squared distances at or above **1**, safely beyond the
0.85-cell support radius. It deliberately evaluates the original square root,
division and smooth curve throughout the support boundary. Zero profiles skip
power evaluation. This does not substitute a rounded squared-radius comparison.
No compiler-wide reassociation or fast-math setting changes.

The snapshot validates the complete input contract before writing. Built-in
constant existence uses a fill; authored image existence still composes afterward.
External masks retain their existing live fallback. Rendering continues to exclude
shader mesh cutouts and collision-only gaps from geometry existence.

Batch candidate lists use actual XY extrema, including all supplied normal halos
and CLOD positions. Bounds touching a batch remain candidates; invalid bounds stay
conservative. Up to 64 record pointers use fixed storage. Overflow uses the complete
ordered list. Candidate pointers remain inside a query retaining its immutable
publication and source owners.

Sector assessment retains its resolved query and coordinates in shared immutable
ownership. The deferred corner proof retains a run without the temporary corner
span; its worker constructs the full grid once. Regular/CLOD gathers can execute
these plans even when sample reuse is off. Reuse preserves the existing bitwise
row/column coordinate checks, retains scratch capacity, and reuses a certified
pointwise single-owner CLOD run for its missing subset. Split owners retain the
resolver. Plans with more than 65 ownership runs also retain the original resolver
path to bound retained storage. Final dependency/publication/destination validation and raster/RT commit
remain unchanged.

Admission reserves another MiB per request for the retained plans and scratch.
The dispatcher still enforces its existing 256 MiB aggregate reservation cap.
Result accounting includes retained query storage; reservations remain distinct
from process or GPU working set and allocator/driver overhead.

## Comparison controls and experimental policy

The existing retained-kernel switch and new cell-cache switch change only
exact-equivalent work. Workers capture these choices at source acquisition.

| Controls | Retained kernel |
| --- | --- |
| r_terrainRetainedKernel false | Original reference profile with all nine powers |
| r_terrainRetainedKernel true, r_terrainCacheHillCells false | Zero-profile-pruned reference |
| Both true (default) | Cached exact kernel with conservative distant-hill rejection |

Serialized KernelPolicy values are Reference=0, CachedExact=1 (default),
IntegerPowers=2 and PrunedReference=3. An unknown value falls back to Reference.
IntegerPowers selects multiplication for exact frequencies 1, 2, 3 and 4; all
other frequencies use the general power function. This policy is experimental.
It can change packed heights and normals. Changing the serialized policy
invalidates source work and publishes a new immutable kernel; ordinary, retained
and collision consumers share it. Diagnostic switches never silently substitute
a different approximation policy.

The preserved component EvaluateReference entry point supplies the original scalar
oracle. The portable benchmark also contains a reference with the same float
configuration, cell construction and accumulation order.

## Measurement and numerical limits

Build tools/BenchmarkProceduralHill.cpp with the target compiler's optimized
precise FP mode. Its kernels, profiles, sqrt and numerics modes emit separate CSV
tables. Inputs/output arrays are allocated before kernel timing. Each measured
batch creates fresh request scratch; first-call measurements are not a flushed
hardware-cache experiment. Constructor setup and lookup-table setup are separate.

The disabled TerrainSectorLifetimeTests.MeasureProceduralHillKernels benchmark
calls actual sector preparation, packing, CLOD and RT conversion. Its matrix
separates kernel policy, sample reuse and synchronous/deferred assessment.
Uninstrumented timings and a separate diagnostic evaluation avoid counting
counter updates as production kernel cost.

Optional TerrainKernel records expose evaluations, cell creations/hits,
square-root/power counts and source/composition/ownership/coordinate/packing CPU
times. tools/AnalyzeTerrainKernelLog.py summarizes them. Inclusive worker
intervals overlap and must not be added to frame times. Kernel data scratch
excludes call frames and allocator overhead; retained/reuse capacities and the
admission reservation are reported separately.

Provisional incremental IntegerPowers budgets, measured against the existing
float kernel at a 0.5 m normal spacing, are 0.001 m height, 0.1 degree normal
angle, two packed height units (the renderer stores even heights), and one packed
normal component unit. These sampled limits are not a global error proof or
permission to enable approximation by default. The benchmark reports worst
cases, RMS, changed packed values and a double-precision arithmetic reference.
Its double oracle shares captured float coordinates/cell centers, isolating
evaluation error from a redefinition of the procedural field.

Uniform squared-distance lookup tables and reciprocal-square-root estimates are
benchmark-only experiments. They are rejected for normal use: low fractional
frequencies amplify support-edge error, and refinement can cost more than vector
hardware square root. Existing float reference error near those edges is reported
separately and is not attributed to caching.

The consuming TG report records final hardware measurements, builds, tests,
camera captures, numerical results and their provenance:
TGProject/Gem/Docs/TerrainCpuKernelProfiling.md.

## Ryzen 7 2700 results, 2026-09-12

Three repeated MSVC 19.51 optimized precise-FP runs measured the following at
density approximately 0.0099, amplitude 3 m and frequency 3:

| Measurement | Pruned reference | Cached exact | Integer experiment |
| --- | ---: | ---: | ---: |
| Dense kernel, ns/sample | 331.04 | 121.11 | 61.12 |
| Sparse kernel, ns/sample | 334.26 | 320.42 | 278.89 |
| Actual sector, reuse off, synchronous assessment, ms | 9.101 | 4.516 | 3.428 |
| Actual sector, reuse on, deferred assessment, ms | 7.320 | 3.701 | 2.818 |

The exact path reduced dense kernel cost by 63.4% and actual sector preparation
by about 49–51% across the reuse/assessment matrix. Sparse points have few cache
benefits. Kernel configuration construction was about 6.5 ns; the first dense
batch and repeated batches were similar, with fresh request scratch each time.

The 983,040-position numerical sweep had zero cached-exact bit mismatches.
Integer specialization's largest incremental height/normal differences were
0.0001220703125 m and 0.0156393 degrees, changing one packed height and eleven
packed normal components. It remains experimental. Uniform-q tables and rsqrt
failed full-range height budgets at low fractional frequencies. Vector sqrt and
unrefined rsqrt had overlapping timings, and refinement had no repeatable speed
advantage. No approximation was enabled by default.

All 414 runtime and 46 editor tests passed with no skips, including the hardware
GPU suite. The actual renderer benchmark passed three explicit runs. Eight
matched Editor captures passed with unchanged source, binary and 62 authored-file
hashes. Uninstrumented reference/cached routes stayed near 60 FPS with no frames
above 33.3 ms; paired settled screenshots showed no new visible cracks or seams.
This is evidence of lower CPU work without an established FPS increase.
