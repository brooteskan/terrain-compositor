#include <TerrainCompositor/TerrainSectorCoverage.h>

namespace TerrainCompositor
{
    namespace graph = wz::core::graph;
    namespace
    {
        using Scratch = TerrainSectorCoverageScratch;
        using Coverage = Scratch::Coverage;
        bool SameKey(const TerrainSectorCoverageClaim& a, const TerrainSectorCoverageClaim& b)
        {
            return a.m_x == b.m_x && a.m_y == b.m_y && a.m_lod == b.m_lod && a.m_destination == b.m_destination;
        }
        TerrainCoverageRebuild RebuildReason(std::span<const TerrainSectorCoverageClaim> claims, size_t lodCount, const Scratch& scratch)
        {
            if (!scratch.m_valid) return scratch.m_generation ? TerrainCoverageRebuild::Invalidated : TerrainCoverageRebuild::Initial;
            if (lodCount != scratch.m_lodCount) return TerrainCoverageRebuild::LodCount;
            if (claims.size() != scratch.m_keys.size()) return TerrainCoverageRebuild::SlotCount;
            for (size_t i = 0; i < claims.size(); ++i)
            {
                if (claims[i].m_destination != scratch.m_keys[i].m_destination) return TerrainCoverageRebuild::Ordering;
                if (!SameKey(claims[i], scratch.m_keys[i])) return TerrainCoverageRebuild::Coordinates;
            }
            return TerrainCoverageRebuild::None;
        }
        size_t RetainedBytes(const Scratch& scratch)
        {
            const auto bytes = [](const auto& vector) { return vector.capacity() * sizeof(typename std::decay_t<decltype(vector)>::value_type); };
            size_t result = bytes(scratch.m_levels) + bytes(scratch.m_keys) + bytes(scratch.m_previousClaim) +
                bytes(scratch.m_claimNodes) + bytes(scratch.m_direct) + bytes(scratch.m_values) + bytes(scratch.m_dirty) +
                bytes(scratch.m_selection.m_draws) + bytes(scratch.m_batch.m_changes) + scratch.m_closure.capacity_bytes();
            for (const auto& level : scratch.m_levels) result += bytes(level);
            const auto& tree = scratch.m_topology.polytree;
            // Include the compact payload and its alignment slack (same formula as build).
            result += sizeof(size_t) * tree.node_data.size() + alignof(size_t) +
                sizeof(uint32_t) * tree.out_offsets.size() + alignof(uint32_t) +
                sizeof(Scratch::Handle) * tree.out_neighbors.size() + alignof(Scratch::Handle) +
                tree.out_edge_data.size() + 1 +
                sizeof(Scratch::Handle) * tree.parent.size() + alignof(Scratch::Handle) + tree.parent_edge_data.size() + 1 +
                sizeof(Scratch::Handle) * (tree.topo_order.size() + tree.reverse_topo_order.size() + tree.root_order.size() + tree.dependency_order.size()) +
                4 * alignof(Scratch::Handle) + sizeof(uint32_t) * tree.dependency_level_offsets.size() + alignof(uint32_t);
            return result;
        }
    }

    bool TerrainSectorCoverageScratch::Matches(std::span<const TerrainSectorCoverageClaim> claims, size_t lodCount) const
    {
        return RebuildReason(claims, lodCount, *this) == TerrainCoverageRebuild::None;
    }

