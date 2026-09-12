#pragma once

#include <AzCore/Interface/Interface.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Uuid.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/function/function_template.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>
#include <TerrainCompositor/TerrainRenderQuery.h>
#include <TerrainCompositor/TerrainPreparationDependency.h>
#include <TerrainCompositor/TerrainProceduralSnapshot.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>

namespace TerrainCompositor
{
    struct TerrainRenderGeometryQuery
    {
        AZ::Aabb m_regionBounds = AZ::Aabb::CreateNull();
        AZStd::function<bool(const AZ::Vector3&)> m_getTerrainExists;
        AZStd::function<float(const AZ::Vector3&)> m_getHeight;
        //! Optional bulk equivalent of both scalar callbacks. Positions retain the
        //! ordinary terrain query's surface Z, including its collision fallback.
        AZStd::function<void(AZStd::span<const AZ::Vector3>, AZStd::span<float>, AZStd::span<bool>)> m_getGeometry;
        TerrainRenderQueryCapability m_capability;
        size_t m_retainedBytes = 0; //!< Zero means unknown: ineligible for bounded deferred admission.
        std::shared_ptr<TerrainPreparationDependency> m_preparationDependency;
        AZ::Uuid m_compositionSession{};
        AZ::u64 m_compositionRevision = 0;
        //! Optional diagnostic adapter, equivalent to the selected callback above.
        //! It must preserve scalar existence-before-height and bulk source order.
        AZStd::function<void(TerrainRenderDispatch, AZStd::span<const AZ::Vector3>, AZStd::span<float>,
            AZStd::span<bool>, TerrainRenderQueryStatistics*)> m_execute;
        //! Acquisition copies this query with retained source callbacks. It never
        //! changes the scene publication. Legacy callbacks remain the fallback.
        AZStd::function<std::shared_ptr<const TerrainRenderGeometryQuery>()> m_acquireSources;
        TerrainProceduralSnapshotPtr m_proceduralSnapshot;
        TerrainRenderQueryStatistics::SourceProvenance m_sourceProvenance;
        const TerrainRenderGeometryQuery* m_liveFallback = nullptr; //!< Owned by the source set's retained publication.
    };

    struct TerrainMeshCutoutRenderSnapshot
    {
        AZ::u64 m_revision = 0;
        AZStd::vector<PreparedTerrainMeshCutout> m_cutouts;
        AZStd::vector<PreparedTerrainMeshHeightGap> m_meshHeightGaps;
        //! Per-composition revisions correlate this scene aggregation with the
        //! QueryState that produced each retained contribution.
        AZStd::vector<AZStd::pair<AZ::Uuid, AZ::u64>> m_compositionGenerations;
        //! Value-owned queries retain immutable composition state, so terrain worker
        //! jobs never call a component.
        AZStd::vector<TerrainRenderGeometryQuery> m_renderGeometryQueries;
    };
    using TerrainMeshCutoutRenderSnapshotPtr = std::shared_ptr<const TerrainMeshCutoutRenderSnapshot>;

    struct TerrainRenderQuerySources
    {
        TerrainMeshCutoutRenderSnapshotPtr m_publication;
        AZStd::vector<TerrainRenderGeometryQuery> m_queries;
        AZStd::vector<TerrainPreparationDependencyTicket> m_dependencies;
    };
    using TerrainRenderQuerySourcesPtr = std::shared_ptr<const TerrainRenderQuerySources>;

