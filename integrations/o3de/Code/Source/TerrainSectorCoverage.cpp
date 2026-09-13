#include <TerrainCompositor/TerrainSectorCoverage.h>

namespace TerrainCompositor
{
    void BuildTerrainSectorCoveragePlan(std::span<const TerrainSectorCoverageClaim> claims,
        size_t lodCount, TerrainSectorCoverageScratch& scratch)
    {
        namespace graph = wz::core::graph;
        using Coordinate = TerrainSectorCoverageScratch::Coordinate;
        using Node = TerrainSectorCoverageScratch::Node;
        constexpr size_t noClaim = TerrainSectorCoverageScratch::NoClaim;
        scratch.Invalidate();
        scratch.m_invalidClaims = scratch.m_duplicates = 0;
        auto& levels = scratch.m_levels;
        if (levels.size() < lodCount) levels.resize(lodCount);
        for (auto& level : levels) level.clear();
        const auto parent = [](Coordinate coordinate)
        {
            const auto half = [](int32_t value) { return value / 2 - (value < 0 && value % 2 != 0); };
            return Coordinate{ half(coordinate.first), half(coordinate.second) };
        };
        for (size_t index = 0; index < claims.size(); ++index)
        {
            const auto& claim = claims[index];
            if (claim.m_lod >= lodCount) { ++scratch.m_invalidClaims; continue; }
            levels[claim.m_lod].push_back({ { claim.m_x, claim.m_y }, index });
        }
        graph::PolytreeBuilder<size_t, uint8_t> builder;
        for (size_t lod = 0; lod < lodCount; ++lod)
        {
            auto& level = levels[lod];
            std::sort(level.begin(), level.end(), [](const Node& a, const Node& b)
            { return a.m_coordinate < b.m_coordinate; });
            size_t count = 0;
            for (size_t i = 0; i < level.size(); ++i)
            {
                const auto node = level[i];
                if (!count || level[count - 1].m_coordinate != node.m_coordinate) level[count++] = node;
                else if (node.m_claim != noClaim)
                {
                    auto& previous = level[count - 1].m_claim;
                    if (previous != noClaim)
                    {
                        ++scratch.m_duplicates;
                        previous = std::max(previous, node.m_claim); // Last supplied claim wins.
                    }
                    else previous = node.m_claim;
                }
            }
            level.resize(count);
            for (auto& node : level) node.m_handle = graph::add_node(builder, node.m_claim);
            if (lod + 1 < lodCount)
                for (const auto& node : level) levels[lod + 1].push_back({ parent(node.m_coordinate), noClaim });
        }
        // Only cold construction searches spatial coordinates. Insertion order
        // establishes ascending roots and quadrant 0..3 postorder in reverse topo.
        for (size_t lod = 1; lod < lodCount; ++lod)
            for (const auto& node : levels[lod])
                for (uint8_t quadrant = 0; quadrant < 4; ++quadrant)
                {
                    const int64_t x = int64_t(node.m_coordinate.first) * 2 + (quadrant & 1);
                    const int64_t y = int64_t(node.m_coordinate.second) * 2 + (quadrant >> 1);
                    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) continue;
                    const Coordinate coordinate{ int32_t(x), int32_t(y) };
                    const auto& children = levels[lod - 1];
                    const auto child = std::lower_bound(children.begin(), children.end(), coordinate,
                        [](const Node& value, Coordinate key) { return value.m_coordinate < key; });
                    if (child != children.end() && child->m_coordinate == coordinate)
                        graph::add_edge(builder, node.m_handle, child->m_handle, quadrant);
                }
        // Every edge decreases LOD, and every child has one spatial parent.
        scratch.m_topology = std::move(*graph::build(std::move(builder)));
        scratch.m_values.resize(graph::node_count(scratch.m_topology.polytree));
        scratch.m_selection.m_draws.reserve(claims.size());
        scratch.m_keys.assign(claims.begin(), claims.end());
        scratch.m_lodCount = lodCount;
        ++scratch.m_generation;
        scratch.m_valid = true;
    }

}
