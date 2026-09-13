#pragma once

#include <TerrainCompositor/TerrainSectorSampling.h>
#include <cstring>

namespace TerrainCompositor
{
    //! Request-local raw final render height/existence. Never shares values with
    //! collision, other requests, publications or source acquisitions. Pointwise
    //! equivalence is an additional opt-in: grid/batch support alone is insufficient.
    class TerrainSectorSampleReuse
    {
    public:
        explicit TerrainSectorSampleReuse(const TerrainSectorSamplingPlan& plan)
            : m_regularQuery(plan.m_regularQuery), m_clodQuery(plan.m_clodQuery),
              m_regular(plan.m_regular), m_clod(plan.m_clod), m_publication(plan.m_publication), m_sources(plan.m_sources),
              m_subsetSupported(Supported(plan)) {}

        static bool Supported(const TerrainSectorSamplingPlan& plan)
        {
            if (!plan.CanAvoidOrdinaryResults() || !plan.m_sources ||
                plan.m_sources->m_publication != plan.m_publication ||
                plan.m_regular.m_sampler != plan.m_clod.m_sampler) return false;
            for (const auto& query : plan.m_sources->m_queries)
                if (!query.m_capability.m_pointwise || !query.m_capability.m_acceptsExplicitPositions ||
                    query.m_capability.m_minSamples > 1 || query.m_capability.m_maxSamples < plan.m_regular.Count() ||
                    !query.m_capability.m_height.m_sampling.m_explicitPositions ||
                    !query.m_capability.m_existence.m_sampling.m_explicitPositions ||
                    query.m_capability.m_height.m_sampling.m_minSamples > 1 ||
                    query.m_capability.m_existence.m_sampling.m_minSamples > 1 ||
                    query.m_capability.m_height.m_source != TerrainRenderSource::RetainedAvailable ||
                    query.m_capability.m_existence.m_source != TerrainRenderSource::RetainedAvailable) return false;
            return !plan.m_sources->m_queries.empty();
        }

        void Gather(const TerrainSectorSamplingLayout& layout, AZStd::vector<float>& heights,
            AZStd::span<bool> existence, bool batch, TerrainRenderQueryStatistics* statistics)
        {
            TerrainRenderQueryTimer timer(statistics ? &statistics->m_reuseMicroseconds : nullptr);
            const bool first = m_heights.empty();
            auto& positions = m_positions;
            positions.clear();
            if (first)
            {
                // The regular grid is already the destination for raw samples.
                // Avoid a second output, identity map and scatter just to retain it.
                const bool owned = batch && m_regularQuery && m_regularQuery->m_query.m_sources == m_sources &&
                    m_regularQuery->m_query.m_publication == m_publication &&
                    layout.m_start == m_regular.m_start && layout.m_spacing == m_regular.m_spacing &&
                    layout.Width() == m_regular.Width() && layout.Height() == m_regular.Height() && layout.m_sampler == m_regular.m_sampler;
                AZStd::span<const AZ::Vector3> inputs;
                if (owned && !m_regularQuery->m_positions.empty()) inputs = m_regularQuery->m_positions;
                else
                {
                    TerrainRenderQueryTimer coordinateTimer(statistics ? &statistics->m_coordinateMicroseconds : nullptr);
                    positions.reserve(layout.Count());
                    for (size_t y = 0; y < layout.Height(); ++y)
                        for (size_t x = 0; x < layout.Width(); ++x)
                            positions.push_back(layout.Position(x, y));
                    inputs = positions;
                }
                if (owned) RecordTerrainSectorOwnedQueryStatistics(m_regularQuery->m_query, statistics);
                if (owned && !m_regularQuery->m_positions.empty())
                    ExecuteTerrainRenderQuery(m_regularQuery->m_query, heights, existence, statistics);
                else if (owned)
                {
                    auto resolved = m_regularQuery->m_query;
                    resolved.m_request = layout.Query(inputs, true);
                    resolved.m_request.m_coordinates = TerrainRenderCoordinates::WorldXY;
                    ExecuteTerrainRenderQuery(resolved, heights, existence, statistics);
                }
                else Evaluate(layout, inputs, heights, existence, batch, true, statistics);
                m_heights = heights;
                m_existence.assign(existence.begin(), existence.end());
                Record(statistics, layout.Count(), layout.Count(), positions.capacity() * sizeof(AZ::Vector3) + CacheBytes());
                return;
            }

            // A rectangular grid has only Width + Height distinct axis values.
            // Resolve each once, still comparing the original float bits exactly.
            auto& columns = m_columns;
            auto& rows = m_rows;
            auto& missing = m_missing;
            columns.resize(layout.Width()); rows.resize(layout.Height()); missing.clear();
            for (size_t x = 0; x < layout.Width(); ++x) columns[x] = FindAxis(layout.Position(x, 0).GetX(), true);
            for (size_t y = 0; y < layout.Height(); ++y) rows[y] = FindAxis(layout.Position(0, y).GetY(), false);
            missing.reserve(layout.Count());
            for (size_t y = 0; y < layout.Height(); ++y)
                for (size_t x = 0; x < layout.Width(); ++x)
                {
                    const size_t index = y * layout.Width() + x;
                    if (columns[x] != size_t(-1) && rows[y] != size_t(-1))
                    {
                        const size_t source = rows[y] * m_regular.Width() + columns[x];
                        heights[index] = m_heights[source];
                        existence[index] = m_existence[source];
                    }
                    else
                    {
                        missing.push_back(index);
                        positions.push_back(layout.Position(x, y));
                    }
                }
            auto& evaluated = m_evaluated;
            evaluated.resize(positions.size());
            if (positions.size() > m_existsCapacity)
            {
                m_exists = std::make_unique<bool[]>(positions.size());
                m_existsCapacity = positions.size();
            }
            Evaluate(layout, positions, evaluated, { m_exists.get(), positions.size() }, batch, false, statistics);
            for (size_t i = 0; i < missing.size(); ++i)
            {
                heights[missing[i]] = evaluated[i];
                existence[missing[i]] = m_exists[i];
            }
            Record(statistics, layout.Count(), missing.size(),
                (missing.capacity() + columns.capacity() + rows.capacity()) * sizeof(size_t) +
                positions.capacity() * sizeof(AZ::Vector3) + evaluated.capacity() * sizeof(float) + m_existsCapacity + CacheBytes());
        }