    //! Capture before any ordinary queries. One set covers regular, CLOD and both
    //! normal halos. All composition tickets precede acquisition callbacks, which
    //! can invalidate other owners. Source tickets are those captured WITH values.
    inline TerrainRenderQuerySourcesPtr CaptureTerrainRenderQuerySources(TerrainMeshCutoutRenderSnapshotPtr publication)
    {
        if (!publication) return {};
        auto sources = std::make_shared<TerrainRenderQuerySources>();
        sources->m_publication = AZStd::move(publication);
        for (const auto& query : sources->m_publication->m_renderGeometryQueries)
            if (query.m_preparationDependency)
                sources->m_dependencies.push_back({ query.m_preparationDependency, query.m_preparationDependency->Capture() });
        for (const auto& query : sources->m_publication->m_renderGeometryQueries)
        {
            const auto acquired = query.m_acquireSources ? query.m_acquireSources() : nullptr;
            sources->m_queries.push_back(acquired ? *acquired : query);
            auto& retained = sources->m_queries.back();
            retained.m_liveFallback = &query;
            if (query.m_acquireSources && !acquired)
                retained.m_sourceProvenance.m_height = retained.m_sourceProvenance.m_existence = TerrainSourceAcquisition::Rejected;
            // Composition ownership always comes from the original publication.
            retained.m_compositionSession = query.m_compositionSession;
            retained.m_compositionRevision = query.m_compositionRevision;
            if (retained.m_proceduralSnapshot)
                sources->m_dependencies.push_back(retained.m_proceduralSnapshot->m_ticket);
        }
        return sources;
    }

    inline void RecordTerrainRenderSourceAcquisitions(const TerrainRenderQuerySourcesPtr& sources, TerrainRenderQueryStatistics* statistics)
    {
        if (!sources || !statistics) return;
        for (const auto& query : sources->m_queries)
        {
            const auto& source = query.m_sourceProvenance;
            statistics->m_sourceAcquisitions[static_cast<size_t>(source.m_height)]++;
            statistics->m_sourceAcquisitions[static_cast<size_t>(source.m_existence)]++;
            statistics->m_sources.push_back(source);
        }
    }

    inline bool TryGetTerrainRenderGeometryHeight(
        const TerrainMeshCutoutRenderSnapshot& snapshot, const AZ::Vector3& position, float& height)
    {
        for (const auto& query : snapshot.m_renderGeometryQueries)
        {
            const auto& bounds = query.m_regionBounds;
            if (query.m_getHeight && bounds.IsValid() && position.GetX() >= bounds.GetMin().GetX() &&
                position.GetX() <= bounds.GetMax().GetX() && position.GetY() >= bounds.GetMin().GetY() &&
                position.GetY() <= bounds.GetMax().GetY())
            {
                height = query.m_getHeight(position);
                return true;
            }
        }
        return false;
    }

    //! Resolve the terrain-existence value used to build render mesh topology.
    //! Terrain regions are heightfields, so ownership is determined in XY. The
    //! sampled Z value can be outside the region bounds when the ordinary terrain
    //! query reports a collision-only hole; using a three-dimensional containment
    //! test there would leak that coarse hole into render geometry.
    inline bool TryGetTerrainRenderGeometryExists(
        const TerrainMeshCutoutRenderSnapshot& snapshot, const AZ::Vector3& position, bool& terrainExists)
    {
        for (const auto& query : snapshot.m_renderGeometryQueries)
        {
            const AZ::Aabb& bounds = query.m_regionBounds;
            if (query.m_getTerrainExists && bounds.IsValid() && position.GetX() >= bounds.GetMin().GetX() &&
                position.GetX() <= bounds.GetMax().GetX() && position.GetY() >= bounds.GetMin().GetY() &&
                position.GetY() <= bounds.GetMax().GetY())
            {
                terrainExists = query.m_getTerrainExists(position);
                return true;
            }
        }
        return false;
    }

