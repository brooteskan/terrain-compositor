#pragma once

#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>

namespace TerrainCompositor
{
    //! The enabled policy is intentionally independent of capability assessment.
    enum class TerrainSectorQueryPolicy { OrdinaryThenOverlay, RetainedOnly };
    enum class TerrainSectorSchedulePolicy { Synchronous, Deferred };
    enum class TerrainSectorSampleChannels { HeightAndExistence };

    //! Row-major output grid plus one normal row/column on EVERY side. CLOD has
    //! its own spacing and halo; corners are required too (the legacy gather uses
    //! every returned existence value in its any-terrain decision).
    struct TerrainSectorSamplingLayout
    {
        AZ::Vector2 m_start = AZ::Vector2::CreateZero();
        float m_spacing = 0.0f;
        size_t m_samplesX = 0, m_samplesY = 0;
        AzFramework::Terrain::TerrainDataRequests::Sampler m_sampler =
            AzFramework::Terrain::TerrainDataRequests::Sampler::CLAMP;
        TerrainSectorSampleChannels m_channels = TerrainSectorSampleChannels::HeightAndExistence;
        bool m_vertexOrderRemap = false;

        size_t Width() const { return m_samplesX + 2; }
        size_t Height() const { return m_samplesY + 2; }
        size_t Count() const { return Width() * Height(); }
        AZ::Vector2 QueryStart() const { return m_start - AZ::Vector2(m_spacing); }
        bool IsValid() const
        {
            // The packing kernel addresses samples with uint16_t. Validate before
            // adding halos, multiplying dimensions, or allocating planning scratch.
            return m_samplesX > 0 && m_samplesY > 0 && m_samplesX <= 65533 && m_samplesY <= 65533 &&
                Count() <= 65535 && m_start.IsFinite() && std::isfinite(m_spacing) && m_spacing > 0 &&
                QueryStart().IsFinite() && Position(Width() - 1, Height() - 1).IsFinite();
        }
        AZ::Vector3 Position(size_t x, size_t y) const
        {
            // Same float operations/order as pinned TerrainSystem's
            // GenerateInputPositionsFromRegion (not repeated addition or double).
            const auto start = QueryStart();
            return AZ::Vector3(start.GetX() + float(x) * m_spacing, start.GetY() + float(y) * m_spacing, 0.0f);
        }
        TerrainRenderQueryRequest Query(AZStd::span<const AZ::Vector3> positions, bool batch) const
        {
            TerrainRenderQueryRequest request;
            request.m_positions = positions;
            request.m_sampler = m_sampler;
            request.m_allowBatch = batch;
            if (batch)
            {
                request.m_grid = TerrainRenderGrid::Regular;
                request.m_gridStart = QueryStart();
                request.m_gridSpacing = AZ::Vector2(m_spacing);
                request.m_gridWidth = Width();
                request.m_gridHeight = Height();
            }
            return request;
        }
    };

    struct TerrainSectorGatherReadiness
    {
        size_t m_samples = 0, m_bothOwned = 0, m_eligibleSamples = 0;
        AZ::u32 m_fallbackReasons = 0;
    };

    struct TerrainSectorOwnedQuery
    {
        AZStd::vector<AZ::Vector3> m_positions;
        TerrainRenderQueryPlan m_query;
    };

    inline size_t TerrainSectorOwnedQueryBytes(const std::shared_ptr<const TerrainSectorOwnedQuery>& owned)
    {
        return owned ? sizeof(TerrainSectorOwnedQuery) + owned->m_positions.capacity() * sizeof(AZ::Vector3) +
            owned->m_query.m_additionalRuns.capacity() * sizeof(TerrainRenderQueryRun) : 0;
    }

