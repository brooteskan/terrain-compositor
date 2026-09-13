#pragma once
// Independent pre-issue-9 map oracle and reused implicit spatial hierarchy.
// Preserved from terrain-compositor 76eeacf for ordering/cost comparisons.
#include <TerrainCompositor/TerrainSectorCoverage.h>
#include <map>
namespace TerrainCompositor
{
    inline TerrainSectorCoverageSelection ReferenceTerrainCoverage(
        std::span<const TerrainSectorCoverageClaim> claims, size_t lodCount)
    {
        using Coordinate = std::pair<int32_t, int32_t>;
        constexpr size_t noClaim = std::numeric_limits<size_t>::max();
        std::vector<std::map<Coordinate, size_t>> levels(lodCount);
        TerrainSectorCoverageSelection selection;
        const auto parent = [](Coordinate coordinate)
        {
            const auto half = [](int32_t value) { return value / 2 - (value < 0 && value % 2 != 0); };
            return Coordinate{ half(coordinate.first), half(coordinate.second) };
        };
        for (size_t index = 0; index < claims.size(); ++index)
        {
            const auto& claim = claims[index];
            if (claim.m_lod >= lodCount) { ++selection.m_unrepresentableChildren; continue; }
            Coordinate coordinate{ claim.m_x, claim.m_y };
            auto [node, inserted] = levels[claim.m_lod].try_emplace(coordinate, noClaim);
            if (!inserted && node->second != noClaim) ++selection.m_duplicateClaims;
            node->second = index;
            for (size_t ancestor = claim.m_lod + 1; ancestor < levels.size(); ++ancestor)
            {
                coordinate = parent(coordinate);
                levels[ancestor].try_emplace(coordinate, noClaim);
            }
        }
        enum class Coverage { Missing, Partial, Full };
        const auto select = [&](auto&& self, size_t lod, Coordinate coordinate) -> Coverage
        {
            const auto node = levels[lod].find(coordinate);
            if (node == levels[lod].end()) return Coverage::Missing;
            const size_t index = node->second;
            uint8_t missing = 0;
            bool partial = false;
            for (uint8_t quadrant = 0; quadrant < 4; ++quadrant)
            {
                const int64_t x = int64_t(coordinate.first) * 2 + (quadrant & 1);
                const int64_t y = int64_t(coordinate.second) * 2 + (quadrant >> 1);
                const bool representable = x >= INT32_MIN && x <= INT32_MAX && y >= INT32_MIN && y <= INT32_MAX;
                const auto coverage = lod && representable ? self(self, lod - 1, { int32_t(x), int32_t(y) }) : Coverage::Missing;
                if (coverage == Coverage::Missing) missing |= 1 << quadrant;
                partial |= coverage == Coverage::Partial;
                if (index != noClaim && claims[index].m_hasData && coverage == Coverage::Partial)
                    ++selection.m_unrepresentableChildren;
            }
            if (index != noClaim)
            {
                if (!claims[index].m_hasData) return Coverage::Full;
                if (missing) selection.m_draws.push_back({ index, missing });
                return partial ? Coverage::Partial : Coverage::Full;
            }
            return !missing && !partial ? Coverage::Full : missing == 0xf ? Coverage::Missing : Coverage::Partial;
        };
        if (!levels.empty())
            for (const auto& [coordinate, unused] : levels.back()) select(select, levels.size() - 1, coordinate);
        return selection;
    }


    struct FlatScratch
    {
        using Coordinate = std::pair<int32_t, int32_t>;
        struct Node { Coordinate m_coordinate; size_t m_claim; };
        std::vector<std::vector<Node>> m_levels;
        TerrainSectorCoverageSelection m_selection;
    };

    //! The same projection serves production raster/RT selection and replacement
    //! assessment. Missing claims are unknown, never authoritative holes.
    inline const TerrainSectorCoverageSelection& ReferenceFlatCoverage(
        std::span<const TerrainSectorCoverageClaim> claims, size_t lodCount, FlatScratch& scratch)
    {
        using Coordinate = FlatScratch::Coordinate;
        using Node = FlatScratch::Node;
        constexpr size_t noClaim = std::numeric_limits<size_t>::max();
        auto& levels = scratch.m_levels;
        // Keep inactive levels' capacity too, but never traverse their old nodes.
        if (levels.size() < lodCount) levels.resize(lodCount);
        for (auto& level : levels) level.clear();
        auto& selection = scratch.m_selection;
        selection.m_draws.clear();
        selection.m_unrepresentableChildren = selection.m_duplicateClaims = 0;
        const auto parent = [](Coordinate coordinate)
        {
            const auto half = [](int32_t value) { return value / 2 - (value < 0 && value % 2 != 0); };
            return Coordinate{ half(coordinate.first), half(coordinate.second) };
        };
        for (size_t index = 0; index < claims.size(); ++index)
        {
            const auto& claim = claims[index];
            if (claim.m_lod >= lodCount) { ++selection.m_unrepresentableChildren; continue; }
            levels[claim.m_lod].push_back({ { claim.m_x, claim.m_y }, index });
        }
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
                        ++selection.m_duplicateClaims;
                        previous = std::max(previous, node.m_claim); // Last supplied claim wins.
                    }
                    else previous = node.m_claim;
                }
            }
            level.resize(count);
            if (lod + 1 < lodCount)
                for (const auto& node : level) levels[lod + 1].push_back({ parent(node.m_coordinate), noClaim });
        }
        enum class Coverage { Missing, Partial, Full };
        const auto select = [&](auto&& self, size_t lod, Coordinate coordinate) -> Coverage
        {
            const auto& level = levels[lod];
            const auto node = std::lower_bound(level.begin(), level.end(), coordinate,
                [](const Node& value, Coordinate key) { return value.m_coordinate < key; });
            if (node == level.end() || node->m_coordinate != coordinate) return Coverage::Missing;
            const size_t index = node->m_claim;
            uint8_t missing = 0;
            bool partial = false;
            for (uint8_t quadrant = 0; quadrant < 4; ++quadrant)
            {
                const int64_t x = int64_t(coordinate.first) * 2 + (quadrant & 1);
                const int64_t y = int64_t(coordinate.second) * 2 + (quadrant >> 1);
                const bool representable = x >= INT32_MIN && x <= INT32_MAX && y >= INT32_MIN && y <= INT32_MAX;
                const auto coverage = lod && representable ? self(self, lod - 1, { int32_t(x), int32_t(y) }) : Coverage::Missing;
                if (coverage == Coverage::Missing) missing |= 1 << quadrant;
                partial |= coverage == Coverage::Partial;
                if (index != noClaim && claims[index].m_hasData && coverage == Coverage::Partial)
                    ++selection.m_unrepresentableChildren;
            }
            if (index != noClaim)
            {
                if (!claims[index].m_hasData) return Coverage::Full;
                if (missing) selection.m_draws.push_back({ index, missing });
                return partial ? Coverage::Partial : Coverage::Full;
            }
            return !missing && !partial ? Coverage::Full : missing == 0xf ? Coverage::Missing : Coverage::Partial;
        };
        if (lodCount)
            for (const auto& node : levels[lodCount - 1]) select(select, lodCount - 1, node.m_coordinate);
        return selection;
    }

}