    //! Overlay retained render geometry on ordinary terrain results. Unowned
    //! samples stay untouched. Contiguous runs with the same height/existence
    //! owner use one bulk call; legacy or independently owned callbacks keep their
    //! original first-match semantics (including inclusive XY boundaries).
    inline void ApplyTerrainRenderGeometry(
        const TerrainMeshCutoutRenderSnapshot& snapshot,
        AZStd::span<const AZ::Vector3> positions,
        AZStd::span<float> heights,
        AZStd::span<bool> terrainExists)
    {
        if (positions.size() != heights.size() || positions.size() != terrainExists.size())
        {
            AZ_Assert(false, "Render geometry input/output lists have different sizes.");
            return;
        }
        using Owners = AZStd::pair<const TerrainRenderGeometryQuery*, const TerrainRenderGeometryQuery*>;
        const auto findOwners = [&snapshot](const AZ::Vector3& position)
        {
            Owners owners{ nullptr, nullptr };
            for (const auto& query : snapshot.m_renderGeometryQueries)
            {
                const auto& bounds = query.m_regionBounds;
                if (!bounds.IsValid() ||
                    !(position.GetX() >= bounds.GetMin().GetX() && position.GetX() <= bounds.GetMax().GetX() &&
                      position.GetY() >= bounds.GetMin().GetY() && position.GetY() <= bounds.GetMax().GetY()))
                {
                    continue;
                }
                if (!owners.first && query.m_getHeight)
                    owners.first = &query;
                if (!owners.second && query.m_getTerrainExists)
                    owners.second = &query;
                if (owners.first && owners.second)
                    break;
            }
            return owners;
        };
        size_t start = 0;
        Owners owners = positions.empty() ? Owners{} : findOwners(positions.front());
        while (start < positions.size())
        {
            size_t end = start + 1;
            Owners nextOwners{};
            for (; end < positions.size(); ++end)
            {
                nextOwners = findOwners(positions[end]);
                if (nextOwners != owners)
                    break;
            }
            if (owners.first && owners.first == owners.second && owners.first->m_getGeometry)
            {
                owners.first->m_getGeometry(
                    positions.subspan(start, end - start), heights.subspan(start, end - start), terrainExists.subspan(start, end - start));
            }
            else
            {
                for (size_t index = start; index < end; ++index)
                {
                    if (owners.second)
                        terrainExists[index] = owners.second->m_getTerrainExists(positions[index]);
                    if (owners.first)
                        heights[index] = owners.first->m_getHeight(positions[index]);
                }
            }
            start = end;
            owners = nextOwners;
        }
    }

    struct TerrainRenderQueryRun
    {
        size_t m_start = 0, m_count = 0;
        const TerrainRenderGeometryQuery* m_height = nullptr;
        const TerrainRenderGeometryQuery* m_existence = nullptr;
        AZ::u32 m_fallbackReasons = 0;
        bool m_batch = false;
    };

    //! One publication and independent channel owners for each contiguous range.
    //! The first run is inline so scalar requests and single-owner sectors do not
    //! allocate. This slice deliberately offers only ordinary-then-overlay execution.
    struct TerrainRenderQueryPlan
    {
        TerrainMeshCutoutRenderSnapshotPtr m_publication;
        TerrainRenderQuerySourcesPtr m_sources;
        TerrainRenderQueryRequest m_request;
        TerrainRenderQueryRun m_firstRun;
        AZStd::vector<TerrainRenderQueryRun> m_additionalRuns;
        bool RequiresOrdinaryResults() const { return true; }
    };

