# Terrain control-thread storage

Committed sectors retain immutable `TerrainPreparationDependencySet` metadata:
the original dependency tickets and a sorted, unique list of their mutex owners.
Acceptance shares a set only when all ticket identities and revisions match.
Different dependency sets remain separate, so a source change cannot invalidate
an unrelated sector through a broadened dependency union.

`r_terrainReuseControlStorage` defaults to `true`. Coverage retains unique
publication/snapshot pairs and immutable dependency sets. Every refresh checks
publication activity and identity under the publication mutex and observes every
exact dependency ticket through atomic revision/activity reads. If the metadata
remains current, the manager skips the repeated committed-sector walk. A changed
publication or dependency triggers per-sector refresh, preserving unrelated
coverage. No camera or frame counter can postpone these checks.

Dependency invalidation advances revisions immediately under the existing mutex;
retirement is permanent. Commits, hiding and grid clearing discard validation
metadata before committed ownership changes. The cache owns no sector pointers
or prepared worker results. Authoritative empty claims retain their authorities.

Final acceptance still uses publication-then-sorted-dependency locks through GPU
publication. Admission retains its metadata until the locks are released, and
its scratch retains capacity only. Nested admissions use private lock storage;
callers must still avoid reacquiring a dependency held by the same thread.

Setting the switch to `false` reconstructs and sorts temporary dependency lock
lists for each visibility check. It compares fresh visibility observations with
locked revalidation; it does not revert immutable committed metadata or the flat
coverage-selection algorithm.

Coverage selection builds sorted flat levels from children upward, retaining
per-level capacity instead of allocating map nodes. Signed parent division,
last-wins duplicate handling, authoritative holes, traversal order and quadrant
masks match the original projection. Claims, sector pointers, selected draws and
the next RT list also reuse capacity. RT lists swap buffers and clear stale
pointer entries. A separate dirty flag distinguishes an empty completed draw
list from one that needs rebuilding.

Metadata is prepared before the final publication/dependency locks are acquired.
It grants no admission and never refreshes a captured revision. The existing
final acceptance validates all request tickets before consuming any destination
lease or publishing resources. Resource accounting counts shared committed
metadata once. It remains partial accounting, not a measure of driver memory.

## Timing scopes

`TerrainFeatureProcessor::ProcessSurfaces` now separates mesh updates, macro and
detail material updates, clipmap updates, drawing, material compilation, and
terrain SRG compilation/binding. Drawing separates sector SRG compilation and
draw-packet submission from dispatch and candidate selection. Deferred capture
separates request capture, source acquisition, area queries and eligibility
assessment.

All scopes measure elapsed wall time, including scheduling delays. Nested scope
durations overlap. TG's flight analyzer correlates contained scopes on the same
thread and merges their intervals before reporting unscoped parent time.
Preparation-worker totals must not be added to control-thread durations.

TG's flight harness accepts `trace_sector_timing: false` to disable console
diagnostics during CPU capture while retaining a separate timing-only route.
This distinction matters because request-state logging occurs during capture
and publication. `trace_kind` can select translation, reversal or teleport;
the historical `cpu_translation.json` output name is retained for compatibility.
The analyzer preserves profiler data-loss warnings; missing expensive parent
scopes in a truncated recovery trace cannot establish a low control-thread peak.

## Validation and scope

The coverage-selection follow-up passed 417 runtime and 46 editor tests,
including warmed-cache source/publication invalidation, immediate raster/RT
withdrawal without a frame advance, independent authorities and retained lifetime.
Differential tests compare flat projection with the original maps over 300 changing
layouts; independent cell-ownership and hardware normal/cutout tests also pass.
All 15 offline profiling tests and maintained override hash-contract tests passed.

Selection scratch retains high-water capacity; whole-renderer allocation counts
and memory peaks were not measured. Existing resource accounting remains partial.
This change leaves sampling, upload budgets, group acceptance and numerical
policies intact. TG's `Gem/Docs/TerrainCoverageSelectionProfiling.md` records the
matched camera, teleport, raster/RT and CPU measurements. The original storage
pass remains documented in `Gem/Docs/TerrainControlStorageProfiling.md`.
