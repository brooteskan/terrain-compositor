# Heightmap stamps: granular terrain invalidation (phase five)

Implements [issue #9](https://github.com/brooteskan/TG/issues/9) on top of the phase-four immutable query snapshots.
This is the implementation contract and a pending user-run checklist, not a report of runtime acceptance.
No engine, shader, material, level, transform, import-setting, or test-target changes are required.
Builds, automated tests, and visual/debugger validation have not been run for this phase.

## Publication and coverage

Registration, transform, parameter, and shared-image updates still publish a complete immutable `QueryState`
immediately. Scalar queries and entire batches retain their acquired snapshot and pixel ownership. Invalidation
never adds work to the height-query path. The writer captures previous/current footprints from those snapshots,
then accumulates notifications after publication. Removing a contribution queues its previous footprint before
discarding the old snapshot. Later invalid-to-invalid edits do not repeatedly dirty that removed footprint.

`HeightmapStampFootprintChange` now carries the previous/current region entity IDs and AABBs as well as the
previous/current XY footprint, composition address/session, and snapshot revision. These are runtime records,
not new serialized properties. Deferred removal and retargeting never resolve old bounds against a new region.

| Change | Coverage |
| --- | --- |
| Stamp activation, enable, image readiness, recovery | Current contributing footprint |
| XYZ movement, yaw, positive uniform scale, footprint width/depth | Previous and current full footprints |
| Height scale, offset, strength, feather width/exponent, edge inset, relative-edge toggle | Full affected footprint; old coverage if contribution disappears |
| Priority or ordering identity | Full affected footprint, including other claimants suppressed/recovered by a key collision |
| Image reload/failure/removal | Every affected claim's previous/current footprint |
| Disable, deactivate, remove, invalid configuration/transform | Previous contributing footprint |
| Change target composition | Removal at old address; addition at new address, with their own region context |
| Procedural height settings, source references | Whole affected region |
| Region shape, elevation range, activation/deactivation, retarget | Captured old/new whole-region bounds |
| Composition activation/removal | Whole region, because the procedural base also appears/disappears |
| Tint only | No compositor height invalidation |

The compositor continues to compare contributor sets, not just the directly edited stamp. Identity collisions
and their recovery therefore invalidate all affected participants. Height/priority/image edits cannot be skipped
merely because the XY bounds are unchanged. Zero strength and unavailable/invalid images are not contributors.

## Bounds construction

`TerrainInvalidation` is a project-local, value-owned accumulator and pure geometry helper. Each entry captures
its region ID and exact bounds. Entries from different regions or shape revisions are not merged together.

For footprint work:

1. Reuse `PreparedHeightmapStamp::m_worldBounds`, already conservatively rounded from the yaw/scaled corner extrema.
2. Expand XY by `2 * GetTerrainHeightQueryResolution()` on each side.
3. Replace Z with the captured region's full vertical range; ignore stamp-origin Z and sample Z for overlap.
4. Clip to the captured region bounds, rounding float conversions outward.
5. Discard empty/outside results. Reject invalid/nonfinite inputs.

Expansion happens before clipping. Double intermediates avoid overflow for extreme finite placement and spacing.
The two-spacing margin covers the installed terrain system's bilinear height/normal sampling neighborhood;
`TerrainSystem::GetNormalsSynchronousBilinear` uses a 12-point stencil extending to neighboring grid nodes.
`TerrainMeshManager::ForOverlappingSectors` independently expands for its LOD and next-LOD normal dependencies.
No duplicate renderer-specific LOD logic is added to the project.

The spacing query is a settings-only TerrainDataRequestBus call on the main thread outside locks. It never asks
the final terrain provider for heights. If spacing is missing, nonpositive, or nonfinite, footprint work stays
pending until a later regular tick. Whole-region work does not require spacing. Invalid region bounds cannot
produce a notification; a later valid region publication queues its whole new bounds. Old valid bounds remain
in pending records even after the current region becomes unavailable.

Only finite valid AABBs reach `OnCompositionRegionChanged`. There is no null-AABB fallback: the installed terrain
height list interprets null bounds as `RefreshArea`, which would defeat granular invalidation.

## Frame coalescing

SystemTick continues to reconcile entity context and process asset lifecycle work. It can fire more than once
per frame in tools, so normal outbound footprint/dependency notifications now flush from `AZ::TickBus` at
`AZ::TICK_DEFAULT - 1`, before the installed terrain system's default-order tick. Edits after that flush wait
until the next regular tick. When tools pause regular ticking while unfocused, pending work remains queued.

The accumulator removes contained/redundant regions and merges overlapping/touching rectangles when the merged
rectangle adds at most 10% area over their combined covered area for that merge. Disjoint rectangles stay separate.
Whole-region changes subsume footprint entries for the same captured region. There is no count-based promotion
of ordinary stamp edits to global/whole-region invalidation. Padding and clipping are followed by another
coalescing pass because formerly separate footprints can have overlapping sampling margins.

Every published intermediate position in a drag remains covered: `A -> B -> C` cannot replace pending work with
only `B -> C`. Per-publication footprint metadata remains available; terrain dependency notifications are
coalesced. Pending state is detached before external dispatch, so a callback that edits, retargets, or deactivates
a component cannot invalidate the batch being traversed. Reentrant edits produce later work.

## Source dependencies and cycles

The procedural source uses `DependencyMonitor::SetEntityNotificationFunction` with a weak-owned mailbox.
Callbacks only copy notifications under a short mailbox lock; they never call query, terrain, or dependency
buses under that lock. Whole-source events subsume bounded source entries, and contained entries are deduplicated.
The main-thread frame handler drains the mailbox, preserves bounded XY work, and translates unbounded source
events into finite whole-region changes. Base settings were already published by the source before its event.

Each dependency reconnection creates a new mailbox and retires the old one. Captured weak references do not own
the component, and stale source callbacks cannot reach the new mailbox. A configuration reconnection also queues
whole-region coverage so a pending base event in the retired inbox cannot be lost.

Region subscriptions use only ShapeComponentNotificationsBus and entity lifecycle events, never the consuming
terrain region's DependencyNotificationBus. Shape changes are observed after the shape has updated its bounds.
The engine layer spawner retains responsibility for refreshing departed terrain-area extents; the height list
clips incoming notifications against its current shape.

Existing hierarchy checks also guard deferred source forwarding. A transition to an invalid/cyclic source queues
one whole-region refresh; subsequent events on that invalid link are suppressed. Repairing the link queues a
whole-region refresh as well. Deferral must not convert a cycle into events bouncing between frames.

## Shared images, shutdown, and contexts

The existing cache, generation checks, and shared-image revision fan-out are reused unchanged. The first subscriber
to publish a new canonical image revision updates every matching registration in its composition before the
replacement snapshot is exchanged. Each changed claimant then contributes dirty coverage. Other compositions
receive that revision through their own existing subscribers. Lagging callbacks cannot regress the image group.

Composition shutdown closes registration/query access, publishes an empty state, and captures removal metadata.
Because removing the entire compositor removes its base too, retained region contexts are promoted to whole-region
cleanup. This also makes shutdown independent of terrain spacing availability. A queued TickBus function owns
only copied address, metadata, and dirty AABBs; it may execute after component destruction without accessing it.
It cannot resurrect registrations. If a surviving entity ID now belongs to a different context, old-context
notifications are not delivered into that context. An already-removed entity can still notify its old consumers.

Same-context restarted sessions may receive harmless cleanup refreshes, but registrations still reject the old
session and lease. Ordinary stamp deactivation is only a footprint removal, not a whole-composition shutdown.

## Installed-engine limitation

The existing Terrain Height Gradient List forwards valid regions through DependencyMonitor to
`TerrainSystemServiceRequestBus::RefreshRegion` with `HeightData` only. No engine changes or direct calls to
private terrain/renderer interfaces are introduced.

However, the installed `TerrainSystem::RefreshRegion` calls `m_dirtyRegion.AddAabb`, and `TerrainSystem::OnTick`
broadcasts that accumulated single AABB. Two distant project notifications in one terrain tick can therefore
refresh the intervening space downstream. Keeping them separate at the compositor boundary does not guarantee
separate mesh updates. This engine behavior is explicitly outside phase five's project-local constraints.
Do not claim distant-gap avoidance at the renderer boundary or delay restoration across frames to simulate it.

## Pending user-run validation

Build only with the existing `TGProject/build/windows` tree. Do not perform a clean engine build. Use a disposable
setup or the existing correctly wired composition without committing validation edits. Dedicated editor UI and deterministic
demonstration sources are covered by the [phase-six authoring guide](HeightmapStampAuthoring.md). The editor adapters use active
runtime setters rather than controller restarts or global change notifications. No automated tests or test targets were added.

Useful debugger locations are `PublishStamps`, `QueueFootprintChange`, `TerrainInvalidation::BuildRegions`,
`DispatchChanges`, and the installed `TerrainHeightGradientListComponent::OnCompositionRegionChanged` /
`TerrainSystem::RefreshRegion`. Observe event bounds and counts, not just the resulting visible height.

| Check | Expected observation |
| --- | --- |
| Move XYZ, yaw, scale, resize | Old and new full footprints covered; Z-only edits still send valid overlapping dirty volumes |
| Stamp origin above/below the region's Z range | Dirty bounds still span the region's Z range, including depressions and clipped target heights |
| Edit scale/offset/strength/feather/exponent/inset/relative-edge toggle without moving | Full footprint refresh; zero strength or a full-footprint inset restores the underlying composition |
| Swap priority/key; create/repair/remove a duplicate-key claimant | Entire affected footprints refresh, including suppressed/recovered peers |
| Disable/remove; invalid dimensions/pitch/roll/nonuniform scale | Old contribution restored once; further invalid-to-invalid edits do not repeat its cleanup |
| Several edits before one tick, including A -> B -> C -> A | Original and intermediate regions retained; no stale trail; dependency regions coalesced |
| Widely separated old/new positions | Separate compositor/height-list notifications; document terrain system's later union |
| Entire footprint plus padding outside the region | No height dependency notification; moving out still restores the old in-region footprint |
| Footprint near a region or sector edge | Correct height/normal updates after padding and clipping; no missing edge row |
| Change terrain query spacing | Subsequent dirty footprints use current spacing; engine settings refresh remains intact |
| Shared-image readiness/reload/failure/recovery across two compositions | Every dependent stamp updates, with old shared buffers still safe for old readers |
| Retarget a stamp or composition before pending dispatch | Old/new addresses and captured region bounds remain correct |
| Region move/resize/elevation change, removal/recovery | Old/new whole-region coverage; invalid bounds never become global notifications |
| Remove/restart compositor before pending dispatch | Copied cleanup survives; no stale-session registrations, freed-pointer access, or cross-context delivery |
| Deferred source event followed by source replacement/shutdown | Retired mailbox cannot affect replacement; pending old-region changes are covered |
| Introduce a source cycle after activation, then repair it | Correct fallback/recovery; notifications settle instead of repeating every frame |
| Change density/amplitude/frequency versus tint only | Height settings refresh the affected region; tint only emits no compositor height invalidation |
| Sample inside a dependency callback | Published snapshot is already visible; notifications are outside application query/update locks |

All of these checks remain pending until performed by the user. Static code review and whitespace checks are
not substitutes for a successful build or runtime/visual acceptance.