    inline TerrainRenderQueryPlan ResolveTerrainRenderQuery(
        TerrainMeshCutoutRenderSnapshotPtr publication, const TerrainRenderQueryRequest& request,
        TerrainRenderQueryStatistics* statistics = nullptr, TerrainRenderQuerySourcesPtr sources = {})
    {
        AZ_PROFILE_SCOPE(AzRender, "Terrain::RenderQuery::ResolveOwnership");
        TerrainRenderQueryTimer timer(statistics ? &statistics->m_resolutionMicroseconds : nullptr);
        TerrainRenderQueryPlan plan;
        plan.m_publication = AZStd::move(publication);
        plan.m_request = request;
        if (sources && sources->m_publication != plan.m_publication)
        {
            AZ_Assert(false, "Source acquisition belongs to a different render publication.");
            return plan;
        }
        plan.m_sources = AZStd::move(sources);
        const auto ownersAt = [&plan](const AZ::Vector3& position)
        {
            TerrainRenderQueryRun run;
            if (plan.m_publication)
            {
                for (const auto& query : plan.m_sources ? plan.m_sources->m_queries : plan.m_publication->m_renderGeometryQueries)
                {
                    const auto& bounds = query.m_regionBounds;
                    if (!bounds.IsValid() || !(position.GetX() >= bounds.GetMin().GetX() &&
                        position.GetX() <= bounds.GetMax().GetX() && position.GetY() >= bounds.GetMin().GetY() &&
                        position.GetY() <= bounds.GetMax().GetY())) continue;
                    if (!run.m_height && query.m_getHeight) run.m_height = &query;
                    if (!run.m_existence && query.m_getTerrainExists) run.m_existence = &query;
                    if (run.m_height && run.m_existence) break;
                }
            }
            return run;
        };
        const auto channelReasons = [&request](const TerrainRenderGeometryQuery* owner, bool height, size_t start, size_t count, bool batch)
        {
            if (!owner) return TerrainRenderFallbackBit(height ? TerrainRenderFallback::UnownedHeight : TerrainRenderFallback::UnownedExistence);
            const auto& capability = owner->m_capability;
            const auto& channel = height ? capability.m_height : capability.m_existence;
            if (!capability.m_declared) return TerrainRenderFallbackBit(TerrainRenderFallback::LegacyContract);
            AZ::u32 reasons = 0;
            using Sampler = AzFramework::Terrain::TerrainDataRequests::Sampler;
            const bool sampler = (request.m_sampler == Sampler::EXACT && capability.m_exact) ||
                (request.m_sampler == Sampler::CLAMP && capability.m_clamp) ||
                (request.m_sampler == Sampler::BILINEAR && capability.m_bilinear);
            const bool grid = request.m_grid == TerrainRenderGrid::ExplicitPositions ? capability.m_acceptsExplicitPositions :
                request.m_grid == TerrainRenderGrid::Regular && capability.m_acceptsRegularGrid && request.m_gridWidth > 0 && request.m_gridHeight > 0 &&
                request.m_gridWidth <= request.m_positions.size() &&
                request.m_positions.size() / request.m_gridWidth == request.m_gridHeight &&
                request.m_positions.size() % request.m_gridWidth == 0 && request.m_gridStart.IsFinite() &&
                request.m_gridSpacing.IsFinite() && request.m_gridSpacing.GetX() > 0 && request.m_gridSpacing.GetY() > 0;
            const bool coordinates = request.m_coordinates == capability.m_coordinates ||
                (request.m_coordinates == TerrainRenderCoordinates::WorldXY && channel.m_sampling.m_declared &&
                    channel.m_inputZ == TerrainRenderInputZ::Independent &&
                    capability.m_coordinates == TerrainRenderCoordinates::WorldXYOrdinarySurfaceZ);
            if (request.m_coordinates == TerrainRenderCoordinates::Unknown || !coordinates ||
                !sampler || !grid || count < capability.m_minSamples || count > capability.m_maxSamples)
                reasons |= TerrainRenderFallbackBit(TerrainRenderFallback::UnsupportedRequest);
            const auto& sampling = channel.m_sampling;
            if (sampling.m_declared && (!sampling.SupportsPositions(request.m_positions.subspan(start, count)) ||
                (!batch && (sampling.m_minSamples > 1 || sampling.m_maxSamples < 1)) ||
                (request.m_grid == TerrainRenderGrid::ExplicitPositions ? !sampling.m_explicitPositions : !sampling.m_regularGrid) ||
                !(request.m_sampler == Sampler::EXACT ? sampling.m_exact :
                  request.m_sampler == Sampler::CLAMP ? sampling.m_clamp :
                  request.m_sampler == Sampler::BILINEAR && sampling.m_bilinear)))
                reasons |= TerrainRenderFallbackBit(TerrainRenderFallback::UnsupportedRequest);
            if (channel.m_source != TerrainRenderSource::RetainedAvailable)
                reasons |= TerrainRenderFallbackBit(channel.m_source == TerrainRenderSource::Live ? TerrainRenderFallback::LiveSource :
                    channel.m_source == TerrainRenderSource::Unavailable ? TerrainRenderFallback::UnavailableSource : TerrainRenderFallback::UnknownSource);
            if (channel.m_inputZ != TerrainRenderInputZ::Independent) reasons |= TerrainRenderFallbackBit(TerrainRenderFallback::InputZ);
            if (channel.m_requiresOrdinaryResult) reasons |= TerrainRenderFallbackBit(TerrainRenderFallback::OrdinaryDependency);
            return reasons;
        };
        for (size_t start = 0; start < request.m_positions.size();)
        {
            auto run = ownersAt(request.m_positions[start]);
            run.m_start = start;
            size_t end = start + 1;
            for (; end < request.m_positions.size(); ++end)
            {
                const auto next = ownersAt(request.m_positions[end]);
                if (next.m_height != run.m_height || next.m_existence != run.m_existence) break;
            }
            run.m_count = end - start;
            const bool splitOwners = run.m_height && run.m_existence && run.m_height != run.m_existence;
            run.m_batch = request.m_allowBatch && run.m_height && run.m_height == run.m_existence && bool(run.m_height->m_getGeometry);
            auto heightReasons = channelReasons(run.m_height, true, start, run.m_count, run.m_batch);
            auto existenceReasons = channelReasons(run.m_existence, false, start, run.m_count, run.m_batch);
            const auto unsupportedSnapshot = [](const TerrainRenderGeometryQuery* owner, AZ::u32 reasons, bool height)
            {
                return owner && owner->m_proceduralSnapshot && owner->m_liveFallback &&
                    (height ? owner->m_capability.m_height : owner->m_capability.m_existence).m_source == TerrainRenderSource::RetainedAvailable &&
                    (reasons & TerrainRenderFallbackBit(TerrainRenderFallback::UnsupportedRequest));
            };
            const bool liveHeight = unsupportedSnapshot(run.m_height, heightReasons, true);
            const bool liveExistence = unsupportedSnapshot(run.m_existence, existenceReasons, false);
            // Preserve the existing bulk call and source order when either channel
            // cannot support its full run. Independently owned/scalar channels can
            // fall back separately. Never regroup or chunk a legacy provider.
            if (liveHeight || (run.m_batch && liveExistence)) run.m_height = run.m_height->m_liveFallback;
            if (liveExistence || (run.m_batch && liveHeight)) run.m_existence = run.m_existence->m_liveFallback;
            if (liveHeight || liveExistence)
            {
                heightReasons |= channelReasons(run.m_height, true, start, run.m_count, run.m_batch);
                existenceReasons |= channelReasons(run.m_existence, false, start, run.m_count, run.m_batch);
            }
            run.m_fallbackReasons = heightReasons | existenceReasons;
            if (splitOwners)
                run.m_fallbackReasons |= TerrainRenderFallbackBit(TerrainRenderFallback::SplitOwners);
            if (statistics)
            {
                if (run.m_height) statistics->m_heightOwned += run.m_count;
                if (run.m_existence) statistics->m_existenceOwned += run.m_count;
                if (run.m_height && run.m_existence) statistics->m_bothOwned += run.m_count;
                if (!run.m_fallbackReasons) statistics->m_independentSamples += run.m_count;
                if (!heightReasons && run.m_height->m_proceduralSnapshot) statistics->m_heightSnapshotSamples += run.m_count;
                if (!existenceReasons && run.m_existence->m_proceduralSnapshot) statistics->m_existenceSnapshotSamples += run.m_count;
            }
            run.m_fallbackReasons |= TerrainRenderFallbackBit(TerrainRenderFallback::PreservedPolicy);
            if (statistics)
                for (size_t reason = 0; reason < statistics->m_fallbackSamples.size(); ++reason)
                    if (run.m_fallbackReasons & (AZ::u32{1} << reason)) statistics->m_fallbackSamples[reason] += run.m_count;
            if (start == 0) plan.m_firstRun = run;
            else plan.m_additionalRuns.push_back(run);
            start = end;
        }
        return plan;
    }