    private:
        void Evaluate(const TerrainSectorSamplingLayout& layout, AZStd::span<const AZ::Vector3> positions,
            AZStd::span<float> heights, AZStd::span<bool> existence, bool batch, bool fullGrid,
            TerrainRenderQueryStatistics* statistics) const
        {
            if (!positions.empty())
            {
                auto query = layout.Query(positions, fullGrid && batch);
                query.m_coordinates = TerrainRenderCoordinates::WorldXY;
                query.m_allowBatch = batch;
                if (batch)
                {
                    // A subset of a certified pointwise single-owner CLOD run
                    // keeps the same owners. Split ownership retains resolution.
                    if (!fullGrid && m_subsetSupported && m_clodQuery &&
                        m_clodQuery->m_query.m_sources == m_sources && m_clodQuery->m_query.m_publication == m_publication &&
                        m_clodQuery->m_query.m_additionalRuns.empty() &&
                        layout.m_start == m_clod.m_start && layout.m_spacing == m_clod.m_spacing &&
                        layout.Width() == m_clod.Width() && layout.Height() == m_clod.Height() && layout.m_sampler == m_clod.m_sampler)
                    {
                        auto resolved = m_clodQuery->m_query;
                        resolved.m_request = query;
                        resolved.m_firstRun.m_count = positions.size();
                        RecordTerrainSectorOwnedQueryStatistics(resolved, statistics);
                        ExecuteTerrainRenderQuery(resolved, heights, existence, statistics);
                    }
                    else
                    {
                        const auto resolved = ResolveTerrainRenderQuery(m_publication, query, statistics, m_sources);
                        ExecuteTerrainRenderQuery(resolved, heights, existence, statistics);
                    }
                }
                else for (size_t i = 0; i < positions.size(); ++i)
                {
                    query.m_positions = { &positions[i], 1 };
                    const auto resolved = ResolveTerrainRenderQuery(m_publication, query, statistics, m_sources);
                    ExecuteTerrainRenderQuery(resolved, { &heights[i], 1 }, { &existence[i], 1 }, statistics);
                }
            }
        }
        size_t CacheBytes() const
        {
            return m_heights.capacity() * sizeof(float) + m_existence.capacity() +
                TerrainSectorOwnedQueryBytes(m_regularQuery) + TerrainSectorOwnedQueryBytes(m_clodQuery);
        }
        static void Record(TerrainRenderQueryStatistics* statistics, size_t requested, size_t evaluated, size_t scratch)
        {
            if (statistics)
            {
                statistics->m_requestedSamples += requested;
                statistics->m_evaluatedSamples += evaluated;
                statistics->m_reusedSamples += requested - evaluated;
                statistics->m_skippedOrdinarySamples += requested;
                statistics->m_reuseScratchBytes = AZStd::max(statistics->m_reuseScratchBytes, scratch);
            }
        }
        static bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }
        size_t FindAxis(float value, bool x) const
        {
            size_t lo = 0, hi = x ? m_regular.Width() : m_regular.Height();
            const auto at = [&](size_t i) { const auto p = m_regular.Position(x ? i : 0, x ? 0 : i); return x ? p.GetX() : p.GetY(); };
            const size_t count = hi;
            while (lo < hi) { const size_t mid = lo + (hi - lo) / 2; if (at(mid) < value) lo = mid + 1; else hi = mid; }
            return lo < count && SameBits(at(lo), value) ? lo : size_t(-1);
        }
        AZStd::vector<AZ::Vector3> m_positions;
        AZStd::vector<size_t> m_columns, m_rows, m_missing;
        AZStd::vector<float> m_evaluated;
        std::unique_ptr<bool[]> m_exists;
        size_t m_existsCapacity = 0;
        std::shared_ptr<const TerrainSectorOwnedQuery> m_regularQuery, m_clodQuery;
        TerrainSectorSamplingLayout m_regular, m_clod;
        TerrainMeshCutoutRenderSnapshotPtr m_publication;
        TerrainRenderQuerySourcesPtr m_sources;
        bool m_subsetSupported = false;
        AZStd::vector<float> m_heights;
        AZStd::vector<uint8_t> m_existence;
    };
}
