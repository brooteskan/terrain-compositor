# Terrain tick subscriptions

Terrain Compositor uses ticks only for bounded resolution or work whose ordering
is explicitly tied to the regular terrain frame.

| Owner | Bus | Connect trigger | Disconnect condition | Purpose |
| --- | --- | --- | --- | --- |
| Registration client | `SystemTickBus` | Entity context is unavailable | Context resolves, deactivation, or eight attempts expire | Resolve late context ownership. Healthy asset subscriptions recover through cache lifecycle events. |
| Height/surface provider lifecycle | `SystemTickBus` | Region context is unavailable | Context resolves, stop, or eight attempts expire | Resolve late context ownership. |
| Terrain composition | `SystemTickBus` | Composition owner context is unavailable | Context resolves, deactivation, or eight attempts expire | Resolve late context ownership. Gap admission arrives through render-channel events. |
| Procedural ground material binding | `SystemTickBus` | RPI, scene, or render registry is not ready | Binding succeeds, stop, or eight attempts expire | Establish the scene material event subscription. Material replacement is then event-driven. |
| Terrain quality readiness | `TickBus`, `TICK_DEFAULT - 2` | A valid quality claim is waiting for terrain/renderer readiness or settings readback | Readiness resolves, the claim stops/conflicts, or eight attempts expire | Bounded fallback for readiness gaps without a reliable notification. Terrain creation, settings, renderer entity, and scene/subsystem events restart the budget. |
| Mesh placement lifecycle | `SystemTickBus` | Placement or visibility binding requests a retry | Placement and visibility independently succeed, the component stops, or each eight-attempt budget expires | Bounded compatibility retry for late hierarchy, transform, and mesh readiness. Hierarchy/model events restart the relevant work. |
| Editor stamp identity system | `SystemTickBus` | A prefab or undo/redo transition requests a refresh | The pending refresh is delivered or the system stops | Deliver one post-propagation identity refresh. |
| Terrain composition invalidation | `TickBus`, `TICK_DEFAULT - 1` | Source, gap, configuration, publication, or terrain settings/lifecycle changes create work | The scheduled pass drains actionable work; unavailable-spacing work waits disconnected for terrain readiness | Coalesce dirty regions and deliver them before the terrain system consumes its regular tick. Reentrant work schedules a later ordered pass. |
| Selected editor preview | `TickBus` | Its entity becomes selected | Entity is deselected or preview deactivates | Refresh visible inspector status. Only selected previews connect, so handler count does not scale with authored stamps. |

`SystemTickBus::QueueFunction` remains in asset/catalog callbacks, model
preparation completion, composition source/gap wakeups, mesh placement notification delivery,
and material replacement delivery. Those calls marshal owned work to the control thread;
they do not create persistent handlers. Every queued callback carries either a
weak owner or a generation/session check so retired work cannot publish.

Entity-context removal notifications arrive before O3DE disconnects the
entity's context query handler. Consumers clear the observed context from the
notification and start bounded resolution on a later system tick instead of
re-querying synchronously from the removal callback.
