# Terrain composition and heightmap stamps

Implements the foundation in [issue #5](https://github.com/brooteskan/TG/issues/5), image data in
[issue #6](https://github.com/brooteskan/TG/issues/6), placement/blending in
[issue #7](https://github.com/brooteskan/TG/issues/7), persistent ordering/query ownership in
[issue #8](https://github.com/brooteskan/TG/issues/8), and granular terrain invalidation in
[issue #9](https://github.com/brooteskan/TG/issues/9). Dedicated editor authoring in
[issue #10](https://github.com/brooteskan/TG/issues/10) is described in the [authoring guide](HeightmapStampAuthoring.md). See the
[ordering, export, lifecycle, and validation contract](HeightmapOrderingAndSnapshots.md) and
[invalidation contract and user-run checks](TerrainInvalidation.md).
Optional categorical surface-ID stamps, the composition-scoped palette, and terrain-region provider are documented in
[TerrainSurfaceComposition.md](TerrainSurfaceComposition.md).
Explicit procedural/stamp holes and the Terrain Composition Height Provider are documented in
[TerrainHoles.md](TerrainHoles.md).
Opt-in terrain-grid/renderer quality and per-stamp reconstruction controls from
[issue #25](https://github.com/brooteskan/TG/issues/25) are documented in
[TerrainQualityAndReconstruction.md](TerrainQualityAndReconstruction.md).
The runtime foundation preserves terrain bounds, transforms, shaders, materials, and existing ground serialization.
The authoring guide describes the minimal composition wiring and opt-in demonstration. Visual acceptance and project-specific
performance measurements remain user-run because they depend on the active level, camera path, and hardware.

## Component and query contract

`TerrainCompositionGradientComponent` provides `GradientService` and `TerrainCompositionService`.
Place it on a **separate entity** from both the existing procedural gradient and Terrain Region.
The installed Terrain Height Gradient List declares `GradientService` incompatible with its own entity.

`TerrainCompositionConfig` references the existing procedural source and target terrain region, and contains an opt-in
Terrain Quality profile. The profile is inert by default and never replaces Terrain World or Terrain World Renderer ownership.
Both runtime activation and the generic editor wrapper connect the query handler using the actual owning entity ID.
Terrain Region uses **Terrain Composition Height Provider** to consume the compositor's world height plus explicit
existence result. Older GradientRequestBus-only wiring cannot represent terrain holes. Use the wiring in
[TerrainHoles.md](TerrainHoles.md) and the [authoring guide](HeightmapStampAuthoring.md).

Queries use `GradientRequestBus`, never final terrain height requests. A scalar query requests its base once;
a nonempty batch makes one base `GetValues` request. Both then use `ComposeHeightmapStamps` with one acquired
composition state for the entire call. Positions with no contributing stamp return the original base value directly,
without a normalized/meters round trip. Empty batches are no-ops. Mismatched spans assert and leave the output untouched.
Missing, self-referencing, region-referencing, or cyclic sources produce zero in both query paths.
Hierarchy checks run at query time as well as configuration time, so late source activation or dependency edits
cannot bypass cycle rejection. Reentrant hierarchy traversal through a compositor terminates conservatively.

The target's encompassing AABB is cached through `ShapeComponentRequestsBus`. Shape changes and target
activation/deactivation refresh or clear it; retargeting disconnects the old subscriptions and clears the old bounds.
The target's dependency notifications are deliberately **not** followed, because it consumes this gradient.
The elevation mapping is `z = minZ + normalizedHeight * (maxZ - minZ)`. A missing, nonfinite, or zero-height
region returns the procedural sample unchanged even if stamps are present. There is no duplicated 2048-meter constant.

## Stamp registration contract

`HeightmapStampConfig` serializes the image asset, centered local footprint width/depth, height scale,
vertical offset, sampling mode, reconstruction radius, strength, inward feather width, feather exponent, zero-contribution edge inset, relative edge blend, priority, stable ordering key
(with legacy UUID compatibility), and target composition entity. Version three adds `FeatherExponent` (default 1) and `EdgeInset`
(default 0); absent fields retain their defaults so old stamps keep their feather mask and ordering identity. Version four adds
`RelativeEdgeBlend`, default true even when absent in old data. This intentionally changes heights within positive feather bands;
disable it to restore pure Replace. Interior and zero-feather heights are unchanged.
Version seven adds `SamplingMode` and `ReconstructionRadius`. Their absent-field defaults are Bilinear and zero texels,
so existing stamps retain their exact sampling path.
The image reference uses the existing Atom `StreamingImageAsset` type. The phase-two registration helper
now loads it asynchronously through the shared [heightmap data cache](HeightmapData.md).
Placement is supplied separately as the entity's world transform. The sampler supports yaw and positive uniform
scale only, with `stampZ = entityZ + scale * (verticalOffset + heightScale * sample)`.

`HeightmapStampComponent` owns a `HeightmapStampRegistration` lifecycle helper. It calls `Activate` with the
**editor wrapper's entity ID in edit mode** (or the runtime entity ID), `Update` after configuration/world-transform
changes, and `Deactivate` before destruction. Parent changes are revalidated. All configuration, registration,
and lifecycle operations run on the main thread. A missing transform fails closed instead of substituting identity.

The request and notification buses are addressed by `(entity context UUID, composition entity ID)`.
Only the compositor owns its registration map. No process-wide stamp list exists, and all records are value copies.
Unknown contexts fail closed; the registration helper retries late context ownership on SystemTick.

- A stamp subscribes before attempting registration. If the compositor is absent, it waits at that address.
- A compositor connects its request handler, then announces availability so waiting stamps replay current data.
- Removing a compositor disconnects requests, clears its map, and announces unavailability. Stamps retain their
  subscriptions and current data for reactivation or replacement of the component on that entity.
- Retargeting or changing context unregisters from the old address before subscribing to the new one.
- Repeated current-lease/revision requests are idempotent; obsolete sessions, revisions, and lease tokens are rejected.
- Invalid/noncontributing claims remain tracked but are excluded from snapshots.
- Duplicate keys suppress every claimant, with diagnostics and deterministic recovery after repair/removal.
- Unregistration requires the stamp's current registration token and composition session.

The blend order is ascending priority, then unsigned-byte lexicographic stable key. Editor keys derive from
saved instance-qualified prefab/entity aliases relative to the level root; runtime configuration contains
the exact baked key. No activation order, map order, or runtime entity ID participates in comparison.
See the [identity contract](HeightmapOrderingAndSnapshots.md) for key encoding, legacy UUID compatibility,
the one-time switch from old UUID ordering, prefab export, duplication, and programmatic key assignment.

Each query acquires one complete immutable snapshot containing references, region elevation mapping, sorted
prepared records, transforms, parameters, and shared image ownership/revisions. The writer atomically exchanges
the complete shared pointer; workers retain their snapshot for the scalar call or entire batch. No worker reads
editable configuration, loads/decodes assets, logs diagnostics, sorts stamps, or enumerates mutable registrations.

## Stamp setup and updates

1. Create a separate entity with **Heightmap Stamp**, under an unrotated authoring parent. Do not parent it under
   the existing tilted terrain/source entities unless its effective world rotation is still yaw-only.
2. Set **Target Composition** to the composition entity, not Terrain Region or the procedural source.
3. Select a ready StreamingImage product imported using the [GSI16 route](HeightmapData.md).
4. Set the local footprint, height scale, offset, strength, and feather. Move XYZ, yaw, and uniform scale with Transform.

The component provides `HeightmapStampService` and requires `TransformService`; it is not a gradient provider.
`NonUniformScaleService` is incompatible and is also checked defensively during registration. O3DE's world transform
contains uniform scale only; silently reading that transform would otherwise discard separate nonuniform scale.
Dedicated editor components reuse the runtime EditorActivate/EditorDeactivate and configuration update paths without adding
editor-only dependencies to runtime targets. Legacy generic wrappers remain readable/exportable. New authoring includes
selected footprint/feather outlines and read-only diagnostics; see the [authoring guide](HeightmapStampAuthoring.md).

For active runtime components, call `HeightmapStampComponent::SetStampConfiguration` on the main thread, using a copy
from `GetStampConfiguration` so the ordering identity is preserved. For a programmatic clone, call
`AssignNewPersistentOrderingIdentity()` on the clone's configuration before registration and persist it. `ReadInConfig` uses the same update path.
Do not use `AZ::Component::SetConfiguration` on an active component; the engine disallows that API while active.
Transform changes use the ordinary Transform bus. No worker-thread setter or script-specific stamp bus is provided.

### Validation and fallback

| Property | Rule |
| --- | --- |
| Width/depth | Finite, positive local meters |
| Height scale | Finite, nonnegative local meters |
| Vertical offset | Finite, signed local meters |
| Strength | Finite, in [0, 1] |
| Feather / inset | Finite, nonnegative local meters; feather + inset <= min(width, depth)/2 |
| Feather exponent | Finite, >= 1; applied to the mask before Strength |
| World transform | Finite, normalized rotation, yaw-only, positive uniform scale |

Validation is shared by inspector/runtime updates and registration. Submitted invalid values are not silently
clamped or replaced with the previous valid settings: they remain editable and contribute nothing until corrected.
Resizing revalidates feather and inset; each inspector maximum accounts for the other width. Lower either width before
shrinking the footprint if their sum would exceed the new limit. A full-footprint inset with zero feather is valid but
noncontributing, and is excluded from snapshots while its registration remains tracked. Rotation checks use a 1e-4 tolerance on
quaternion squared length and the rotated up-vector's deviation from +Z; numerical drift is accepted, inherited tilt is not.
World bounds and target-height endpoints must fit in finite floats. Double intermediates avoid overflow in placement/blending.
The registration helper warns once per validation-state transition, naming the stamp and corrective action.

Unassigned/loading/missing/failed images contribute nothing; a ready all-zero image is valid data. If the region
mapping is unusable, the original base is returned. Extreme height/region combinations that cannot produce a finite,
representable float gradient also fall back to the base; they are not clamped into a different stamp target.

## Footprint, image orientation, and source reconstruction

The footprint is centered on the entity. For world XY position p, translation t, yaw theta, and uniform scale s:

```text
localXY = inverseYaw(theta) * (p - t.xy) / s
u = localX / width + 0.5
v = localY / depth + 0.5
pixelX = u       * (imageWidth  - 1)
pixelY = (1 - v) * (imageHeight - 1)
```

Image left/right maps to local -X/+X; image top/bottom maps to local +Y/-Y. At zero yaw, the image top is world +Y;
at positive 90-degree yaw it is world -X. Width/depth cover the complete image once, regardless of image aspect ratio.
Uniform entity scale multiplies footprint dimensions and feather width as well as elevation parameters.

Queries first reject against the conservative world AABB's **XY** extents, then against the exact local rectangle.
Query Z and stamp-origin Z never determine footprint coverage. AABBs are rounded outward so they do not shrink the footprint.
The Bilinear mode uses bilinear interpolation with clamped valid-edge neighbors and never repeats, mirrors, or tiles.
Smooth Cubic applies a nonnegative, normalized cubic B-spline prefilter at snapshot publication, clamps each pass to its
contributing source range, then uses the same bilinear query sampler. This preserves constant plateaus and cannot create
ringing outside the contributing neighborhood. Radius zero skips allocation and is byte-for-byte compatible with Bilinear.
Prepared images are cached by source identity, source revision, mode, and radius and are immutable after publication.
Pixel-coordinate clamping happens only after footprint acceptance. A 1x1 image is constant, 1xN/Nx1 images interpolate
along their nondegenerate axis, and zero-sized/malformed buffers are rejected before publication.

## Feathered accumulated Replace with tapered relative edges

```text
range = regionMaxZ - regionMinZ
height = regionMinZ + baseGradient * range
baseline = entityWorldZ + s * verticalOffset
displacement = s * heightScale * imageSample
stampHeight = baseline + displacement

dx = width/2 - abs(localX)
dy = depth/2 - abs(localY)
smooth(t) = t*t*(3 - 2*t), with t clamped to [0, 1]
if dx < 0 or dy < 0: skip stamp
if inset > 0 and (dx <= inset or dy <= inset): skip stamp
replaceBlend = 1
if feather == 0:
    weight = strength
else:
    mask = smooth((dx-inset)/feather) * smooth((dy-inset)/feather)
    replaceBlend = clamp(mask, 0, 1)
    weight = strength * pow(mask, exponent)
if relativeEdgeBlend and replaceBlend < 1:
    height += weight * (displacement + replaceBlend * (baseline - height))
else:
    height = height * (1 - weight) + stampHeight * weight

composedGradient = (height - regionMinZ) / range
```

The height update repeats in sorted order, using the accumulated result. Lower targets form depressions; no maximum
blending or per-stamp height clamp is used. Terrain Height Gradient List retains its existing terrain-world clipping.
Conversion to meters is deferred until the first nonzero contribution; outside stamps and at zero-weight edges the exact
base sample is returned if no other stamp contributes. The image is not read when strength/feather weight is zero or the query
lies in the inset. Zero contribution preserves the accumulated lower layers, not necessarily the original procedural base.

Positive feather width starts after the zero-contribution inset, reaches full mask weight at least `inset + feather` inside
both axes, and has zero-slope mask endpoints for every supported exponent (>= 1). At exponent 1, half-band masks are 0.5 along
one axis and 0.25 at a corner halfway through both bands; exponent 2 changes them to 0.25 and 0.0625. Strength is multiplied
after exponentiation so the interior weight is unchanged. The default exponent-1 path retains the original multiplication
order and avoids `pow`. Larger exponents reduce edge influence but concentrate the transition inward; they do not change UVs.

Relative edges blend from a target of `accumulatedHeight + displacement` near the edge to `baseline + displacement` at the
inner feather boundary. The relative fraction is `1 - replaceBlend`, using the unexponentiated mask independently of Strength.
The taper has zero-slope endpoints. It is confined to the existing feather band after the inset, without changing the full
interior's arithmetic or introducing an additional query, reference image, cache, or invalidation footprint.

Displacement is relief above the stamp's zero-sample elevation, computed directly as `heightRange * sample`; it excludes entity Z
and Vertical Offset. Those absolute baseline terms taper in toward the interior. Black pixels therefore have no relative relief,
while their baseline influence gradually returns across the band. The reference terrain is the current accumulated lower layers,
not a fresh procedural sample. With this option disabled, the original Replace calculation is retained everywhere. Nonzero image
features cut off at the border still need feathering; the mode is not an automatic boundary-height matching algorithm.

**Zero feather and zero inset retain the inclusive hard-edged rectangle.** With positive inset, the entire strip and its inner
boundary contribute nothing even with zero feather; strictly interior points then have weight equal to Strength. This path
uses neither division nor exponentiation; relative edges also have no effect on it. A full-footprint inset contributes nowhere
and is excluded from the query snapshot.

## Publication and invalidation boundary

Updates, asset readiness/reloads, and removal publish complete immutable query snapshots before notifying dependents.
Outbound stamp/dependency notifications are deferred to regular TickBus at `TICK_DEFAULT - 1`, before the installed
terrain system's tick and outside control-bus dispatch mutexes. SystemTick remains for context/asset lifecycle work;
it can run multiple times per frame. A valid stamp becoming invalid is removed from the query list immediately.
The previous snapshot supplies its old footprint and region context, retained in pending value-owned work until
dispatch. Further invalid-to-invalid edits do not repeatedly invalidate that already-removed contribution.
`OnStampFootprintChanged` includes previous/current XY bounds, previous/current region IDs and AABBs, composition
session, and snapshot revision. Retargeting sends removal to the old composition and addition to the new one.

`TerrainInvalidation` expands XY by two terrain height-query spacings, extrudes through each captured region's Z
range, clips, and coalesces nearby coverage. Empty/outside results emit nothing. Distant old/new regions remain
separate at the compositor notification boundary. Every intermediate published drag footprint remains covered.
The compositor emits only finite `OnCompositionRegionChanged` bounds, never broad `OnCompositionChanged` events.
Z-only, priority/key, strength, feather width/exponent, inset, relative-edge toggles, and shared-image revision changes refresh full affected footprints.

Procedural-source events enter a weak-owned subscription mailbox and join the same deferred path. Unbounded base
changes and source/region mapping changes invalidate finite whole-region bounds. Shape/lifecycle subscriptions
never monitor the consuming region's dependency bus. Whole-composition shutdown also removes the base and queues
whole-region cleanup for retained contexts using copies that survive destruction. Tint-only updates remain outside
height invalidation. No new locks, buses, or asset work enter scalar/batch sampling.

The installed O3DE terrain system unions received dirty AABBs before its tick. Separate compositor notifications
therefore do not guarantee separate downstream renderer updates; see [the engine limitation and checks](TerrainInvalidation.md).

## Existing procedural ground

The existing component/configuration IDs and serialized field names are unchanged. The hill/depression algorithm,
normalization constants, and shared tint material workflow are preserved. The editor callbacks now connect and
disconnect the gradient handler as well as the tint lifecycle.

Inspector/configuration state and published query state are separate. A parent configuration `ChangeNotify`
publishes a copy under a short lock; scalar/batch queries copy that snapshot and evaluate without holding the lock.
Hill density, amplitude, and frequency edits notify gradient dependents after publication. Tint-only changes
publish the tint value but do not emit height invalidation. `ReadInConfig` uses the same publication path.

## Checks for the next user validation session

Use the existing `TGProject/build/windows` tree; do not start a clean engine build. On a disposable level setup:

1. Compare procedural and composed scalar/batch samples with no registrations, with missing image registrations,
   and with strength-zero registrations. Include empty and mismatched spans when exercising the C++ interfaces.
2. Check edit-mode and runtime heights, then edit density/amplitude/frequency. Confirm tint-only edits retain
   the shared appearance workflow without a gradient dependency height notification.
3. Exercise registration before/after compositor activation, removal/recreation, stamp retargeting, and separate
   contexts. Inspect copied registrations through `TerrainCompositionRequestBus`.
4. Move/resize/remove/reactivate the target shape and retarget the compositor; inspect the cached bounds.
5. Try direct self-reference, a two-compositor cycle, and a cycle through an existing gradient modifier.
   Invalid chains must return zero without recursion; repairing the source must restore pass-through samples.

These are pending manual checks, not results from this implementation session. The existing test target is unchanged.

### Phase-three numerical and placement checks

Use a disposable setup after building in the existing tree. No automated harness was added. Phase six supplies deterministic
source images and the [two-stamp authoring demonstration](HeightmapStampAuthoring.md#7-deterministic-two-stamp-demonstration-opt-in).

| Check | Expected result |
| --- | --- |
| No stamps, outside footprint, missing image, zero strength, zero feather-mask weight | Exact procedural sample, no normalization round trip |
| Asymmetric 2x2 image, zero feather | Top-left at (-width/2,+depth/2), bottom-right at (+width/2,-depth/2); no repetition outside |
| 2x2 normalized values [0,1/3] in top row and [2/3,1] in bottom row | Center sample approximately 0.5; allow decoded float precision |
| Move XYZ, yaw, resize, and scale 2 | Correct landmarks; world width/depth, feather, and inset double; exponent is unchanged; Z-only edits still contribute |
| Z=20, scale=2, offset=-5, height scale=40, sample=0.25 | Target 30 m (allow actual R16 quantization if using an imported image) |
| Positive feather, exponent 1, half-band edge/corner, full interior | Weights 0.5*strength, 0.25*strength, and strength, measured after inset |
| Positive feather, exponent 2, half-band edge/corner, full interior | Weights 0.25*strength, 0.0625*strength, and strength, measured after inset |
| Relative Edge Blend on, positive feather | Relative fraction 1-mask, independent of Strength/exponent; pure Replace in full interior |
| Relative Edge Blend off or zero feather | Previous pure Replace heights, including the original arithmetic |
| Positive inset, strip interior and inner boundary; inset covers footprint | Exact underlying composition; full-footprint inset omitted from snapshot |
| Zero feather; 1x1, 1xN, Nx1; image boundaries | No division/underflow/out-of-range read |
| Invalid dimensions/strength/feather/exponent/inset, NaN/Inf, pitch/roll including parents, unsupported scale | Warning, no contribution, previous footprint restored; correction recovers |
| Shared image reload, loss, replacement while loading, deactivation while loading | All current subscribers update; stale callbacks do not revive contributions |
| Source/region retargeting and region shape edits | Retained stamp list uses new cached elevation mapping; unusable mapping preserves base |
| Scalar versus batch, same state | Matching composition; empty/mismatched spans retain the documented contract |
| Priority reversal, distinct stable keys, reversed activation order | Sorting does not depend on registration/map/entity order |

For a deterministic two-stamp depression check, use a temporary Constant Gradient as the compositor's base:
`baseGradient = (10 - regionMinZ) / (regionMaxZ - regionMinZ)`. With [-1024,+1024], this is 0.5048828125.
Create two independent stamp entities (normal editor duplication now resolves distinct keys), each with a ready height image,
world Z=0, scale=1, height scale=0, strength=0.5, feather=0, and overlapping footprints. Set A's offset=30,
priority=0, and B's offset=-10, priority=1. Inside both footprints the accumulated result is 10 -> 20 -> **5 m**.
Reverse their priorities and expect **15 m**. Set both strengths to zero and expect the original 10 m base.
This setup is a pending user check; do not replace the saved DefaultLevel procedural source to run it.
