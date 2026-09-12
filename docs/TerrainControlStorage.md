# Terrain control-thread storage

Committed sectors retain immutable `TerrainPreparationDependencySet` metadata:
the original dependency tickets and a sorted, unique list of their mutex owners.
Acceptance shares a set only when all ticket identities and revisions match.
Different dependency sets remain separate, so a source change cannot invalidate
an unrelated sector through a broadened dependency union.

`r_terrainReuseControlStorage` defaults to `true`. Each coverage admission reuses
the metadata's lock order and the manager's temporary lock-vector capacity.
It still acquires the publication lock, acquires every dependency lock, and checks
the current active flags, publication identity and exact revisions. Neither
validation results nor locks are cached between admissions. Both coverage checks
in the draw path remain in place, as do the final whole-group publication checks.

The admission retains the metadata until all its locks are released. Scratch
storage retains capacity only; it cannot prolong source lifetime after admission.
It belongs to the renderer control owner, never preparation workers. Nested
admissions use private lock storage; callers must still obey the existing lock
order and avoid reacquiring a dependency already held by the same thread.

Setting the switch to `false` reconstructs and sorts temporary lock lists for
each coverage check, as the previous implementation did. Immutable committed
metadata is shared in both settings. The switch therefore isolates coverage
admission storage reuse in a same-binary comparison; it does not revert the
metadata representation or alter validation policy.

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

The profile build passed 398 runtime tests and 46 editor tests, including checks
for shared versus independent sector invalidation, conflicting duplicate
tickets, immediate invalidation/retirement, and retained mutex lifetime. The
runtime suite includes 142,560 GPU boundary classifications with zero mismatches.
Eleven offline profiling tests and maintained override hash-contract tests passed.

This change does not alter detail-material query scheduling, request source
capture, replacement-group size or synchronous startup/teleport recovery. The
matched performance record is in TG's `Gem/Docs/TerrainControlStorageProfiling.md`.
