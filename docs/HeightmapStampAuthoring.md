# Heightmap stamps: authoring, import, and acceptance

Implements the editor integration and delivery workflow for [issue #10](https://github.com/brooteskan/TG/issues/10),
including the quality and reconstruction controls added by [issue #25](https://github.com/brooteskan/TG/issues/25).
Runtime sampling, persistent ordering, snapshots, and granular invalidation are shared with phases one through five.
Asset Processor product verification, visual acceptance, and hardware-specific performance measurements remain user-run.

## 1. Build only in the existing tree

Close the editor before updating its gem DLLs. Use the existing `TGProject/build/windows` tree and the configuration you
already build (`debug` or `profile`). For an existing **profile** installation, from the repository root:

```powershell
cmake --build .\TGProject\build\windows --config profile --target TerrainCompositor TerrainCompositor.Editor
```

Use `--config debug` instead if that is your existing built configuration. Do not switch configurations to work around an
error, create a clean build tree, or rebuild the engine from scratch. Normal incremental CMake regeneration discovers the
added files in the existing editor target. If the build unexpectedly wants to rebuild the entire engine, stop and inspect
the configuration. Open your existing editor installation and let its Asset Processor process the new sample sources.
No build/import/visual validation is performed automatically by this feature.

Tools/Builders load `TerrainCompositor.Editor`, which includes the runtime descriptors once plus the dedicated editor components
and the identity processor. Clients/Servers keep the runtime module without AzToolsFramework. No shader, material, renderer,
or engine changes are required.

## 2. Required wiring

| Entity | Component/reference |
| --- | --- |
| Procedural Ground Source | Keep the existing Procedural Ground Gradient, IDs, settings, transform, and parenting |
| Terrain Composition Source | Add **Terrain Composition Gradient**; Procedural Source = the existing ground; Target Terrain Region = Terrain Region |
| Terrain Region | Keep its existing region shape and terrain components; use **Terrain Composition Height Provider** referencing the composition entity |
| Stamp Authoring | Optional separate parent with identity rotation and scale 1; parent it under the unrotated level root |
| Each stamp | Transform + **Heightmap Stamp**; Target Composition = Terrain Composition Source |

The compositor must be on a separate entity, not on Terrain Region or the procedural source. Do not put the base gradient
or individual stamps in a stock Terrain Height Gradient List: that path cannot return terrain existence and its maximum
blend would defeat Replace blending and depressions. Do not use overlapping Terrain Layer Spawners for stamp blending.

Keep the existing 64000 x 64000 x 2048 region dimensions and terrain-world settings. The current region mapping is
[-1024, +1024] meters; composition reads the region shape rather than hard-coding this interval. Do not rotate/reparent
the existing ground or terrain to accommodate stamps. Their existing pitch/roll does not need to change; put stamps under
the separate unrotated authoring parent instead.

The demonstration below is opt-in and should be performed in a disposable level copy. Do not save active demonstration
stamps or a replacement Constant Gradient into the base DefaultLevel.

## 3. Import the supplied source data

The sample sources are under `Assets/HeightmapStamps`:

| Source | Exact data |
| --- | --- |
| `constant_mid_gsi16.tiff` | 64 x 64; every unsigned pixel is 32768 |
| `landmarks_gsi16.tiff` | 64 x 64; asymmetric quadrant plateaus, an upper-left L, and a lower-right marker |

Both are single-channel, unsigned 16-bit, uncompressed TIFFs with top-left row origin and no display-gamma conversion.
Their adjacent `.tiff.assetinfo` files request GSI16, size reduction 0, Use Max Res, and generated mips. Keep sources and
settings together. The deterministic PowerShell generator in `Gem/Tools/GenerateHeightmapSamples.ps1` reproduces these four
files without external libraries; it overwrites only those named samples and does not run Asset Processor.

In Asset Browser, right-click each **source TIFF**, choose **Edit Texture Settings...**, and verify:

1. **Preset = GSI16**: Linear source/destination, R16, power-of-two dimensions.
2. **Res Limit = 0** and **Use Max Res = enabled** on your target platform.
3. **Max Res = 64 x 64** for these sources. Platform limits and image-quality settings must not remove front mips or resize them.
4. After processing, select the **StreamingImage** product for the stamp. Its format must be uncompressed `R16_UNORM`,
   2D with one array slice and the original mip-zero dimensions. Mips can exist; the stamp always reads mip 0.

The serialized field `EngineReduce` stores `m_suppressEngineReduce`; `true` is the intended Use Max Res setting.
Do not assume its name means reduction is enabled. Actual imported products still need user verification.
R16 output alone cannot prove that an arbitrary source was never reduced to 8 bits, gamma-adjusted, or resized beforehand.
RGB(A), signed, float, 8-bit, and compressed product formats are unsupported in this first version.

## 4. Place and edit a stamp

Create Stamp Authoring as described above, add a child with **Heightmap Stamp**, select the composition and image, then edit:

| Control | Units/limits |
| --- | --- |
| Footprint Width / Depth | Finite, positive local meters; inspector minimum 0.001 m |
| Height Scale | Finite, nonnegative local meters |
| Vertical Offset | Finite, signed local meters relative to entity origin |
| Sampling Mode | Bilinear for compatibility; Smooth Cubic for bounded source reconstruction |
| Reconstruction Radius | [0, 8] source texels; 0 leaves source samples unchanged; 1 to 2 is the initial terrace range |
| Strength | [0, 1] |
| Edge Feather Width | Finite, nonnegative local meters; width + inset <= half the smaller footprint dimension |
| Edge Feather Exponent | Finite, >= 1; default 1 preserves the original curve |
| Edge Inset | Finite, nonnegative local meters with exactly zero contribution; default 0 disables it |
| Relative Edge Blend | On by default; relative displacement near the edge tapers to absolute Replace across the feather band |
| Priority | Signed integer; lower first, higher later |
| Transform | World XYZ placement, yaw only, positive uniform scale |

The footprint is centered on the entity. Scale multiplies footprint size, feather width, edge inset, height scale, and vertical offset.
The exponent is dimensionless and does not scale. Relative Edge Blend uses that same local feather band; it has no separate width.
It defaults on even when loading older stamps without the field. Turn it off to compare against the previous pure Replace blend.
The entity's world Z is added once; it is not multiplied again. Parent transforms count: an apparently yaw-only child under
a tilted parent is invalid. Remove pitch/roll from the stamp's authoring hierarchy; do not change the existing terrain hierarchy.
The separate Non-uniform Scale component is incompatible. Zero/negative scale and nonfinite values are invalid.

Submitted invalid values remain editable but contribute nothing. Dimensions, feather, and inset edits refresh both width maxima.
If shrinking a footprint leaves feather + inset too large, lower either width to recover; runtime values are not silently clamped.
An inset equal to half the smaller dimension (with zero feather) covers the whole footprint and contributes nothing.
The Status label and transition diagnostics explain unavailable targets/assets and invalid placement. Status refresh is limited
to selected components and does not load assets or sample terrain. Existing generic-wrapper components remain loadable/exportable
but do not acquire the new editor outlines/status UI automatically; no automatic migration changes their IDs or saved data.

Property edits and Transform tools use the normal editor undo/redo and prefab workflow. Valid changes preview in edit mode.
To disable a component, select its header in Entity Inspector and open the component-header context menu. The installed editor
only exposes Disable component when the selected component is editable/removable. If that action is unavailable, set Strength
to 0 to isolate a stamp without deleting it; this removes its height contribution but does not exercise component deactivation.
Do not use entity visibility or Start inactive as substitutes for edit-mode component disabling. Removal followed by undo is
another lifecycle check in a disposable level. Undo restores the serialized authoring state. The preview is nonserialized
and passes copies to the runtime setters, so terrain worker queries cannot observe partially edited inspector values.

When selected, a valid stamp draws a cyan outer footprint, a pink inset boundary when inset is positive, and a gold inner feather
boundary when feather is positive. Between cyan and pink the contribution is exactly zero; between pink (or cyan without inset)
and gold it ramps to full Strength. With Relative Edge Blend enabled, this same band also transitions from relative relief to
absolute Replace, reaching ordinary Replace at gold. The exponent shapes influence without moving the outlines or changing
the relative-to-absolute taper. Green cues identify image top
(local +Y) and image right (+X). These outlines lie at the entity's **authoring Z**, not on the terrain surface. They work even
before image pixels are ready. A rejected transform gets a diagnostic instead of a misleading flattened footprint. At maximum
combined inset/feather width the inner rectangle can collapse to a line/point; zero inset and zero feather have only the outer boundary.

## 5. Orientation, interpolation, and height

At zero yaw: image left/right maps to world -X/+X and image top/bottom maps to world +Y/-Y. At +90 degrees yaw, image top points
toward world -X. Width/depth stretch the whole image exactly once, independently of its pixel aspect ratio.

```text
localXY = inverseYaw(worldXY - entityWorldXY) / uniformScale
u = localX / width + 0.5
v = localY / depth + 0.5
pixelX = u * (imageWidth - 1)
pixelY = (1 - v) * (imageHeight - 1)

h = unsignedPixel / 65535
stampHeight = entityWorldZ + uniformScale * (verticalOffset + heightScale * h)
```

Bilinear samples use bilinear interpolation and clamped pixel neighbors inside the footprint; outside positions contribute nothing.
Edges do not repeat, mirror, or tile. There is no automatic min/max normalization or inversion. A ready all-zero image is valid
height data; a missing image is not a zero-height stamp.

Smooth Cubic prepares a separable cubic B-spline reconstruction once when the source revision, mode, or radius changes.
It does not run a multi-tap filter in terrain query threads. Each nonnegative normalized pass is clamped to its contributing
source range, so constants remain exact and sharp terraces spread without ringing or new extrema. The prepared buffer is shared
by immutable snapshots; scalar and batch queries use the same final sampler. Changing either reconstruction control republishes
and invalidates only that stamp's old/current footprint.

For the constant image, `h = 32768 / 65535`, approximately `0.5000076295`, **not exactly 0.5**. With world Z=20, scale=2,
offset=-5, and height scale=40, the target is approximately **50.0006104 m** before feathering/strength and terrain clipping.
Allow decoded-float precision; account for bilinear interpolation when not sampling a constant plateau.

For the landmark image, pixel coordinates `(x, y)` count from the top-left, starting at zero:

| Pixel | Unsigned value | Landmark |
| --- | ---: | --- |
| (8, 8) | 65535 | Upper-left high plateau |
| (48, 8) | 49151 | Upper-right plateau |
| (8, 48) | 16384 | Lower-left low plateau |
| (56, 56) | 0 | Lower-right bottom plateau |
| (10, 12) | 32768 | Upper-left L |
| (46, 48) | 43690 | Lower-right marker |

For an exact pixel location on a 64-pixel source: `localX = (x/63 - 0.5)*width` and
`localY = (0.5 - y/63)*depth`; apply the entity's yaw, uniform scale, and translation. Use strength 1 and feather 0 for
direct height checks with inset 0. Neither inset nor exponent crops/rescales the image coordinates. Use an interior plateau for
easier placement checks with positive feather.

## 6. Feathering, priority, and persistent ties

The canonical sampling and composition contract—including edge inclusion, inset/feather behavior, relative-edge blending,
meter conversion, and exact-base fallback—is maintained in
[Terrain composition and heightmap stamps](TerrainComposition.md#feathered-accumulated-replace-with-tapered-relative-edges).
Use that algorithm when interpreting the controls and expected values in the demonstrations below. Authoring order remains
ascending priority followed by the persistent key; later stamps blend into the accumulated lower layers.

Equal priorities compare the complete stable key lexicographically as unsigned bytes. Authoring keys encode saved entity aliases
and ancestor prefab-instance aliases relative to the level root. Display names, activation order, and runtime entity IDs do not
participate. Normal entity/prefab duplication obtains a distinct alias path; undo/redo restores its saved path. Save/reopen and
Play/export preserve the same key. Moving between prefab ownership paths can change the key/order; undo restores it.

The identity processor runs before Editor info remover in both PlayInEditor and GameObjectCreation. It handles new editor components
and legacy wrappers, modifies only conversion copies, and preserves normal reference remapping. Unresolved/conflicting export keys
fail conversion; collisions at runtime suppress **all** conflicting claimants. There is no random-key or first-activated fallback.

## 7. Deterministic two-stamp demonstration (opt-in)

Use a **disposable copy** of DefaultLevel. Leave its ground entity/settings intact. Add a temporary **Constant Gradient** on a separate
entity, set its value to `0.5048828125`, and temporarily select it as the composition's Procedural Source. With [-1024,+1024], the base
is exactly 10 m. Do not modify the region/world height mapping to run this example.

Create the unrotated Stamp Authoring parent (translation zero, scale 1). Under it add two Heightmap Stamp entities sharing the ready
`constant_mid_gsi16.tiff.streamingimage` product. Point both at the composition:

| Setting | A | B |
| --- | ---: | ---: |
| World XYZ | (-16, 0, 0) | (16, 0, 0) |
| Yaw / scale | 0 degrees / 1 | 0 degrees / 1 |
| Width / depth | 64 / 64 m | 64 / 64 m |
| Height scale | 0 m | 0 m |
| Vertical offset | 30 m | -10 m |
| Strength | 0.5 | 0.5 |
| Feather | 8 m | 8 m |
| Exponent / inset | 1 / 0 m | 1 / 0 m |
| Relative Edge Blend | Off (for these original Replace checks) | Off |
| Priority | 0 | 1 |

At world XY=(0,0), both masks have full interior weight: `10 -> 20 -> 5 m`. Reverse priorities and expect `10 -> 0 -> 15 m`.
At (-24,0), only A covers and gives 20 m; at (24,0), only B covers and gives 0 m. Outside both, expect 10 m. Give later B strength 1
and expect -10 m at (0,0). Set both strengths to zero and expect exact base fallback. The image must still be ready even when height
scale is zero: an unavailable image contributes nothing, not an offset-only stamp.

At (-44,0), A is halfway through its feather and B does not cover: effective weight 0.25 and expected height 15 m.
At (-44,28), A is halfway through both edge bands: effective weight 0.125 and expected height 12.5 m. At A's left edge (-48,0),
positive feather yields 10 m; with feather zero it yields 20 m, while a point just outside still yields 10 m.

To check the new controls, keep A's feather at 8 m, set its inset to 4 m and exponent to 2; leave B unchanged. At (-46,0) and
(-44,0), A contributes exactly zero, including the inset boundary, and the result is 10 m. At (-40,0), halfway through the
remaining transition, the effective weight is `0.5 * 0.5^2 = 0.125` and the result is **12.5 m**. At (-40,24), halfway through both
bands, the weight is `0.5 * 0.25^2 = 0.03125` and the result is **10.625 m**. At (-36,0), the feather is complete and the result is
20 m. With feather 0 and inset still 4 m, (-44,0) remains 10 m while (-43,0) jumps to 20 m. With inset 32 m and feather 0, A
contributes nowhere; reduce inset to restore it. Restore exponent 1, inset 0, and feather 8 before continuing other checks.

For the tapered relative-edge check, use A's inset 4 m, feather 8 m, exponent 2, Strength 0.5, Height Scale 0, and Vertical Offset
30 m. Turn **Relative Edge Blend on** for A. At (-40,0), `replaceBlend=0.5` and weight=0.125: the result is now **11.25 m** instead
of the pure-Replace 12.5 m. At (-40,24), `replaceBlend=0.25` and weight=0.03125: expect **10.15625 m** instead of 10.625 m.
At (-36,0), the result remains **20 m**; the inset boundary still returns 10 m. These checks deliberately use zero Height Scale
to isolate the taper of absolute baseline influence. With a nonzero image displacement, its weighted amount is added throughout
the feather band as defined by the canonical composition algorithm. Set both stamps back to the table settings to repeat the original overlap checks.

For placement, switch one stamp to the landmark image, strength 1, height scale 40 m, offset 0, and inspect the features while
moving XYZ, yawing 90 degrees, scaling to 2, and resizing width/depth independently. Lower feather before making a dimension too small.
Keep original transforms/settings in undo history so the numerical overlap case can be restored.

For reload checks, edit a **copy** of the constant source and point both stamps at that shared copy. Set Height Scale to 40 m on
both stamps first: the zero height scale used above deliberately ignores pixel-value changes. Change the copy's pixel values
without changing its unsigned 16-bit format and let Asset Processor finish. Both dependent footprints must update.
Do not confuse source editing with a verified imported product. Restore the original source
selection after the check. Remove temporary stamps/Constant Gradient or discard the disposable level when finished.

## 8. Runtime updates and diagnostics

On the main/entity thread:

```cpp
auto config = stamp->GetStampConfiguration();
config.m_verticalOffset = -20.0f;
stamp->SetStampConfiguration(config); // Preserves its persistent identity; safe while active.

TerrainCompositor::TerrainCompositionConfig compositionConfig;
composition->WriteOutConfig(&compositionConfig);
compositionConfig.m_proceduralSourceEntityId = newBaseEntity;
composition->ReadInConfig(&compositionConfig); // Publishes the source/region change safely.
```

Use the ordinary Transform bus for placement. Do not call `AZ::Component::SetConfiguration` on an active component and do not write
from terrain worker threads. Programmatic clones must call `AssignNewPersistentOrderingIdentity()` on a copied configuration **once
before activation**, persist it, and remap their target references. Blindly copying a baked runtime key into the same composition is
not a cloning API. Ordinary updates must preserve identity. There is no script-specific stamp bus in this version.

Shared image data is loaded asynchronously and retained by immutable snapshots. Switching assets, deactivation, and context/session
changes reject stale work. Missing/loading/failed/unsupported assets reveal the underlying terrain; a ready zero image does not.
Inspect the Status label and the `HeightmapData`, `HeightmapStamp`, `TerrainComposition`, and `HeightmapStampExport` log categories.
Import errors require fixing the source/settings and reprocessing; repeated sampling does not retry blocking I/O or emit per-query logs.

Movement/removal invalidates captured old/new footprints after publication, padded/clipped to the region. Priority and parameter edits
refresh the full affected footprint. Retargeting cleans the old context and adds to the new one. Ground/region changes may invalidate
the whole affected region; tint-only changes do not. The engine can union separate dirty AABBs downstream, so separate project
notifications do not guarantee separate distant renderer updates. See [TerrainInvalidation.md](TerrainInvalidation.md).

## 9. User-run acceptance record

All checks below are **pending**. Use the existing build tree and disposable setup. A successful compilation alone does not check them.
For numerical agreement and exact fallback, compare gradient results in the debugger, not screenshots. Existing scalar `GetValue` and
batch `GetValues` entry points use the same sampling helper; compare unchanged state at identical XY locations. Terrain-level queries
may additionally interpolate/clip results, so distinguish those from direct gradient checks.

- [ ] Empty composition, zero strength, and unavailable images return the exact procedural base sample.
- [ ] XYZ, yaw, uniform scale, and footprint resizing place the documented landmarks correctly; edges never repeat.
- [ ] Known pixels follow the height formula, including quantization, and lower targets create depressions.
- [ ] Zero and positive feather behave as documented at edges/corners; maximum feather remains valid.
- [ ] Exponent/inset produce the stated edge/corner values, exact zero strip, unchanged interior Strength, and hard-edge behavior.
- [ ] Exponent < 1, negative/nonfinite inset, and inset + feather beyond the limit invalidate and restore old coverage; repair recovers.
- [ ] Exponent/inset edits refresh the terrain and viewport boundaries; undo/redo, save/reopen, and Play/export retain both values.
- [ ] Absent exponent/inset fields retain the original mask; turn Relative Edge Blend off for exact old heights. Scale multiplies both widths equally.
- [ ] Relative Edge Blend defaults on, tapers only across positive feather, and survives save/reopen, undo/redo, and Play/export.
- [ ] Toggle Relative Edge Blend: documented edge/corner results change, full interior and zero-feather results remain identical.
- [ ] Black pixels add no relative relief; nonzero pixels retain scaled relief; lower-priority layers supply the relative reference.
- [ ] The overlap/depression example gives 5 m, reversed priority 15 m, and the stated edge/corner values.
- [ ] Equal-priority order survives entity duplication, nested prefab duplication, save/reopen, undo/redo, and Play/export.
- [ ] Move/rotate/resize/Z-only edits restore previous footprints without a project-wide rebuild; drag intermediates leave no trails.
- [ ] Disable/remove, asset loss, invalid transforms, and retargeting restore the correct previous composition.
- [ ] Shared-image ready/reload/failure/recovery reaches every dependent stamp; obsolete callbacks never resurrect a contribution.
- [ ] Unsupported source/product formats and transforms give actionable warnings; correction restores contribution.
- [ ] Scalar/batch results agree for unchanged state, and edit-mode/Play/runtime results match.
- [ ] Play transitions and repeated activation/deactivation do not duplicate registrations or mix entity contexts.
- [ ] With Physics/AI simulation off, repeat Play -> exit several times: ready heightmaps appear in both modes after loading.
- [ ] Repeat with Simulate enabled before Play, then exit Play and disable Simulate: stamps recover in the editor without
  reimporting assets or changing parameters. No old memory-unload event produces a HeightmapData missing-product warning.
- [ ] Existing ground controls, world bounds, serialized settings/transforms, camera, lighting, and near/distant tint remain unchanged.
- [ ] The guide and samples reproduce the setup without relying on an untracked image or a preexisting stamp.

Useful debugger locations: `HeightmapStampIdentityProcessor::Process`, `PublishStamps`, `ComposeHeightmapStamps`,
`TerrainInvalidation::BuildRegions`, and `DispatchChanges`. Observe stable keys, snapshot revisions, asset revisions, and dirty bounds.
Do not add a new automated test or concurrency harness without separate authorization. Keep the current test target unchanged.
