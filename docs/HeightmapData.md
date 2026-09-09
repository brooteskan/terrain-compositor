# Heightmap image data: phase two

Implements [issue #6](https://github.com/brooteskan/TG/issues/6). This supplies the shared CPU image data
used by the phase-three [stamp component and sampler](TerrainComposition.md). The cache itself does not
perform placement or blending. No DefaultLevel rewiring is part of the cache or phase-three implementation.

## Supported import route

1. Export a **single-channel, unsigned 16-bit grayscale TIFF**, with power-of-two width and height
   (for example, 1024 x 1024). Rectangular power-of-two images are supported. Do not export RGB,
   signed integers, an 8-bit image, or a display-gamma-adjusted preview.
2. In Asset Browser, right-click the source TIFF (not its `.streamingimage` child) and choose
   **Edit Texture Settings...**. Set **Preset** to **GSI16**. It specifies **Linear**
   source and destination color spaces, **R16** output, and power-of-two dimensions. The installed
   engine's `Gems/Atom/Asset/ImageProcessingAtom/Assets/Config/GSI16.preset` and
   `Code/Source/ImageLoader/TIFFLoader.cpp` provide this route; no engine edits are needed.
3. Set **Res Limit to 0** for the target platform and enable **Use Max Res**. The editor calls
   resolution reduction "Res Limit". Confirm **Format** is **R16** and **Max Res** matches the source,
   then save. Ensure image-tag quality settings do
   not drop front mip chains, and platform maximum texture dimensions do not resize the source.
   Mips may be generated, but this cache always uses **mip 0**; it never substitutes a lower mip.
4. Let Asset Processor finish and select the resulting **StreamingImage** product in the stamp
   configuration. Its runtime format must be **R16_UNORM**, uncompressed, 2D, with one array slice.
5. Preserve the source TIFF and its image processing settings together. Phase six supplies deterministic sources/settings
   in `Assets/HeightmapStamps`; follow the [authoring guide](HeightmapStampAuthoring.md). No Asset Processor import or
   processed-product verification was performed here; those checks remain user-owned.

Source value 0 becomes 0.0, 65535 becomes 1.0, and intermediate values are divided by 65535 using
O3DE's existing CPU pixel decoder. The cache does not apply sRGB conversion, automatic min/max
normalization, inversion, placement, or height scaling. Float samples retain the source's 16-bit
precision. Rows retain image order; the phase-three placement/sampling helper owns the image-to-local-axis
mapping. An all-zero **ready** image is valid height data; an absent image has a **null data pointer**.

The cache validates the imported product's format, dimensions, mip topology, layout, and byte count.
It cannot recover the source TIFF's precision/color-space history or original dimensions from the
runtime product. **The user must verify the actual import**: confirm no prior 8-bit conversion,
gamma adjustment, resizing, or reduction occurred. R16 output alone cannot establish those facts.
Other formats, including R8, RGB(A), floating point, signed, and block-compressed images, are rejected
in this initial contract, even where the engine can decode them.

## Ownership and lifecycle

`TerrainCompositorSystemComponent` owns a `HeightmapDataCache`, available through
`HeightmapDataCacheInterface`, for its active lifetime. System activation precedes stamp activation.
The cache shares sources by canonical asset ID, keeps only weak source references, and has **no
stamp list**. Each `HeightmapStampRegistration` holds a source subscription independently of its
composition target. Transforms, dimensions, scale, strength, feather width, and ordering remain
per-stamp values and neither duplicate the CPU samples nor restart loading.

The image is queued with `AssetManager`, and the mip-zero dependency is queued separately when
it is not embedded in the image's tail. No `BlockUntilLoadComplete`, file reads, or decoding occurs
in a height query. In particular, the cache never calls `StreamingImageAsset::GetSubImageData`,
which can initiate and block on a mip load. It uses only the non-loading
`ImageMipChainAsset::GetSubImageData` accessor after the required assets are ready.

Asset callbacks enqueue owned payloads through `SystemTickBus`, including callbacks delivered
inline while connecting to `AssetBus`. On the main thread, the cache validates and decodes the
ready mip into a new buffer, then signals its subscribers. Decoding is CPU work on the main thread,
outside terrain queries; large-library streaming and background decode scheduling are not part of
this phase. Row pitch is respected and aligned row copies are passed to the engine pixel helper.

`HeightmapStampRegistrationData::m_heightmap` contains a copied status/revision snapshot and a
`shared_ptr<const HeightmapData>`. Each publication has a monotonically increasing process-local
revision, including after source eviction/reacquisition. Identity plus revision identifies a
published buffer; revisions are not serialized product hashes. All subscribers to a source receive
the **same data pointer**. Reloads allocate a new buffer; already-published buffers are never edited.
Image and mip asset handles remain owned during preparation, and copied samples survive the release
of all asset handles and registrations for as long as any reader retains their shared pointer.

The sampler retains that pointer in the compositor's immutable prepared-stamp array, skips null data, and reads
`m_samples[y * m_width + x]` only at valid coordinates. It does not call `Acquire`, `GetSnapshot`,
asset APIs, or registration buses from a terrain worker. Scalar and batched gradient queries use the same
bilinear sampling and accumulated Replace helper; see [the composition contract](TerrainComposition.md).

## Reload, failure, and notification behavior

- Unassigned, loading, missing, failed, removed, and unsupported images publish **no sample data**.
  No valid zero-height image is fabricated, and a failed reload does not leave old heights in new
  registrations. Queries already holding an older immutable snapshot can safely finish.
- The catalog is checked before loading to avoid O3DE's missing-image color fallback. Missing
  products remain subscribed for catalog discovery; removing the image or required mip invalidates
  every dependent registration. Adding/reprocessing a missing or failed product retries loading.
- The cache listens for image and required-mip readiness/reloads. Since `ImageMipChainAsset`
  opts out of automatic reload, the cache explicitly refreshes resident mip data for a new parent
  revision and for standalone mip changes. An older in-flight mip load is allowed to finish, then
  refreshed asynchronously before its data can be published for a new parent.
- Image and mip subscription generations reject queued callbacks from superseded requests.
  Asset creation tokens reject duplicate/stale ready revisions. Parent reload/removal invalidates
  the previous mip subscription. Queued work holds weak source references, not stamp pointers.
- Memory unload is not catalog removal. O3DE queues `OnAssetUnloaded` by asset ID after destroying
  an instance; it can reach a newly connected watcher after editor/Play reactivation. The cache ignores
  this notification because it retains strong handles to its current image/mip instances. Otherwise,
  unloading an old instance could clear a newly loaded heightmap and leave it Missing until reactivation.
  Actual product deletion still invalidates through `OnCatalogAssetRemoved`; load/reload errors and
  cancellation retain their failure handling.
- Asset selection changes and stamp deactivation disconnect the stamp's event handler and advance
  its selection generation. Late callbacks cannot update another selection or reactivate a stamp.
  Cache shutdown clears subscribers' data and disconnects all asset/catalog handlers.
- Each source emits diagnostics only when its failure state changes, including the asset ID/path,
  the failing stage, and corrective import settings. Multiple stamps share that diagnostic state.
  An unassigned asset does not warn. No diagnostic is emitted while reading sample data.
- Every readiness/revision change replaces each subscriber's composition registration, using the
  `RegisterStamp` -> immutable publication -> deferred regional dependency notification path. The
  compositor queues the old/new footprints of every affected claim, then coalesces notifications per frame.
  Readiness/reload/failure do not require a global height refresh or a new cache/subscriber registry.
  Compositions activating later receive the latest snapshot when the stamp replays registration.

## Pending user validation

Build using the existing `TGProject/build/windows` tree only. The user subsequently reported a
successful compile and configured `Assets/height.tiff` with GSI16, Res Limit 0, and Use Max Res;
the source and its saved `.assetinfo` settings are included. Automated tests and visual validation
were not run by the implementation agent, and the existing test target is unchanged. The shared
cache still requires runtime validation through the phase-three stamp component. Before accepting the import
and composed results, manually check:

| Scenario | Expected observation |
| --- | --- |
| 16-bit ramp with nearby values (e.g. 32767, 32768), GSI16 | Correct dimensions, R16_UNORM, distinct values divided by 65535; no min/max stretch |
| Small image with mip 0 embedded, then a larger image with an external mip-zero chain | Both become Ready only with a validated full-resolution buffer |
| Two stamps referencing one image | Identical `m_heightmap.m_data.get()`; transforms/strength edits preserve that pointer |
| Missing, loading, failed, removed, unsupported, or unassigned image | Null sample data; procedural pass-through remains unchanged |
| Reload image or its mip-zero product | All subscribers get new data/revision; a retained old snapshot remains readable and unchanged |
| Replace A with B (or A -> B -> A) while loading; deactivate while loading | No stale A/B callback replaces the selected data or recreates a registration |
| Delete/recreate products, repair a failed import, remove/recreate the compositor | Data is cleared/recovered and latest registrations replay without stale heights |
| Enter/exit Play repeatedly with ready heightmaps (embedded and external mip zero); repeat through Simulate | Heightmap contributions return after asynchronous loading in each context; old memory-unload events do not mark the new source Missing |
| R8, sRGB/color, compressed, non-power-of-two, or malformed product | Rejected with actionable diagnostics, without repeated warnings from queries |

These are pending checks, not reported runtime results.

## Phase-four ownership and lifecycle integration

Snapshots now include the canonical source asset ID in all readiness states. The compositor updates every
dependent claim to a shared-image publication before exchanging a complete immutable query snapshot, and
rejects attempts to regress that group to an older image revision. In-flight readers still own their old pixels.

Cache control access is restricted to its owner thread. Asset event payloads retain weak source ownership
and load generations; queued catalog removal/availability events are reconciled against current catalog state.
Stamp registration adds composition-session and registration-lease checks, including stale-unregister rejection
and retry after late context ownership or cache restart. See
[HeightmapOrderingAndSnapshots.md](HeightmapOrderingAndSnapshots.md) for the full contract and pending
user-run reload/shutdown/batch checks. These additions do not change the import or sampling route.
