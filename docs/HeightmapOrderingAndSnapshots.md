# Heightmap stamps: persistent ordering and query ownership (phase four)

Implements [issue #8](https://github.com/brooteskan/TG/issues/8), following phases one through three.
This document is the ordering/lifecycle contract, not a report of completed runtime acceptance checks.
The user reported a successful build after the explicit address-reset construction fix.
Automated tests were not run; debugger checks and visual acceptance remain unverified.
The existing test target and DefaultLevel content are unchanged by this work.

## Persistent identity

The blend comparison is ascending signed priority, then lexicographic **unsigned-byte** comparison of
the entire stable key. There is no numeric interpretation of aliases, locale comparison, case folding,
registration-order tie-break, entity-ID tie-break, or "newest wins" rule. Higher/later records blend into
the accumulated result using the existing feathered Replace implementation.

### Authoring key format

The version-one prefab key is:

```text
prefab-v1: (i<byte-length>:<instance-alias>)* e<byte-length>:<entity-alias>
```

Spaces and parentheses above describe the grammar; they are not written into a key. Decimal lengths
are positive, contain no leading zero, and count bytes. Length prefixes avoid delimiter ambiguity.
For a stamp named by entity alias `Entity_A` inside instance alias `Instance_X`, the key is:

```text
prefab-v1:i10:Instance_Xe8:Entity_A
```

The ancestor instance aliases are ordered from the level root toward the stamp. The level root's own
alias is excluded, including the conversion document's `DummyAlias`. A container entity uses its actual
`ContainerEntity` alias. Entity names, filesystem paths, context IDs, activation tokens, and runtime IDs
are not key material. Identity is scoped to a composition and entity context; keys need not be globally
unique across levels or independent compositions.

The installed O3DE prefab duplicator assigns a new entity alias to an entity copy and a new nested-instance
alias to a prefab copy. Inner entity aliases may be shared across prefab instances: their ancestor instance
segments distinguish them. Undo/redo restores saved aliases and therefore restores the corresponding key.
Transform edits and display-name changes do not change identity. Moving an entity across prefab ownership,
creating/detaching a prefab, or otherwise changing its saved alias path may change order; undo restores it.

The Tools/Builders identity service resolves from the live level root for dedicated editor components and legacy generic wrappers.
It refreshes after root loading, prefab propagation, and undo/redo. During propagation/undo, unresolved
stamps contribute nothing until the hierarchy is consistent; no copied or random identity is substituted.
Expected pending resolution is not logged as an identity error. Missing Tools support and malformed runtime
keys produce control-side diagnostics. No query logs diagnostics.

### Export

`HeightmapStampIdentityProcessor` walks each conversion document directly before `EditorInfoRemover`.
Conversion instances use `EntityIdInstanceRelationship::OneToMany` and are not represented by the editor's
global entity-to-instance mapper. The processor therefore uses the document hierarchy, not active editor
entity buses or runtime IDs, to resolve exactly the same relative alias key.

The processor changes only the conversion copy of the component configuration. It supports dedicated
`EditorHeightmapStampComponent`, legacy `GenericComponentWrapper`, and runtime stamps, writes `StableOrderKey`, and lets normal component conversion and entity-reference
remapping continue. It runs in both `PlayInEditor` and `GameObjectCreation`, inserted with `$stack_before`
in the project-local `Registry/heightmap_stamps.tools.setreg`. Missing/unresolvable or conflicting export
identities mark the conversion context failed; no random fallback is permitted.

The editor module includes the runtime descriptors once, the two dedicated editor descriptors, and the identity service/processor.
Tools and Builders load it in place of the runtime module. Clients/Servers do not depend on AzToolsFramework.
Phase six preserves the same conversion order/key encoding and bumps the processor's reflected version to 2 for its expanded
component support. Dedicated BuildGameEntity copies the already baked key without consulting the live editor mapper.
See the [authoring, sample import, and acceptance guide](HeightmapStampAuthoring.md).

### Legacy and programmatic configuration

`HeightmapStampConfig` version two adds `StableOrderKey` and retains `OrderingId` unchanged on deserialization.
Version three adds `FeatherExponent` and `EdgeInset`, defaulting to 1 and 0 when absent; it leaves both identity fields unchanged.
Version four adds `RelativeEdgeBlend`, defaulting to true even for older data. It changes only positive-feather edge blending;
disable it to recover the old edge heights. Stable keys, legacy UUIDs, full interiors, and zero-feather blending remain unchanged.
If a runtime config has no stable key but has a nonnull legacy UUID, its effective key is:

```text
uuid-v1:<32 uppercase hexadecimal UUID digits, no braces or dashes>
```

For a set of legacy UUID records this preserves their original byte ordering. A nonempty malformed key
is rejected rather than falling back to its UUID. A default config no longer generates a UUID merely by
construction/deserialization. Null UUID plus empty key is unresolved and cannot contribute.

Editor registration and export always derive the alias key, ignoring copied legacy/baked keys. They do
not dirty the saved template to cache it. **The first adoption of alias ordering may change an existing
equal-priority overlap compared with phases one through three.** Subsequent reopen/export/activation use
the deterministic alias contract. No level rewrite or opportunistic UUID repair is performed.

For a programmatic stamp or clone, assign its identity before activation/registration and save that config:

```cpp
auto cloneConfig = original->GetStampConfiguration();
cloneConfig.AssignNewPersistentOrderingIdentity();
// Set/remap cloneConfig.m_targetCompositionEntityId as appropriate for this context.
// Construct the inactive clone with cloneConfig, persist it, then activate it.
```

Regular updates use a copy of the current configuration and preserve the key. Repeatedly instantiating
the same baked prefab into one composition also requires distinct persistent keys before activation;
blindly cloning a baked runtime configuration is not an identity allocation API. Such collisions fail
closed. For a mixture of key kinds, the literal format prefix participates in the byte comparison.

## Registration and collision policy

The compositor owns a map of current registration **claims**, not a process-wide stamp list. Claims are
addressed by `(entity context UUID, composition entity ID)` and carry a composition-session token, a
stamp-registration token, and a monotonically increasing update revision. These ephemeral tokens are
never ordering keys and are never exported.

`RegisterStamp` accepts current-context/current-session claims. True means the claim is stored, not that
it contributes. Invalid placement, pending identity, unavailable pixels, zero strength, and identity
collisions are excluded from the published contributing list. Identity claims remain tracked even when
their image is unavailable, strength is zero, or placement is invalid.

All claimants of a duplicate stable key are suppressed, including any claimant which had previously
contributed. No "first registration wins" behavior remains. Diagnostics identify the key, composition,
context, and all conflicting stamp IDs; they are emitted when the claimant group changes. Removing or
repairing a claimant reevaluates the remaining group. A sole valid remaining claimant contributes again.
Different priorities do not excuse duplicate identities.

The same lease/revision is an idempotent replay. Older revisions and mismatched leases are rejected.
Unregistration requires matching stamp, registration token, and composition session. Removed lease tokens
are retained as tombstones until the composition session ends, preventing delayed registration replay.
Direct bus clients must supply these tokens and revisions; prefer `HeightmapStampRegistration` to manage them.

Stamps subscribe before registration and replay current data when a composition becomes available.
Availability notifications carry a session token; an old unavailability event cannot clear a newer session.
Late context ownership and cache restarts are retried on SystemTick using current data. Target/context
changes remove the old registration before establishing a new lease. Composition restarts create a new
session and reject all operations carrying its predecessor's token.

## Query ownership and publication

Each published `QueryState` owns:

- Its composition address, session, revision, source reference, and region reference.
- Region bounds and a validated, cached minimum-Z/elevation-range mapping.
- A sorted array of prepared stamp records, including stable keys and diagnostic stamp IDs.
- Validated placement inverses, conservative XY bounds, numeric blend parameters, and shared immutable
  image pointers with revisions.

The writer builds the entire replacement before exchanging
`std::atomic<std::shared_ptr<const QueryState>>`. Source/region changes do not publish intermediate new
references with old or temporarily cleared bounds. Atomic shared ownership is not claimed to be lock-free;
there is no application update mutex held by queries or around calls to other buses.

Scalar queries acquire one state. A batch acquires one state and performs one base-gradient `GetValues`
request. Both use the same phase-three composition helper and retain their acquired state until return.
The procedural base keeps its own existing per-call configuration capture; this does not introduce a global
transaction spanning independent gradient providers.

Workers do not read editable configs, enumerate mutable registrations, sort records, resolve prefab aliases,
load assets, inspect raw asset spans, or send notifications. XY bounds rejection still precedes pixel sampling.
No-contribution samples return the original procedural value directly; the hill math and tint path are unchanged.

Image readiness/reload/failure publications carry a canonical asset ID even when no pixels are ready.
When a revision reaches a composition, all its claims using that canonical source are updated before one
replacement is published. A lagging subscriber's transform/configuration update cannot roll the group back
to an older revision. Old snapshots retain their old immutable pixels until their last reader releases them.

## Threading, shutdown, and notifications

Scene control operations run on the entity/main thread. Activation establishes control-thread ownership
after asynchronous deserialization; off-thread setters/registrations are asserted and rejected. Inactive
conversion-only components can be configured on their builder/construction thread. Image/cache control
access is also confined to its owner thread. Asset/catalog callbacks enqueue owned data through weak source
ownership; source/stamp generation, session, asset ID, and revision checks reject obsolete work.

Deactivation closes the registration gate, disconnects requests and gradient dispatch, and only then clears
component-owned records. O3DE's shared-dispatch gradient disconnect waits for in-flight dispatches. It must
not run from a gradient handler or while holding an update lock. Waiting for a deliberately paused debugger
query to resume is expected draining behavior, not permission to free its component early.

Stamp shutdown invalidates asset callbacks, disconnects subscriptions, and unregisters its lease before
releasing ownership. The cache rechecks catalog state when consuming queued availability/removal events,
so obsolete catalog events cannot restart/remove a product contrary to current catalog state.

`OnStampFootprintChanged` carries old/new XY bounds, the corresponding region IDs/AABBs, and captured composition
address, session, and snapshot revision. Phase five keeps immediate publication but **defers outbound notifications
to regular TickBus at `TICK_DEFAULT - 1`**, outside the registration EBus's implicit recursive dispatch mutex.
SystemTick still services context/asset lifecycle work; it is not a once-per-frame boundary. Pending movement/removal
coverage survives subsequent edits, including intermediate drag positions and source/region changes.

Shutdown queues value-only removal metadata and whole-region cleanup, including the departing composition's base.
It does not capture the component. Ended-session cleanup can invalidate old terrain but cannot recreate registrations;
dispatch checks that a surviving entity ID has not acquired a different owning context. Retired source subscriptions
have independent weak-owned mailboxes, so their queued events cannot affect a new source/session. A late source cycle
causes one whole-region transition to fallback, then invalid links stop forwarding to prevent deferred feedback loops.

The compositor emits bounded `OnCompositionRegionChanged` notifications after publication. Phase-five expansion,
clipping, coalescing, engine limitations, and pending user checks are documented in [TerrainInvalidation.md](TerrainInvalidation.md).
No gradient/asset/terrain call runs under a publication or source-mailbox lock. Scalar/batch query ownership and
persistent ordering are unchanged. Phase-five changes have not been built or visually validated.

## User-run acceptance workflow

Build only in the existing `TGProject/build/windows` tree. The editor module and conversion processor must
be included in that build; do not use a clean engine build. Use a disposable setup or your existing level
without committing validation edits. No new automated identity/concurrency harness is supplied.

1. **Ordering and persistence:** make two equal-priority overlapping stamps and inspect their effective keys
   through `GetRegisteredStamps`. Record heights, save/reopen, and reverse activation order. Keys/order/heights
   must match. Entity display-name and transform edits must not change the key.
2. **Duplication:** duplicate an entity, a prefab containing stamps, and a nested prefab instance. Check that
   all copies have distinct qualified keys. Save/reopen and undo/redo duplication; surviving/restored aliases
   must reproduce their expected order. Do not expect duplicate creation order to define blend order.
3. **Export:** inspect `HeightmapStampIdentityProcessor::Process` in both PlayInEditor and GameObjectCreation.
   Confirm it precedes EditorInfoRemover, sees every stamp (including wrappers), and produces the exact editor
   key without the root/DummyAlias. Inspect baked runtime config and remapped target references. Runtime entity
   IDs/context differ, but ordering and scalar/batch heights must agree with edit mode.
4. **Programmatic identity:** construct an inactive clone using a configuration whose new persistent key was
   assigned before activation. Reload it from saved configuration and verify that key is unchanged.
5. **Collision:** deliberately submit two runtime configs with the same key using the main-thread setter.
   Both must cease contributing irrespective of activation order, priority, or asset readiness. Verify the
   group diagnostic. Repair/remove either claimant and verify deterministic recovery of the other.
6. **Batch ownership:** break after `GetValues` captures its state. Allow the main thread to publish a transform,
   parameter, region, or asset change, then resume the worker. Its state revision and pixels must stay unchanged
   throughout that batch. A subsequent query acquires the replacement; no partially updated placement is visible.
7. **Shared asset revision:** reload or lose one image shared by several stamps. Inspect the next snapshot:
   every dependent claim uses that publication/fallback, while an already captured old state remains readable.
   Switch images repeatedly while old callbacks are pending; obsolete callbacks must not overwrite the selection.
8. **Lifecycle:** repeat disable/enable, composition removal/recreation, retargeting, context transitions, and
   Play/Stop while loads are pending. Verify one current lease per stamp, no stale resurrection, no old-session
   unregister clearing a replacement, and removal metadata for previous footprints.
9. **Numerical regression:** compare unchanged-state scalar/batch samples, empty batches, no stamps, missing
   images, zero strength, and invalid mappings. Preserve exact procedural fallback. The existing 10 m base,
   A=30 m at 0.5 then B=-10 m at 0.5 example still yields 5 m (15 m in reverse order).
10. **Notification boundary:** inspect publication before notifications and confirm no control-bus dispatch
    mutex is held when downstream dependency notifications are sent. Moving then removing a stamp before
    the flush must retain both its previous footprint and removal information.

These checks remain pending until the user performs them. The dedicated editor components, outlines, deterministic
demonstration, and integrated user acceptance workflow are described in [HeightmapStampAuthoring.md](HeightmapStampAuthoring.md).
