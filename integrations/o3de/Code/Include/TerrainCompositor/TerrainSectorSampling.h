#pragma once

#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>

namespace TerrainCompositor
{
    //! The enabled policy is intentionally independent of capability assessment.
    enum class TerrainSectorQueryPolicy { OrdinaryThenOverlay };
    enum class TerrainSectorSchedulePolicy { Synchronous };
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
            const auto assessGather = [&](const TerrainSectorSamplingLayout& layout, TerrainSectorGatherReadiness& readiness)
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
                // Match the selected dispatch exactly: scalar callbacks are one
                // explicit position, while a batch retains full grid/run sizes.
                AZStd::vector<AZ::Vector3> positions;
                if (batch) positions.reserve(layout.Count());
                for (size_t y = 0; y < layout.Height(); ++y)
                    for (size_t x = 0; x < layout.Width(); ++x)
                    {
                        const auto position = layout.Position(x, y);
                        if (batch) positions.push_back(position);
                        else
                        {
                            const auto query = ResolveTerrainRenderQuery(publication, layout.Query({ &position, 1 }, false), nullptr,
                                sources && sources->m_publication == publication ? sources : nullptr);
                            assessRun(query.m_firstRun);
                        }
                    }
                if (batch)
                {
                    const auto query = ResolveTerrainRenderQuery(publication, layout.Query(positions, true), nullptr,
                        sources && sources->m_publication == publication ? sources : nullptr);
                    assessRun(query.m_firstRun);
                    for (const auto& run : query.m_additionalRuns) assessRun(run);
                }
            };
            assessGather(m_regular, m_regularReadiness);
            if (m_clodEnabled) assessGather(m_clod, m_clodReadiness);
            m_ordinaryFallbacks |= m_regularReadiness.m_fallbackReasons | m_clodReadiness.m_fallbackReasons;
            m_acrossFramesFallbacks = m_ordinaryFallbacks;
            if (!m_area.m_retainedAcrossFrames) m_acrossFramesFallbacks |= bit(TerrainRenderFallback::AreaLifetimeUnproven);
            if (!m_ordinaryQueriesRetainedAcrossFrames) m_acrossFramesFallbacks |= bit(TerrainRenderFallback::OrdinaryQueryLifetimeUnproven);
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
