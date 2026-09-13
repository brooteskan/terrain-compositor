#pragma once

#include <algo/next.h>
#include <graph/static_polytree.h>
#include <algorithm>
#include <climits>
#include <limits>
#include <span>
#include <vector>

namespace TerrainCompositor
{
    // Supplied claims are current and known, including authoritative emptiness.
    // Resource ownership and publication validation remain with the caller.
    struct TerrainSectorCoverageClaim
    {
        int32_t m_x = 0, m_y = 0;
        uint32_t m_lod = 0;
        bool m_hasData = false;
    };
    struct TerrainSectorCoverageSelection
    {
        struct Draw { size_t m_claim; uint8_t m_quadrants; };
        std::vector<Draw> m_draws;
        size_t m_unrepresentableChildren = 0;
        size_t m_duplicateClaims = 0;
        bool IsRepresentable() const { return !m_unrepresentableChildren && !m_duplicateClaims; }
    };

    // Single control-owner workspace. No pointers to sectors or resources.
    // Output references last until the next evaluation; graph handles are private
    // to this plan generation. Invalidate on an owner/topology identity reset.
    struct TerrainSectorCoverageScratch
    {
        using Coordinate = std::pair<int32_t, int32_t>;
        using Handle = wz::core::graph::NodeHandle;
        static constexpr size_t NoClaim = std::numeric_limits<size_t>::max();
        struct Node { Coordinate m_coordinate; size_t m_claim; Handle m_handle = wz::core::graph::INVALID_NODE; };
        enum class Coverage : uint8_t { Missing, Partial, Full };
        std::vector<std::vector<Node>> m_levels;
        std::vector<TerrainSectorCoverageClaim> m_keys;
        wz::core::graph::PolytreeStorage<size_t, uint8_t> m_topology;
        std::vector<Coverage> m_values;
        TerrainSectorCoverageSelection m_selection;
        size_t m_lodCount = 0, m_invalidClaims = 0, m_duplicates = 0;
        uint64_t m_generation = 0;
        bool m_valid = false;

        TerrainSectorCoverageScratch() = default;
        TerrainSectorCoverageScratch(const TerrainSectorCoverageScratch&) = delete;
        TerrainSectorCoverageScratch& operator=(const TerrainSectorCoverageScratch&) = delete;
        TerrainSectorCoverageScratch(TerrainSectorCoverageScratch&& other) noexcept { *this = std::move(other); }
        TerrainSectorCoverageScratch& operator=(TerrainSectorCoverageScratch&& other) noexcept
        {
            if (this != &other)
            {
                m_levels = std::move(other.m_levels);
                m_keys = std::move(other.m_keys);
                m_topology = std::move(other.m_topology);
                m_values = std::move(other.m_values);
                m_selection = std::move(other.m_selection);
                m_lodCount = other.m_lodCount;
                m_invalidClaims = other.m_invalidClaims;
                m_duplicates = other.m_duplicates;
                m_generation = other.m_generation;
                m_valid = std::exchange(other.m_valid, false);
            }
            return *this;
        }

        void Invalidate() { m_valid = false; }
        bool Matches(std::span<const TerrainSectorCoverageClaim> claims, size_t lodCount) const
        {
            return m_valid && lodCount == m_lodCount && claims.size() == m_keys.size() &&
                std::equal(claims.begin(), claims.end(), m_keys.begin(), [](const auto& a, const auto& b)
                { return a.m_x == b.m_x && a.m_y == b.m_y && a.m_lod == b.m_lod; });
        }
    };

    void BuildTerrainSectorCoveragePlan(std::span<const TerrainSectorCoverageClaim> claims,
        size_t lodCount, TerrainSectorCoverageScratch& scratch);

    // Exact ordered keys are checked on every call, including calls in one frame.
    // Warm evaluation is O(claims + nodes + edges), with no heap allocations.
    // hasData is deliberately not cached: empty/populated transitions reuse the
    // topology and immediately change policy results. See TerrainCoverageTraversal.md.
    inline const TerrainSectorCoverageSelection& EvaluateTerrainSectorCoverage(
        std::span<const TerrainSectorCoverageClaim> claims, size_t lodCount, TerrainSectorCoverageScratch& scratch)
    {
        namespace graph = wz::core::graph;
        using Coverage = TerrainSectorCoverageScratch::Coverage;
        if (!scratch.Matches(claims, lodCount)) BuildTerrainSectorCoveragePlan(claims, lodCount, scratch);
        auto& selection = scratch.m_selection;
        selection.m_draws.clear();
        selection.m_unrepresentableChildren = scratch.m_invalidClaims;
        selection.m_duplicateClaims = scratch.m_duplicates;
        const auto& tree = scratch.m_topology.polytree;
        struct Result { graph::NodeHandle m_node; Coverage m_coverage; };
        struct Sink
        {
            std::span<Coverage> m_values;
            bool push(Result result) { m_values[result.m_node] = result.m_coverage; return true; }
        } sink{ scratch.m_values };
        wz::core::algo::next::transform(graph::evaluation_plan(tree).reverse_topological_order, sink,
            [&](graph::NodeHandle node) -> Result
            {
                const size_t index = graph::node_data(tree, node);
                uint8_t missing = 0xf;
                bool partial = false;
                for (auto child : graph::children(tree, node))
                {
                    const auto coverage = scratch.m_values[child];
                    if (coverage != Coverage::Missing) missing &= ~(1 << graph::parent_edge_data(tree, child));
                    partial |= coverage == Coverage::Partial;
                    if (index != TerrainSectorCoverageScratch::NoClaim && claims[index].m_hasData && coverage == Coverage::Partial)
                        ++selection.m_unrepresentableChildren;
                }
                if (index != TerrainSectorCoverageScratch::NoClaim)
                {
                    if (!claims[index].m_hasData) return { node, Coverage::Full };
                    if (missing) selection.m_draws.push_back({ index, missing });
                    return { node, partial ? Coverage::Partial : Coverage::Full };
                }
                return { node, !missing && !partial ? Coverage::Full : missing == 0xf ? Coverage::Missing : Coverage::Partial };
            });
        return selection;
    }
}