    void BuildTerrainSectorCoveragePlan(std::span<const TerrainSectorCoverageClaim> claims, size_t lodCount, Scratch& scratch)
    {
        using Coordinate = Scratch::Coordinate;
        using Node = Scratch::Node;
        constexpr size_t noClaim = Scratch::NoClaim;
        scratch.Invalidate();
        scratch.m_previousClaim.assign(claims.size(), noClaim);
        scratch.m_claimNodes.assign(claims.size(), graph::INVALID_NODE);
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
            if (claim.m_lod < lodCount) levels[claim.m_lod].push_back({ { claim.m_x, claim.m_y }, index });
        }
        graph::PolytreeBuilder<size_t, uint8_t> builder;
        // Reserve once per build. The immutable library owns the final storage.
        builder.nodes.reserve(claims.size());
        builder.parent_of.reserve(claims.size());
        builder.edges.reserve(claims.size());
        for (size_t lod = 0; lod < lodCount; ++lod)
        {
            auto& level = levels[lod];
            std::sort(level.begin(), level.end(), [](const Node& a, const Node& b)
            { return a.m_coordinate != b.m_coordinate ? a.m_coordinate < b.m_coordinate : a.m_claim < b.m_claim; });
            size_t count = 0;
            for (size_t i = 0; i < level.size(); ++i)
            {
                const auto node = level[i];
                if (!count || level[count - 1].m_coordinate != node.m_coordinate) level[count++] = node;
                else if (node.m_claim != noClaim)
                {
                    auto& head = level[count - 1].m_claim;
                    scratch.m_previousClaim[node.m_claim] = head;
                    head = node.m_claim;
                }
            }
            level.resize(count);
            for (auto& node : level)
            {
                node.m_handle = graph::add_node(builder, node.m_claim);
                for (auto claim = node.m_claim; claim != noClaim; claim = scratch.m_previousClaim[claim])
                    scratch.m_claimNodes[claim] = node.m_handle;
            }
            if (lod + 1 < lodCount)
                for (const auto& node : level) levels[lod + 1].push_back({ parent(node.m_coordinate), noClaim });
        }
        // Checked signed arithmetic and insertion order preserve the characterized
        // ascending-root, quadrant 0..3 postorder, including absent intermediate LODs.
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
        scratch.m_topology = std::move(*graph::build(std::move(builder)));
        const auto& tree = scratch.m_topology.polytree;
        scratch.m_values.assign(graph::node_count(tree), {});
        scratch.m_dirty.assign(graph::node_count(tree), 0);
        scratch.m_closure.prepare(graph::evaluation_plan(tree));
        scratch.m_direct.reserve(claims.size());
        scratch.m_selection.m_draws.reserve(claims.size());
        scratch.m_selection.m_unrepresentableChildren = scratch.m_selection.m_duplicateClaims = 0;
        scratch.m_invalidClaims = 0;
        scratch.m_batch.m_changes.reserve(claims.size());
        scratch.m_keys.assign(claims.begin(), claims.end());
        scratch.m_lodCount = lodCount;
        scratch.m_selectionDirty = true;
        ++scratch.m_generation;
        scratch.m_valid = true;
    }

    const TerrainCoverageChangeBatch& EvaluateTerrainSectorCoverageChanges(
        std::span<const TerrainSectorCoverageClaim> claims, size_t lodCount, Scratch& scratch, bool forceFull)
    {
        auto& stats = scratch.m_statistics;
        const auto hits = stats.m_planHits, rebuilds = stats.m_planRebuilds;
        stats = {};
        stats.m_rebuild = RebuildReason(claims, lodCount, scratch);
        const bool rebuild = stats.m_rebuild != TerrainCoverageRebuild::None;
        stats.m_planHits = hits + !rebuild;
        stats.m_planRebuilds = rebuilds + rebuild;
        if (rebuild) BuildTerrainSectorCoveragePlan(claims, lodCount, scratch);
        stats.m_fullEvaluation = rebuild || forceFull;
        auto& batch = scratch.m_batch;
        batch.m_baseline = scratch.m_resultGeneration;
        batch.m_result = ++scratch.m_resultGeneration;
        batch.m_plan = scratch.m_generation;
        batch.m_reset = rebuild || forceFull;
        if (batch.m_reset) scratch.m_selectionDirty = true;
        batch.m_nodeCount = scratch.m_values.size();
        batch.m_changes.clear();
        scratch.m_direct.clear();
        auto& selection = scratch.m_selection;
        selection.m_unrepresentableChildren -= scratch.m_invalidClaims;
        scratch.m_invalidClaims = 0;
        for (size_t i = 0; i < claims.size(); ++i)
        {
            const auto& claim = claims[i];
            auto& previous = scratch.m_keys[i];
            if (claim.m_known && claim.m_lod >= lodCount) ++scratch.m_invalidClaims;
            if (rebuild || claim != previous)
            {
                ++stats.m_directlyChangedClaims;
                stats.m_eligibilityChanges += claim.m_known != previous.m_known && claim.m_available && previous.m_available;
                stats.m_availabilityChanges += claim.m_available != previous.m_available;
                stats.m_resourceChanges += claim.m_resourceVersion != previous.m_resourceVersion;
                stats.m_policyChanges += claim.m_hasData != previous.m_hasData;
                if (scratch.m_claimNodes[i] != graph::INVALID_NODE)
                {
                    scratch.m_direct.push_back(scratch.m_claimNodes[i]);
                    scratch.m_dirty[scratch.m_claimNodes[i]] = 1;
                }
                previous = claim;
            }
        }
        selection.m_unrepresentableChildren += scratch.m_invalidClaims;
        // When every slot changed, avoid constructing/sorting an ancestor union
        // that cannot exclude any input. A full snapshot also bounds delta size.
        if (!claims.empty() && stats.m_directlyChangedClaims == claims.size())
        {
            stats.m_fullEvaluation = true;
            batch.m_reset = true;
            scratch.m_selectionDirty = true;
        }
        const auto& tree = scratch.m_topology.polytree;
        const auto order = stats.m_fullEvaluation ? graph::evaluation_plan(tree).reverse_topological_order :
            graph::ancestor_closure_order(tree, scratch.m_direct, scratch.m_closure);
        struct Result { Scratch::Handle m_node; Scratch::Value m_value; };
        struct Sink
        {
            Scratch& m_scratch;
            bool push(Result result)
            {
                auto& old = m_scratch.m_values[result.m_node];
                const auto& value = result.m_value;
                auto& output = m_scratch.m_selection;
                output.m_unrepresentableChildren = output.m_unrepresentableChildren - old.m_partial + value.m_partial;
                output.m_duplicateClaims = output.m_duplicateClaims - old.m_duplicates + value.m_duplicates;
                if ((m_scratch.m_batch.m_reset && value.m_draw.m_quadrants) ||
                    (!m_scratch.m_batch.m_reset && old.m_draw != value.m_draw))
                {
                    m_scratch.m_batch.m_changes.push_back({ m_scratch.m_closure.rank[result.m_node],
                        m_scratch.m_batch.m_reset ? TerrainSectorCoverageSelection::Draw{} : old.m_draw, value.m_draw });
                    m_scratch.m_selectionDirty = true;
                }
                // Parents depend only on child aggregate classification. Emit the
                // local draw change first, even when propagation can stop here.
                if (old.m_coverage != value.m_coverage)
                    if (auto parent = m_scratch.m_topology.polytree.parent[result.m_node]; parent != graph::INVALID_NODE)
                        m_scratch.m_dirty[parent] = 1;
                m_scratch.m_dirty[result.m_node] = 0;
                old = value;
                return true;
            }
        } sink{ scratch };
        namespace algo = wz::core::algo::next;
        const auto evaluate = algo::filter([&](Scratch::Handle node)
            { return stats.m_fullEvaluation || scratch.m_dirty[node]; }) | algo::map([&](Scratch::Handle node) -> Result
        {
            ++stats.m_visitedNodes;
            Scratch::Value value;
            size_t winner = Scratch::NoClaim, known = 0;
            for (auto index = graph::node_data(tree, node); index != Scratch::NoClaim; index = scratch.m_previousClaim[index])
                if (claims[index].m_known)
                {
                    if (winner == Scratch::NoClaim) winner = index;
                    ++known;
                }
            value.m_duplicates = known ? known - 1 : 0;
            uint8_t missing = 0xf;
            bool partial = false;
            for (auto child : graph::children(tree, node))
            {
                ++stats.m_visitedEdges;
                const auto coverage = scratch.m_values[child].m_coverage;
                if (coverage != Coverage::Missing) missing &= ~(1 << graph::parent_edge_data(tree, child));
                partial |= coverage == Coverage::Partial;
                if (winner != Scratch::NoClaim && claims[winner].m_hasData && coverage == Coverage::Partial) ++value.m_partial;
            }
            if (winner != Scratch::NoClaim)
            {
                const auto& claim = claims[winner];
                value.m_coverage = claim.m_hasData && partial ? Coverage::Partial : Coverage::Full;
                if (claim.m_hasData && missing) value.m_draw = { winner, missing, claim.m_destination, claim.m_resourceVersion };
            }
            else value.m_coverage = !missing && !partial ? Coverage::Full : missing == 0xf ? Coverage::Missing : Coverage::Partial;
            return { node, value };
        });
        evaluate(order, sink);
        stats.m_emittedChanges = batch.m_changes.size();
        stats.m_retainedBytes = RetainedBytes(scratch);
        return batch;
    }

    const TerrainSectorCoverageSelection& MaterializeTerrainSectorCoverage(Scratch& scratch)
    {
        if (scratch.m_selectionDirty)
        {
            scratch.m_selection.m_draws.clear();
            for (auto node : graph::evaluation_plan(scratch.m_topology.polytree).reverse_topological_order)
                if (const auto& draw = scratch.m_values[node].m_draw; draw.m_quadrants) scratch.m_selection.m_draws.push_back(draw);
            scratch.m_selectionDirty = false;
        }
        return scratch.m_selection;
    }

    const TerrainSectorCoverageSelection& EvaluateTerrainSectorCoverage(
        std::span<const TerrainSectorCoverageClaim> claims, size_t lodCount, Scratch& scratch)
    {
        EvaluateTerrainSectorCoverageChanges(claims, lodCount, scratch);
        return MaterializeTerrainSectorCoverage(scratch);
    }
}