    //! Resolution used to record these counters immediately before execution.
    //! A certified retained gather reuses that resolution but still evaluates
    //! these samples. Record only at execution, never during assessment.
    inline void RecordTerrainSectorOwnedQueryStatistics(const TerrainRenderQueryPlan& plan, TerrainRenderQueryStatistics* statistics)
    {
        if (!statistics) return;
        const auto record = [&](const TerrainRenderQueryRun& run)
        {
            if (!run.m_count) return;
            const auto reasons = run.m_fallbackReasons & ~TerrainRenderFallbackBit(TerrainRenderFallback::PreservedPolicy);
            AZ_Assert(!reasons, "Only fully supported retained queries may reuse sector ownership.");
            if (run.m_height) statistics->m_heightOwned += run.m_count;
            if (run.m_existence) statistics->m_existenceOwned += run.m_count;
            if (run.m_height && run.m_existence) statistics->m_bothOwned += run.m_count;
            if (!reasons)
            {
                statistics->m_independentSamples += run.m_count;
                if (run.m_height && run.m_height->m_proceduralSnapshot) statistics->m_heightSnapshotSamples += run.m_count;
                if (run.m_existence && run.m_existence->m_proceduralSnapshot) statistics->m_existenceSnapshotSamples += run.m_count;
            }
            for (size_t reason = 0; reason < statistics->m_fallbackSamples.size(); ++reason)
                if (run.m_fallbackReasons & (AZ::u32{1} << reason)) statistics->m_fallbackSamples[reason] += run.m_count;
        };
        record(plan.m_firstRun);
        for (const auto& run : plan.m_additionalRuns) record(run);
    }

    inline void ExecuteTerrainSectorOwnedQuery(const TerrainSectorOwnedQuery& owned, const TerrainSectorSamplingLayout& layout,
        AZStd::span<float> heights, AZStd::span<bool> exists, TerrainRenderQueryStatistics* statistics)
    {
        RecordTerrainSectorOwnedQueryStatistics(owned.m_query, statistics);
        if (!owned.m_positions.empty())
        {
            ExecuteTerrainRenderQuery(owned.m_query, heights, exists, statistics);
            return;
        }
        // A corner-certified gather deliberately deferred coordinate allocation
        // until its worker. Its source/publication/run remain the assessed owners.
        AZStd::vector<AZ::Vector3> positions;
        {
            TerrainRenderQueryTimer timer(statistics ? &statistics->m_coordinateMicroseconds : nullptr);
            positions.reserve(layout.Count());
            for (size_t y = 0; y < layout.Height(); ++y)
                for (size_t x = 0; x < layout.Width(); ++x) positions.push_back(layout.Position(x, y));
        }
        auto query = owned.m_query;
        query.m_request = layout.Query(positions, true);
        query.m_request.m_coordinates = TerrainRenderCoordinates::WorldXY;
        ExecuteTerrainRenderQuery(query, heights, exists, statistics);
    }

    struct TerrainSectorSamplingPlan
    {
        // These are the existing owned request boundaries, not another source
        // cache. Query runs retain first-match pointers into this exact source set.
        TerrainMeshCutoutRenderSnapshotPtr m_publication;
        TerrainRenderQuerySourcesPtr m_sources;
        TerrainMeshCutoutRenderChannelPtr m_channel;
        AZStd::vector<TerrainPreparationDependencyTicket> m_dependencies;
        TerrainSectorSamplingLayout m_regular, m_clod;
        bool m_clodEnabled = false; // Conditional on regular data, but required for whole-request eligibility.
        bool m_batchQueries = true;
        bool m_rayTracing = false; // Derived from packed regular data; no additional source samples.
        // Ordinary QueryRegion is still a live bus in the complete request. A
        // retained overlay (even an equivalent one) does not retain that service.
        bool m_ordinaryQueriesRetainedAcrossFrames = false;
        struct AreaDecision
        {
            AZ::Aabb m_bounds = AZ::Aabb::CreateNull();
            bool m_captured = false, m_exists = false;
            // An invalidation ticket alone does not retain TerrainSystem/providers.
            bool m_retainedAcrossFrames = false;
        } m_area;
        TerrainSectorQueryPolicy m_queryPolicy = TerrainSectorQueryPolicy::OrdinaryThenOverlay;
        TerrainSectorSchedulePolicy m_schedulePolicy = TerrainSectorSchedulePolicy::Synchronous;
        TerrainSectorGatherReadiness m_regularReadiness, m_clodReadiness;
        AZ::u32 m_ordinaryFallbacks = 0, m_acrossFramesFallbacks = 0;
        bool m_assessed = false;
        std::shared_ptr<const TerrainSectorOwnedQuery> m_regularQuery, m_clodQuery;

