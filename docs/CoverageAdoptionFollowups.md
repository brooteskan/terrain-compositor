# Bounded adoption follow-ups

These are separate implementation candidates, not completion claims or a plan to
rewrite every loop. Ranking uses the current source audit and existing profile
evidence; no new edit-workload timings were collected for these domains.

| Rank / tracking ID | Repeated work and generic primitive | Evidence and consumer benefit | Constraints and bounded next experiment |
| --- | --- | --- | --- |
| 1 / TC-F1 | TerrainBatchCandidates.h scans every registration for each batch bounds. A reusable ordered spatial candidate filter could retain a region index and stable input order. | The scan is present in the production batch-query path; existing query profiles establish repeated batch work but do not isolate this scan. Benefit would be fewer tested records per batch. | Preserve registration ordering, region membership, invalid bounds and scalar fallback. Benchmark a fixed 10/100/1000-registration scene with identical positions against the present scan before selecting an index. Keep this separate from coverage topology. |
| 2 / TC-F2 | CompositionInvalidation.h compares previous/current memberships and intersects height/cutout footprints. Reusable ordered set difference and deduplicated affected-key workspaces may avoid repeated temporary collection. | Direct source evidence of two membership scans and nested overlap tests. Current flight evidence does not establish publication churn as a bottleneck. Benefit would be fewer edit-time membership/overlap visits. | Preserve old and new footprints, region identity and typed height/surface revisions. Replay a bounded move/rename/retire edit trace; compare emitted notifications and authored holes against full invalidation. No shared revision counter. |
| 3 / TC-F3 | Mesh cutout and mesh-height BVH queries repeat traversal and candidate collection. A generic caller-owned traversal frontier with explicit completion policy is a candidate. | Geometry query kernels exist, but recent coverage flights do not isolate their traversal costs. Benefit is potential scratch reuse and fewer allocations, not automatically fewer triangle tests. | Inventory all-hit/parity and early-exit query contracts separately. First benchmark one mesh cutout parity workload with exact ray/edge degeneracy oracles. Never replace all-hit traversal with early exit merely because both use a tree. |
| 4 / TC-F4 | TerrainModelGeometry.cpp repeats vertex/index extraction and validation for distinct prepared products. A reusable validated range-to-output operation may share bounded staging. | Current source traverses model meshes, vertices and indices. No refreshed asset-load timing establishes it as a priority. Benefit would be less staging and extraction on model reload. | Keep extraction bounds, missing streams, model generation and source lifetime explicit. Measure one reload workload and compare each typed output independently before sharing work. |
| 5 / TC-F5 | SurfaceStampSampling.cpp repeatedly combines categorical weights and ordered stamp contributions. A typed keyed-accumulation workspace might reuse capacity. | Current source shows categorical accumulation and normalization; no new kernel timings. Possible allocation reduction in surface composition. | Category IDs are not interpolated continuous geometry. Preserve stamp ordering, palette defaults, zero weights, tie behavior and normalization. Prototype against exact categorical output oracles; remain specialized unless a general operation has independent consumers. |

Transform/motion evaluation stays in scene-polytree. Its dirtiness flows down
toward descendants, while coverage aggregation flows up; the new ancestor
operation is not a reason to merge the runtimes. Typed publications and model
source acceptance remain distinct lifetime authorities. Terrain's missing-LOD,
authoritative-empty and quadrant policy also remain specialized.

TC-F1 through TC-F5 are local tracking entries for separate bounded implementation
follow-ups. None changes authored quality, adds a worker pool, or authorizes
repository-wide loop replacement.