    //! Execute only after ordinary results are supplied. Unsupported capabilities
    //! retain the legacy overlay, including its callback sizes/order; they do not
    //! opt into query elimination or a different sampling policy.
    inline void ExecuteTerrainRenderQuery(
        const TerrainRenderQueryPlan& plan, AZStd::span<float> heights, AZStd::span<bool> terrainExists,
        TerrainRenderQueryStatistics* statistics = nullptr)
    {
        AZ_PROFILE_SCOPE(AzRender, "Terrain::RenderQuery::ExecuteOverlay");
        TerrainRenderQueryTimer timer(statistics ? &statistics->m_executionMicroseconds : nullptr);
        const auto positions = plan.m_request.m_positions;
        if (positions.size() != heights.size() || positions.size() != terrainExists.size())
        {
            AZ_Assert(false, "Render geometry input/output lists have different sizes.");
            return;
        }
        const auto execute = [&](const TerrainRenderQueryRun& run)
        {
            if (!run.m_count || (!run.m_height && !run.m_existence)) return;
            if (statistics)
            {
                statistics->m_retainedSamples += run.m_count;
                (run.m_batch ? statistics->m_batchSamples : statistics->m_scalarSamples) += run.m_count;
            }
            const auto invoke = [&](const TerrainRenderGeometryQuery& owner, TerrainRenderDispatch dispatch, size_t start, size_t count)
            {
                auto points = positions.subspan(start, count);
                auto outHeights = heights.subspan(start, count);
                auto outExists = terrainExists.subspan(start, count);
                if (statistics)
                    (dispatch == TerrainRenderDispatch::Geometry ? statistics->m_batchCallbacks : statistics->m_scalarCallbacks)++;
                if (statistics && owner.m_execute) owner.m_execute(dispatch, points, outHeights, outExists, statistics);
                else if (dispatch == TerrainRenderDispatch::Geometry) owner.m_getGeometry(points, outHeights, outExists);
                else if (dispatch == TerrainRenderDispatch::Existence) outExists[0] = owner.m_getTerrainExists(points[0]);
                else outHeights[0] = owner.m_getHeight(points[0]);
            };
            if (run.m_batch) invoke(*run.m_height, TerrainRenderDispatch::Geometry, run.m_start, run.m_count);
            else
                for (size_t index = run.m_start; index < run.m_start + run.m_count; ++index)
                {
                    if (run.m_existence) invoke(*run.m_existence, TerrainRenderDispatch::Existence, index, 1);
                    if (run.m_height) invoke(*run.m_height, TerrainRenderDispatch::Height, index, 1);
                }
        };
        execute(plan.m_firstRun);
        for (const auto& run : plan.m_additionalRuns) execute(run);
    }

