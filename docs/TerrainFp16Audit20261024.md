# Terrain FP16 arithmetic audit — 2026-10-24

The audit found an actual gap beyond height arithmetic: Windows defines the SDK `real` aliases as `float`, and several terrain material helpers were explicitly float. The retained terrain/ground adapter now uses explicit `half` for the bounded work listed below. Shared SDK aliases are not globally redefined.

## Native arithmetic checked

Probes are appended to the actual cooked production HLSL and compiled with DXC shader model 6.2 and `-enable-16bit-types`. This tests emitted types, rather than assuming a source alias or texture format chooses arithmetic precision. Stage input/output conversions are allowed.

| Probe | Half arithmetic | Float arithmetic | Half math intrinsics | Float math intrinsics |
|---|---:|---:|---:|---:|
| material | 76 | 0 | 43 | 0 |
| culling | 0 | 2 | 0 | 2 |
| cache-factors | 24 | 0 | 8 | 0 |

The material/cache probes declare the DXIL requirement “Use native low precision.” Normalization, dot products and powers use 16-bit math intrinsics in the material probe. The culling probe deliberately emits float arithmetic. These are focused probes of the production helpers; they do not claim the entire renderer or entire production shader is FP16.

## Explicit FP16 paths

- Selected 16-bit height sampling, height composition, retained overlap weights and normalized CLOD height interpolation; 32-bit height mode retains the original float path.
- Ground patch color blending, linear sRGB→ACEScg matrix multiplication, normal-map decoding/reconstruction and material tint multiplication. Patch identities, sample coordinates and derivatives remain float.
- Terrain-to-ground color/normal/roughness/AO/specular response blending; unit-range diffuse BRDF and bounded direct/ambient response controls. HDR light intensity is multiplied after returning to float.
- Graph surface specular/AO channels and validation, cache-factor interpolation, and cache/direct material blending. The other graph material channels already used half.

Material math uses half in both height modes: the inspector selects height precision, while these bounded material channels match the existing 16-bit material caches. No texture layout, shader-resource-buffer layout or geometry LOD setting changed.

## Intentional FP32 boundaries

- World positions, transforms, camera distances, culling and stamp/cutout ownership decisions. At world height 1000, half precision has approximately 0.5-unit spacing; using it for a small ground-cutoff difference could change which triangles are removed.
- UV mapping, gradients/MIP decisions, procedural hashing and patch/triangle selection. Quantizing those would move texture samples or change the established patch distribution.
- Cache ownership tags, world-height validity checks and float surface-ID ranking. The mixed factors/metadata cache remains RGBA32F so its tag and world-height channels stay exact; only bounded specular/AO arithmetic is reduced.
- HDR lighting accumulation and intensity, sharp specular/clear-coat calculations and squared roughness terms. Very bright values or small GGX denominators can overflow/underflow FP16. Shared generic SDK/graph functions retain their original precision where their input range is not guaranteed.

## Editor verification

The same open editor, camera, 1203×656 viewport and 2× MSAA were used before and after. No level reload/save was performed. Input primitive counts remained 2,426,734 in both forward and depth. The simpler signed-distance culler is retained.

| Version | Case | Median frame ms | P95 ms |
|---|---|---:|---:|
| before material audit | simple-cull | 55.115 | 57.499 |
| before material audit | simple-cull-repeat | 55.064 | 57.836 |
| before material audit | simple-cull-32 | 55.293 | 58.086 |
| before material audit | simple-cull-16-restored | 55.463 | 57.460 |
| explicit material FP16 | material-fp16 | 54.786 | 56.994 |
| explicit material FP16 | material-fp16-repeat | 54.832 | 57.387 |
| explicit material FP16 | height32-material-fp16 | 53.084 | 56.874 |
| explicit material FP16 | height16-restored | 53.710 | 57.211 |

No reliable end-to-end FPS improvement is claimed. The image comparison against the pre-audit signed-distance version showed a maximum difference of 1/255 per RGB channel, mean 0.014328/255, with zero pixels exceeding that one-level rounding difference. Performance HUD and the small transform-gizmo origin highlight were excluded; exact masks are in the JSON report.

Both height modes compiled and ran in the editor. R16 publications used DXGI format 10, R32 used format 2. The final run restored 16-bit mode, bindings, camera, visibility and frame pacing; the saved level hash was unchanged.

All six 2/3/4-source × 16/32-bit production overlap combinations compiled. Native height probes retained the expected half/float split. The hardware culling coverage fixture passed 112 cases with zero changed pixels. Python regressions passed 43 tests plus 20 subtests and 53 unittest cases. The native editor target built successfully, followed by live shader cooking with zero failed editor jobs.

Reproduce the precision inspection after asset cooking:

```text
python tests/tg/VerifyTerrainMaterialPrecision.py --project <TGProject> --engine <O3DE-SDK-TG> --output <output-directory>
```