        bool CanAvoidOrdinaryResults() const { return m_assessed && !m_ordinaryFallbacks; }
        bool CanExecuteAcrossFrames() const { return m_assessed && !m_acrossFramesFallbacks; }

        //! Uses the request's existing publication/source set/tickets. No provider
        //! acquisition, sampling, availability probe, or second value cache. Runs
        //! on the preparation worker before either ordinary gather. Assessment is
        //! an observation, never an admission lease; commit revalidates under locks.
        void Assess(TerrainRenderQueryStatistics* statistics = nullptr)
        {
            const auto& publication = m_publication;
            const auto& sources = m_sources;
            const auto& channel = m_channel;
            const auto& dependencies = m_dependencies;
            m_regularQuery.reset();
            m_clodQuery.reset();
            m_regularReadiness = {};
            m_clodReadiness = {};
            m_ordinaryFallbacks = 0;
            const auto bit = TerrainRenderFallbackBit;
            if (!m_area.m_captured) m_ordinaryFallbacks |= bit(TerrainRenderFallback::AreaDecisionMissing);
            if (sources && sources->m_publication != publication) m_ordinaryFallbacks |= bit(TerrainRenderFallback::StalePublication);
            if (channel)
            {
                std::lock_guard lock(channel->m_publicationMutex);
                if (!channel->m_active || channel->m_snapshot.load(std::memory_order_acquire) != publication)
                    m_ordinaryFallbacks |= bit(TerrainRenderFallback::StalePublication);
            }
            if (dependencies.empty()) m_ordinaryFallbacks |= bit(TerrainRenderFallback::MissingInvalidation);
            if (!TerrainPreparationAdmission(dependencies).IsValid()) m_ordinaryFallbacks |= bit(TerrainRenderFallback::StaleDependency);
            // Include both potential gathers even for a captured empty area. Empty
            // ordinary results must not disguise incomplete retained ownership.
            const auto assessGather = [&](const TerrainSectorSamplingLayout& layout, TerrainSectorGatherReadiness& readiness,
                std::shared_ptr<const TerrainSectorOwnedQuery>& retained)
            {
                if (!layout.IsValid())
                {
                    readiness.m_fallbackReasons = bit(TerrainRenderFallback::InvalidLayout);
                    return;
                }
                readiness.m_samples = layout.Count();
                const auto assessRun = [&](const TerrainRenderQueryRun& run)
                {
                    if (!run.m_count) return;
                    AZ::u32 reasons = run.m_fallbackReasons & ~bit(TerrainRenderFallback::PreservedPolicy);
                    if (run.m_height && run.m_existence) readiness.m_bothOwned += run.m_count;
                    const auto assessChannel = [&](const TerrainRenderGeometryQuery* owner, bool height)
                    {
                        if (!owner) return;
                        const auto& proof = (height ? owner->m_capability.m_height : owner->m_capability.m_existence).m_ordinaryEquivalence;
                        if (!proof.m_coordinates) reasons |= bit(TerrainRenderFallback::CoordinatesUnproven);
                        using Sampler = AzFramework::Terrain::TerrainDataRequests::Sampler;
                        if (!(layout.m_sampler == Sampler::EXACT ? proof.m_exact :
                            layout.m_sampler == Sampler::CLAMP ? proof.m_clamp : layout.m_sampler == Sampler::BILINEAR && proof.m_bilinear))
                            reasons |= bit(TerrainRenderFallback::SamplerEquivalenceUnproven);
                        if (!proof.m_renderValue) reasons |= bit(TerrainRenderFallback::RenderValueUnproven);
                        if (!proof.m_collisionFallback) reasons |= bit(TerrainRenderFallback::CollisionFallbackUnproven);
                        const auto acquisition = height ? owner->m_sourceProvenance.m_height : owner->m_sourceProvenance.m_existence;
                        if (acquisition == TerrainSourceAcquisition::ExternalMask) reasons |= bit(TerrainRenderFallback::ExternalMask);
                        if (acquisition == TerrainSourceAcquisition::MissingProvider) reasons |= bit(TerrainRenderFallback::UnavailableSource);
                    };
                    assessChannel(run.m_height, true);
                    assessChannel(run.m_existence, false);
                    if (!reasons) readiness.m_eligibleSamples += run.m_count;
                    readiness.m_fallbackReasons |= reasons;
                };
                const bool batch = m_batchQueries && publication && !publication->m_renderGeometryQueries.empty();
                // A pointwise first owner containing the complete rectangular
                // gather can be certified from its extrema. Float construction
                // is monotone for a valid positive-spacing layout, so every halo
                // point lies inside these bounds. Preserve the full walk for
                // split ownership and all undeclared/subset-limited contracts.
                if (m_schedulePolicy == TerrainSectorSchedulePolicy::Deferred &&
                    sources && sources->m_publication == publication && !sources->m_queries.empty())
                {
                    const auto& owner = sources->m_queries.front();
                    const auto& cap = owner.m_capability;
                    const AZStd::array<AZ::Vector3, 4> corners{
                        layout.Position(0, 0), layout.Position(layout.Width() - 1, 0),
                        layout.Position(0, layout.Height() - 1), layout.Position(layout.Width() - 1, layout.Height() - 1) };
                    const size_t count = batch ? layout.Count() : 1;
                    const auto channelSupportsGrid = [&](const TerrainRenderChannelCapability& channel)
                    {
                        return channel.m_sampling.m_declared && (!batch || channel.m_sampling.m_regularGrid) &&
                            channel.m_sampling.m_minSamples <= count && channel.m_sampling.m_maxSamples >= count;
                    };
                    const auto& bounds = owner.m_regionBounds;
                    if (cap.m_pointwise && owner.m_getHeight && owner.m_getTerrainExists && bounds.IsValid() &&
                        cap.m_minSamples <= count && cap.m_maxSamples >= count && (!batch || cap.m_acceptsRegularGrid) &&
                        channelSupportsGrid(cap.m_height) && channelSupportsGrid(cap.m_existence) &&
                        AZStd::all_of(corners.begin(), corners.end(), [&](const auto& p)
                        {
                            return p.GetX() >= bounds.GetMin().GetX() && p.GetX() <= bounds.GetMax().GetX() &&
                                p.GetY() >= bounds.GetMin().GetY() && p.GetY() <= bounds.GetMax().GetY();
                        }))
                    {
                        auto request = layout.Query(corners, false);
                        request.m_allowBatch = batch;
                        request.m_coordinates = TerrainRenderCoordinates::WorldXY;
                        auto query = [&]()
                        {
                            TerrainRenderQueryTimer ownershipTimer(statistics ? &statistics->m_resolutionMicroseconds : nullptr);
                            return ResolveTerrainRenderQuery(publication, request, nullptr, sources);
                        }();
                        if (query.m_firstRun.m_height == &owner && query.m_firstRun.m_existence == &owner &&
                            query.m_additionalRuns.empty() &&
                            !(query.m_firstRun.m_fallbackReasons & ~bit(TerrainRenderFallback::PreservedPolicy)))
                        {
                            query.m_firstRun.m_count = layout.Count();
                            assessRun(query.m_firstRun);
                            auto owned = std::make_shared<TerrainSectorOwnedQuery>();
                            // The corner span is stack-owned. Retain only the
                            // certified run; execution constructs the grid once.
                            query.m_request = layout.Query({}, batch);
                            query.m_request.m_coordinates = TerrainRenderCoordinates::WorldXY;
                            owned->m_query = AZStd::move(query);
                            retained = AZStd::move(owned);
                            return;
                        }
                    }
                }
                // Match the selected dispatch exactly: scalar callbacks are one
                // explicit position, while a batch retains full grid/run sizes.
                auto owned = std::make_shared<TerrainSectorOwnedQuery>();
                auto& positions = owned->m_positions;
                {
                TerrainRenderQueryTimer coordinatesTimer(batch && statistics ? &statistics->m_coordinateMicroseconds : nullptr);
                if (batch) positions.reserve(layout.Count());
                for (size_t y = 0; y < layout.Height(); ++y)
                    for (size_t x = 0; x < layout.Width(); ++x)
                    {
                        const auto position = layout.Position(x, y);
                        if (batch) positions.push_back(position);
                        else
                        {
                            auto request = layout.Query({ &position, 1 }, false);
                            request.m_coordinates = TerrainRenderCoordinates::WorldXY;
                            const auto query = ResolveTerrainRenderQuery(publication, request, nullptr,
                                sources && sources->m_publication == publication ? sources : nullptr);
                            assessRun(query.m_firstRun);
                        }
                    }
                }
                if (batch)
                {
                    auto request = layout.Query(positions, true);
                    request.m_coordinates = TerrainRenderCoordinates::WorldXY;
                    TerrainRenderQueryTimer ownershipTimer(statistics ? &statistics->m_resolutionMicroseconds : nullptr);
                    owned->m_query = ResolveTerrainRenderQuery(publication, request, nullptr,
                        sources && sources->m_publication == publication ? sources : nullptr);
                    assessRun(owned->m_query.m_firstRun);
                    for (const auto& run : owned->m_query.m_additionalRuns) assessRun(run);
                    // Bound retained run capacity as well as coordinates. Highly
                    // fragmented ownership uses the existing resolver at gather;
                    // retaining an O(samples) run array would exceed the added
                    // per-request planning reservation on the supported grid.
                    if (owned->m_query.m_additionalRuns.size() <= 64)
                        retained = AZStd::move(owned);
                }
            };
            assessGather(m_regular, m_regularReadiness, m_regularQuery);
            if (m_clodEnabled) assessGather(m_clod, m_clodReadiness, m_clodQuery);
            m_ordinaryFallbacks |= m_regularReadiness.m_fallbackReasons | m_clodReadiness.m_fallbackReasons;
            m_acrossFramesFallbacks = m_ordinaryFallbacks;
            if (!m_area.m_retainedAcrossFrames) m_acrossFramesFallbacks |= bit(TerrainRenderFallback::AreaLifetimeUnproven);
            if (!m_ordinaryQueriesRetainedAcrossFrames && m_queryPolicy != TerrainSectorQueryPolicy::RetainedOnly)
                m_acrossFramesFallbacks |= bit(TerrainRenderFallback::OrdinaryQueryLifetimeUnproven);
            m_assessed = true;
            if (statistics)
            {
                statistics->m_requiredSectorSamples = m_regularReadiness.m_samples + m_clodReadiness.m_samples;
                statistics->m_requiredClodSamples = m_clodReadiness.m_samples;
                statistics->m_sectorBothOwned = m_regularReadiness.m_bothOwned + m_clodReadiness.m_bothOwned;
                statistics->m_sectorAvoidOrdinaryEligible = CanAvoidOrdinaryResults();
                statistics->m_sectorAcrossFramesEligible = CanExecuteAcrossFrames();
                statistics->m_sectorOrdinaryFallbacks = m_ordinaryFallbacks;
                statistics->m_sectorAcrossFramesFallbacks = m_acrossFramesFallbacks;
            }
        }
    };
}
