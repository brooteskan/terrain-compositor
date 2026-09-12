# Deferred terrain upload staging

`r_terrainStageUploads` defaults to `true`. Eligible deferred replacement groups
now upload into private replacement buffers over several frames. The existing
terrain remains visible at its committed coordinates until every member is ready
and the complete group passes final validation. Setting the switch to `false`
retains the previous resource update path for a same-binary comparison.

## Preparation, upload and publication

Workers still produce only owned CPU results. After a whole worker batch is
handed back, the terrain control thread stages five operations per sector:

1. Create and upload the packed height/normal buffer.
2. Create and upload the CLOD buffer, when enabled.
3. Create and upload the ray-tracing position buffer, when enabled.
4. Create and upload the ray-tracing normal buffer, when enabled.
5. Prepare ray-tracing views and mesh metadata, when enabled.

Empty results need no replacement buffers. All completed groups share a soft
deadline of 1 ms from the start of `SectorDispatchControl` and a hard limit of 16
operations per call. One allocation, transfer or metadata operation cannot be
interrupted, so this does not guarantee a 1 ms maximum under driver delays.
Speculative groups can finish staging before the predicted camera boundary.

Each buffer uses the existing Atom common buffer pool and an RHI completion
fence. Submission records the successful device fences individually: a failure
on a later device does not discard the lifetime obligation of an earlier
transfer. Results retain their CPU source vectors, replacement buffers and
reservation until all submitted fences signal. The DX12 host-heap ray-tracing
pool can complete its copy immediately; device-heap transfers use the upload
queue. Ordinary completion and cancellation poll without waiting on the CPU.

Readiness alone cannot publish terrain. Final acceptance rechecks settings,
area existence, publication, source dependencies, destination identity and
serials. The publication/dependency locks cover the entire group commit. Waiting
for uploads consumes no destination lease. Publication swaps raster buffers and
ray-tracing metadata, updates stream views in place, then makes the complete
group visible. Stable geometry-view objects preserve existing draw packets and
shader-added dummy streams. Ray-tracing meshes are registered by the existing
candidate update after publication. Old resources and mesh-info entries are
released after commit locks exit.

## Cancellation and limits

Reversal, superseding work and a staging-switch change cancel an obsolete group.
Cancelled submissions retain their source data until their fences finish. An
upload failure rejects the whole group and requests explicit recovery. Reset,
shutdown and coverage-age recovery drain workers and wait for outstanding
transfers before reclaiming their payloads.

The existing ledger still caps outstanding work at 3 groups, 96 requests and
256 MiB. Staging adds 1 MiB per request per RHI device to the existing 2 MiB CPU
allowance, plus the existing 32 MiB per-group publication/source allowance. On
one device this is 3 MiB per request. These are admission reservations, not a
measurement or hard cap of driver-resident GPU memory. Existing visible buffers
remain allocated separately while replacements are staged. A 30-sector group
with 129 by 129 vertices, CLOD and ray tracing owns approximately 15.24 MiB of
replacement buffer payload per device in addition to its CPU results.

Startup, large teleports, unsupported requests and conservative invalidation
recovery retain their synchronous path. The 500 ms required-coverage deadline
also remains in force. Staging does not reduce those severe recovery stalls or
change the size of the atomic publication group.

## Validation

The final profile build passed 395 runtime tests and 46 editor tests, including
new whole-group upload-readiness, lease preservation, old-coverage retention,
source-change and cancellation checks. Maintained patch generation and input,
output and query hash rejection checks passed. Editor flights exercise real
single-device DX12 transfers; multi-device partial submission is implemented
but has not been exercised on multi-device hardware.

Matched performance results and remaining control-thread work are recorded in
TG's `Gem/Docs/TerrainUploadStagingProfiling.md`. Local builds, manifests, test
logs and flight configurations are under `build/windows/terrain-control`; raw
captures are under `user/TerrainFlightProfiling`.