    //! Returns true when the terrain heightfield cell is conservatively removed
    //! by an active mesh-height gap. The retained snapshot and activation keep an
    //! entire asynchronous collider request on one immutable publication.
    inline bool IsTerrainHeightfieldCellRemoved(
        const TerrainMeshCutoutRenderSnapshot& snapshot,
        const TerrainMeshHeightGapActivationPtr& activation,
        const AZ::Vector2& worldCellMinimum,
        const AZ::Vector2& gridSpacing)
    {
        const AZStd::span<const PreparedTerrainMeshHeightGap> admitted =
            activation && activation->m_revision == snapshot.m_revision
            ? AZStd::span<const PreparedTerrainMeshHeightGap>(activation->m_gaps)
            : AZStd::span<const PreparedTerrainMeshHeightGap>{};
        for (const PreparedTerrainMeshHeightGap& gap : snapshot.m_meshHeightGaps)
        {
            const bool gapAdmitted = !gap.m_affectTerrainRendering || AZStd::any_of(
                admitted.begin(), admitted.end(), [&gap](const PreparedTerrainMeshHeightGap& candidate)
                {
                    return gap.m_data == candidate.m_data && gap.m_compositionSession == candidate.m_compositionSession &&
                        gap.m_entityId == candidate.m_entityId && gap.m_originX == candidate.m_originX &&
                        gap.m_originY == candidate.m_originY && gap.m_inverseScale == candidate.m_inverseScale &&
                        gap.m_cosYaw == candidate.m_cosYaw && gap.m_sinYaw == candidate.m_sinYaw &&
                        gap.m_affectTerrainRendering == candidate.m_affectTerrainRendering;
                });
            const auto& cells = gap.m_collisionCells;
            if (!gapAdmitted || !gap.m_affectTerrainCollisionQueries || !cells || cells->m_cells.empty() ||
                !worldCellMinimum.IsFinite() || !gridSpacing.IsFinite() ||
                gridSpacing.GetX() != cells->m_gridSpacing || gridSpacing.GetY() != cells->m_gridSpacing)
            {
                continue;
            }
            const double spacing = cells->m_gridSpacing;
            const double x = double(worldCellMinimum.GetX()) / spacing;
            const double y = double(worldCellMinimum.GetY()) / spacing;
            if (!std::isfinite(x) || !std::isfinite(y) || x < double(std::numeric_limits<AZ::s64>::min()) ||
                x > double(std::numeric_limits<AZ::s64>::max()) || y < double(std::numeric_limits<AZ::s64>::min()) ||
                y > double(std::numeric_limits<AZ::s64>::max()))
            {
                continue;
            }
            const TerrainHeightfieldCellAddress address{
                static_cast<AZ::s64>(std::llround(x)), static_cast<AZ::s64>(std::llround(y)) };
            if (AZStd::binary_search(
                    cells->m_cells.begin(), cells->m_cells.end(), address,
                    [](const TerrainHeightfieldCellAddress& left, const TerrainHeightfieldCellAddress& right)
                    {
                        return left.m_y < right.m_y || (left.m_y == right.m_y && left.m_x < right.m_x);
                    }))
            {
                return true;
            }
        }
        return false;
    }

