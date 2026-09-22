# Terrain tick subscriptions

Terrain Compositor uses ticks only for bounded resolution or work whose ordering
is explicitly tied to the regular terrain frame.

| Owner | Bus | Connect trigger | Disconnect condition | Purpose |
| --- | --- | --- | --- | --- |
| Registration client | `SystemTickBus` | Entity context is unavailable | Context resolves, deactivation, or eight attempts expire | Resolve late context ownership. Healthy asset subscriptions recover through cache lifecycle events. |
| Height/surface provider lifecycle | `SystemTickBus` | Region context is unavailable | Context resolves, stop, or eight attempts expire | Resolve late context ownership. |
| Terrain composition | `SystemTickBus` | Composition owner context is unavailable | Context resolves, deactivation, or eight attempts expire | Resolve late context ownership. Gap admission arrives through render-channel events. |
| Procedural ground material binding | `SystemTickBus` | RPI, scene, or render registry is not ready | Binding succeeds, stop, or eight attempts expire | Establish the scene material event subscription. Material replacement is then event-driven. |
| Mesh placement lifecycle | `SystemTickBus` | Placement or visibility binding requests a retry | Binding resolves, component stops, or eight attempts expire | Bounded compatibility retry for mesh readiness and placement. |
| Editor stamp identity system | `SystemTickBus` | A prefab or undo/redo transition requests a refresh | The pending refresh is delivered or the system stops | Deliver one post-propagation identity refresh. |
| Terrain composition invalidation | `TickBus`, `TICK_DEFAULT - 1` | Composition is active | Component deactivation | Coalesce dirty regions and deliver them before the terrain system consumes its regular tick. |
| Selected editor preview | `TickBus` | Its entity becomes selected | Entity is deselected or preview deactivates | Refresh visible inspector status. Only selected previews connect, so handler count does not scale with authored stamps. |

`SystemTickBus::QueueFunction` remains in asset/catalog callbacks, model
preparation completion, mesh placement notification delivery, and material
replacement delivery. Those calls marshal owned work to the control thread;
they do not create persistent handlers. Every queued callback carries either a
weak owner or a generation/session check so retired work cannot publish.

Entity-context removal notifications arrive before O3DE disconnects the
entity's context query handler. Consumers clear the observed context from the
notification and start bounded resolution on a later system tick instead of
re-querying synchronously from the removal callback.
