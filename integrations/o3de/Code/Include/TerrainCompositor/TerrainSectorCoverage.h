#pragma once

#include <algo/next.h>
#include <graph/ancestor_closure.h>
#include <algorithm>
#include <climits>
#include <limits>
#include <span>
#include <vector>

namespace TerrainCompositor
{
    // Logical slots include unknown (unavailable/ineligible) claims. Unknown is
    // never authoritative emptiness. Tokens identify resources, not authority.
    struct TerrainSectorCoverageClaim
    {
        int32_t m_x = 0, m_y = 0;
        uint32_t m_lod = 0;
        bool m_hasData = false;
        bool m_known = true;
        uint64_t m_destination = 0, m_resourceVersion = 0;
        bool m_available = true; // Diagnostic provenance; m_known is the coverage-policy input.
        bool operator==(const TerrainSectorCoverageClaim&) const = default;
    };
    struct TerrainSectorCoverageSelection
    {
        struct Draw
        {
            size_t m_claim = std::numeric_limits<size_t>::max();
            uint8_t m_quadrants = 0;
            uint64_t m_destination = 0, m_resourceVersion = 0;
            bool operator==(const Draw&) const = default;
        };
        std::vector<Draw> m_draws;
        size_t m_unrepresentableChildren = 0, m_duplicateClaims = 0;
        bool IsRepresentable() const { return !m_unrepresentableChildren && !m_duplicateClaims; }
    };

    enum class TerrainCoverageRebuild : uint8_t { None, Initial, Invalidated, LodCount, SlotCount, Coordinates, Ordering };
    struct TerrainCoverageStatistics
    {
        TerrainCoverageRebuild m_rebuild = TerrainCoverageRebuild::None;
        size_t m_planHits = 0, m_planRebuilds = 0;
        size_t m_directlyChangedClaims = 0, m_visitedNodes = 0, m_visitedEdges = 0, m_emittedChanges = 0;
        size_t m_eligibilityChanges = 0, m_availabilityChanges = 0, m_resourceChanges = 0, m_policyChanges = 0;
        size_t m_retainedBytes = 0;
        bool m_fullEvaluation = false;
    };
    struct TerrainCoverageChangeBatch
    {
        struct Change
        {
            size_t m_order = 0;
            TerrainSectorCoverageSelection::Draw m_before, m_after;
        };
        uint64_t m_baseline = 0, m_result = 0, m_plan = 0;
        // Reset replaces the entire consumer state; changes contain all draws.
        bool m_reset = false;
        size_t m_nodeCount = 0;
        std::vector<Change> m_changes;
    };

    struct TerrainSectorCoverageState
    {
        using Coordinate = std::pair<int32_t, int32_t>;
        using Handle = wz::core::graph::NodeHandle;
        static constexpr size_t NoClaim = std::numeric_limits<size_t>::max();
        struct Node { Coordinate m_coordinate; size_t m_claim; Handle m_handle = wz::core::graph::INVALID_NODE; };
        enum class Coverage : uint8_t { Missing, Partial, Full };
        struct Value
        {
            Coverage m_coverage = Coverage::Missing;
            TerrainSectorCoverageSelection::Draw m_draw;
            size_t m_partial = 0, m_duplicates = 0;
        };
        std::vector<std::vector<Node>> m_levels;
        std::vector<TerrainSectorCoverageClaim> m_keys;
        std::vector<size_t> m_previousClaim;
        std::vector<Handle> m_claimNodes, m_direct;
        std::vector<uint8_t> m_dirty;
        wz::core::graph::PolytreeStorage<size_t, uint8_t> m_topology;
        wz::core::graph::AncestorClosureWorkspace m_closure;
        std::vector<Value> m_values;
        TerrainSectorCoverageSelection m_selection;
        TerrainCoverageChangeBatch m_batch;
        TerrainCoverageStatistics m_statistics;
        size_t m_lodCount = 0, m_invalidClaims = 0;
        uint64_t m_generation = 0, m_resultGeneration = 0;
        bool m_valid = false, m_selectionDirty = true;
    };
    // Single control owner, no resource pointers. All borrowed results expire on
    // evaluation, invalidation, move or destruction. See the coverage contract.
    struct TerrainSectorCoverageScratch : TerrainSectorCoverageState
    {
        TerrainSectorCoverageScratch() = default;
        TerrainSectorCoverageScratch(const TerrainSectorCoverageScratch&) = delete;
        TerrainSectorCoverageScratch& operator=(const TerrainSectorCoverageScratch&) = delete;
        TerrainSectorCoverageScratch(TerrainSectorCoverageScratch&& other) noexcept { *this = std::move(other); }
        TerrainSectorCoverageScratch& operator=(TerrainSectorCoverageScratch&& other) noexcept
        {
            if (this != &other)
            {
                TerrainSectorCoverageState::operator=(std::move(other));
                other.Invalidate();
            }
            return *this;
        }
        void Invalidate() { m_valid = false; }
        bool Matches(std::span<const TerrainSectorCoverageClaim> claims, size_t lodCount) const;
    };

    void BuildTerrainSectorCoveragePlan(std::span<const TerrainSectorCoverageClaim> claims,
        size_t lodCount, TerrainSectorCoverageScratch& scratch);
    // Full input comparison on EVERY call, even within a frame. Sparse evaluation
    // visits the inclusive ancestor closure. forceFull is the correctness oracle.
    const TerrainCoverageChangeBatch& EvaluateTerrainSectorCoverageChanges(
        std::span<const TerrainSectorCoverageClaim> claims, size_t lodCount, TerrainSectorCoverageScratch& scratch,
        bool forceFull = false);
    const TerrainSectorCoverageSelection& MaterializeTerrainSectorCoverage(TerrainSectorCoverageScratch& scratch);
    const TerrainSectorCoverageSelection& EvaluateTerrainSectorCoverage(
        std::span<const TerrainSectorCoverageClaim> claims, size_t lodCount, TerrainSectorCoverageScratch& scratch);

    // Validate before consumer mutation. Reset permits resynchronization, but an
    // obsolete batch never does. A consumer missing a delta requests a full reset.
    inline bool CanApplyTerrainCoverageBatch(const TerrainCoverageChangeBatch& batch, uint64_t baseline, uint64_t plan)
    {
        return batch.m_result > baseline && (batch.m_reset || (batch.m_baseline == baseline && batch.m_plan == plan));
    }
}