    struct TerrainMeshCutoutRenderChannel
    {
        std::mutex m_publicationMutex;
        // Protected by the publication mutex; a removed scene channel is never revived.
        bool m_active = true;
        std::atomic<TerrainMeshCutoutRenderSnapshotPtr> m_snapshot{ std::make_shared<const TerrainMeshCutoutRenderSnapshot>() };
        std::atomic<TerrainMeshHeightGapActivationPtr> m_activation{ std::make_shared<const TerrainMeshHeightGapActivation>() };
    };
    using TerrainMeshCutoutRenderChannelPtr = std::shared_ptr<TerrainMeshCutoutRenderChannel>;

    //! Control-thread registry which publishes one value-owned immutable snapshot
    //! to Atom workers.
    class TerrainMeshCutoutRenderRegistry
    {
    public:
        AZ_RTTI(TerrainMeshCutoutRenderRegistry, "{C7951998-E326-43BA-A4ED-A558F8EA28C1}");

        TerrainMeshCutoutRenderRegistry();
        virtual ~TerrainMeshCutoutRenderRegistry();

        //! Virtual so the Terrain module can consume the cross-module AZ::Interface
        //! without linking a consuming game Gem.
        virtual TerrainMeshCutoutRenderChannelPtr AcquireSceneChannel(const void* sceneKey);
        //! Read-only lookup for editor diagnostics. Unlike AcquireSceneChannel,
        //! this never creates publication state.
        TerrainMeshCutoutRenderChannelPtr FindSceneChannel(const void* sceneKey) const;
        bool Publish(
            const void* sceneKey,
            const AZ::Uuid& compositionSession,
            AZStd::vector<PreparedTerrainMeshCutout> cutouts,
            TerrainRenderGeometryQuery renderGeometryQuery = {},
            AZ::u64 compositionRevision = 0,
            AZStd::vector<PreparedTerrainMeshHeightGap> meshHeightGaps = {});
        void Remove(const AZ::Uuid& compositionSession);
        bool ActivateGaps(const void* sceneKey, const TerrainMeshCutoutRenderSnapshotPtr& expected,
            AZStd::vector<PreparedTerrainMeshHeightGap> admitted);
        void ClearGapActivation(const void* sceneKey);
        //! Retire the channel permanently. Feature-processor deactivation can
        //! preserve component-owned registrations for reactivation of the scene.
        void RemoveScene(const void* sceneKey, bool removeRegistrations = true);

    private:
        struct CompositionEntry
        {
            const void* m_sceneKey = nullptr;
            AZ::u64 m_compositionRevision = 0;
            AZStd::vector<PreparedTerrainMeshCutout> m_cutouts;
            AZStd::vector<PreparedTerrainMeshHeightGap> m_meshHeightGaps;
            TerrainRenderGeometryQuery m_renderGeometryQuery;
        };

        void RebuildSnapshot(const void* sceneKey);

        mutable std::mutex m_updateMutex;
        AZStd::unordered_map<AZ::Uuid, CompositionEntry> m_byComposition;
        AZStd::unordered_map<const void*, TerrainMeshCutoutRenderChannelPtr> m_sceneChannels;
        AZ::u64 m_revision = 0;
    };
} // namespace TerrainCompositor
