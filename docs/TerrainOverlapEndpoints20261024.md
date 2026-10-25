# Retained overlap endpoint optimization, 2026-10-24

The final controlled editor comparison improved from **51.52 ms** to
**50.14 ms**, saving **1.38 ms (2.7%)**.
This is a small measurable improvement, not a major FPS increase. It applies
after the previous empty-cutout specialization (commit ba4e342).

## Behavior

Two-source retained overlap now exits as soon as a queried contributor cannot
participate. It also recognizes the existing material endpoint when a fully
covered source makes the other source's original ramp factor exactly zero.
The test uses the original ramp, including its FP16/FP32 rounding. It does not
approximate a distance threshold. All actual blends still run the original
weight resolver, normalization, participant test, and material channel loop.
Three- and four-source overlap retain their original selection logic.

World-position derivatives are captured before varying endpoint exits. Per-source
UV gradients are transformed only when that source's material is sampled, reducing
query state from eight floats to four per source. SampleGrad receives the same
values as before. The small additional timing improvement from gradient deferral
is close to ordinary timing variation; no independent large gain is claimed.

No material properties, SRG layouts, textures, publication metadata, height
storage, normal reconstruction, ground blending, geometry, geometry LOD, render
distances, or clipmap scheduling changed. The inspector remains at 16-bit height
precision. Shader-only work is canonical in terrain-compositor and synced to TG.

## Editor measurements

AMD Radeon(TM) Graphics, DX12, 1203x656, MSAA 2x, both retained terrains visible,
procedural ground active, identical camera. Each batch requests 240 frames.
Engine Cpu Time includes GPU/render waits; it is not isolated CPU work. Shader
compilers and asset processing were idle during reported frame batches. Individual
Forward pass timestamps remain unreliable in this SDK; full GPU frame envelopes
are retained in the JSON.

| Capture / case | Median ms | P95 ms | Approx. FPS |
|---|---:|---:|---:|
| before / before | 51.624 | 54.807 | 19.37 |
| before / before-repeat | 51.412 | 53.780 | 19.45 |
| endpoints / after | 50.313 | 52.577 | 19.88 |
| endpoints / after-repeat | 50.451 | 53.251 | 19.82 |
| gradients / deferred | 50.071 | 52.000 | 19.97 |
| gradients / overlap-off | 45.927 | 47.705 | 21.77 |
| gradients / deferred-repeat | 50.204 | 53.151 | 19.92 |
| final / final | 50.114 | 51.827 | 19.95 |
| final / final-repeat | 50.169 | 52.442 | 19.93 |

The overlap-disabled diagnostic is temporary and restored before the next case.
It measures 45.927 ms versus 50.071-50.204 ms with the full new blend path,
suggesting approximately 4.1-4.3 ms of remaining overlap cost in this view.
It is a material ablation, not a directly timed isolated overlap GPU pass.

Forward and Depth each retain 2,426,734 input primitives; Forward pixel-shader
invocations remain 994,108. Every full optimized image compared pixel-identically
over 701,588 pixels, excluding only the HUD and a small editor marker. Disabling
overlap changes 1,410 compared pixels (maximum channel error 6), confirming the
view exercises visible blending rather than only endpoint pixels.

## Validation and limitations

The actual cooked endpoint helpers compile to 12 half arithmetic operations and
zero float operations in 16-bit mode, and 12 float operations with zero half in
32-bit mode. Coordinate/gradient precision remains float. All six production
pixel-shader combinations (2/3/4 contributors x 16/32 bits) compile with DXC.
The fixed-constant audits are offline validation, not captured driver binaries.

A rounded NumPy reference exercise covers 504,096 cases per precision, with zero
false endpoint rejections. It includes footprint fades, contact bands, finite
extremes, subnormals, and invalid inputs. It checks the rejection law independently
of GPU execution; the editor images and production compilation are separate
checks. Existing overlap behavior tests (13), precision regressions (6), and
consumer synchronization tests (4) pass.

During deferred-candidate warmup the SDK emitted one ShaderVariant assertion:
"OptionGroup for specialization is different to the one in the ShaderAsset".
No shader-option or material-property declarations changed. Processing completed,
the capture continued with correct images/counts, and a separate settled final
run verifies the delivered state. No editor restart or component reactivation
was used in this work.

All runs restore camera, visibility, material bindings, precision, and the saved
level. Stationary-camera timings do not characterize clipmap boundary spikes or
other GPUs. Raw capture paths, complete metrics, source-count compile results,
and image comparisons are recorded in the companion JSON.

Saved level SHA256: `70f2db2744cd2d2fa2c5808c157ca463913756744d1bae0025108b859cfc7a5d`.
