#pragma once

#include <TerrainCompositor/TerrainSectorSampling.h>
#include <algorithm>
#include <utility>

namespace TerrainCompositor
{
    //! One owned request (including regular, halo, CLOD and RT work) is a CPU
    //! scheduling unit. Admission never grants a destination or publication lease.
    enum class TerrainSectorAdmission { Synchronous, Cancelled, InvalidPlan, DeferredUnsupported, Deferred };
    inline TerrainSectorAdmission AssessTerrainSectorAdmission(const TerrainSectorSamplingPlan& plan, bool cancelled,
        bool acrossFrames = false)
    {
        if (cancelled) return TerrainSectorAdmission::Cancelled;
        if (!plan.m_assessed || !plan.m_area.m_captured || !plan.m_regular.IsValid() ||
            (plan.m_clodEnabled && !plan.m_clod.IsValid())) return TerrainSectorAdmission::InvalidPlan;
        // Capability and enabled policy are separate. Even a fully retained plan
        // needs a future scheduler's admission, lifetime and memory policy.
        if (acrossFrames || plan.m_schedulePolicy != TerrainSectorSchedulePolicy::Synchronous)
            return plan.m_schedulePolicy == TerrainSectorSchedulePolicy::Deferred &&
                plan.m_queryPolicy == TerrainSectorQueryPolicy::RetainedOnly && plan.CanExecuteAcrossFrames()
                ? TerrainSectorAdmission::Deferred : TerrainSectorAdmission::DeferredUnsupported;
        return TerrainSectorAdmission::Synchronous;
    }

    enum class TerrainSectorCommitAdmission { CommitWhole, WaitForBudget, AtomicGroupTooLarge };
    inline TerrainSectorCommitAdmission AssessTerrainSectorCommitBudget(size_t uploadBytes, size_t remainingBytes,
        size_t maximumBytes)
    {
        if (uploadBytes > maximumBytes) return TerrainSectorCommitAdmission::AtomicGroupTooLarge;
        return uploadBytes > remainingBytes ? TerrainSectorCommitAdmission::WaitForBudget : TerrainSectorCommitAdmission::CommitWhole;
    }

    //! Ephemeral selection inputs, not a mesh cache. The caller filters validity
    //! and distance under its normal visibility boundary before projecting them.
    struct TerrainSectorCoverageClaim
    {
        int32_t m_x = 0, m_y = 0;
        uint32_t m_lod = 0;
        bool m_hasData = false; // Every supplied claim is known, including empty.
    };
    struct TerrainSectorCoverageSelection
    {
        struct Draw { size_t m_claim; uint8_t m_quadrants; };
        AZStd::vector<Draw> m_draws;
        size_t m_unrepresentableChildren = 0;
        size_t m_duplicateClaims = 0;
        bool IsRepresentable() const { return !m_unrepresentableChildren && !m_duplicateClaims; }
    };

    //! One control owner. Only capacity survives a call; indices and coordinates
    //! describe the supplied claims, never retained sector resources.
    struct TerrainSectorCoverageScratch
    {
        using Coordinate = std::pair<int32_t, int32_t>;
        struct Node { Coordinate m_coordinate; size_t m_claim; };
        AZStd::vector<AZStd::vector<Node>> m_levels;
        TerrainSectorCoverageSelection m_selection;
    };

    //! The same projection serves production raster/RT selection and replacement
    //! assessment. Missing claims are unknown, never authoritative holes.
    inline const TerrainSectorCoverageSelection& SelectTerrainSectorCoverage(
        AZStd::span<const TerrainSectorCoverageClaim> claims, size_t lodCount, TerrainSectorCoverageScratch& scratch)
    {
        using Coordinate = TerrainSectorCoverageScratch::Coordinate;
        using Node = TerrainSectorCoverageScratch::Node;
        constexpr size_t noClaim = AZStd::numeric_limits<size_t>::max();
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
                        previous = AZStd::max(previous, node.m_claim); // Last supplied claim wins.
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

    inline TerrainSectorCoverageSelection SelectTerrainSectorCoverage(
        AZStd::span<const TerrainSectorCoverageClaim> claims, size_t lodCount)
    {
        TerrainSectorCoverageScratch scratch;
        SelectTerrainSectorCoverage(claims, lodCount, scratch);
        return AZStd::move(scratch.m_selection);
    }

    //! Requirements still unproven across a proposed cut through an atomic group.
    //! No caller-supplied "safe" booleans can override these requirements.
    enum class TerrainSectorSplitBlocker : AZ::u32
    {
        WholeGroupPolicy = 1 << 0, Settings = 1 << 1, Publication = 1 << 2,
        SourceTickets = 1 << 3, LiveSampling = 1 << 4, SharedSamples = 1 << 5,
        ClodSamples = 1 << 6, Coverage = 1 << 7
    };
    inline bool TerrainSectorLayoutsOverlap(const TerrainSectorSamplingLayout& a, const TerrainSectorSamplingLayout& b)
    {
        if (!a.IsValid() || !b.IsValid()) return true; // Unknown cannot establish independence.
        const auto aMin = a.QueryStart(), bMin = b.QueryStart();
        const auto aMax = a.Position(a.Width() - 1, a.Height() - 1);
        const auto bMax = b.Position(b.Width() - 1, b.Height() - 1);
        return aMin.GetX() <= bMax.GetX() && bMin.GetX() <= aMax.GetX() &&
            aMin.GetY() <= bMax.GetY() && bMin.GetY() <= aMax.GetY();
    }
    inline AZ::u32 AssessTerrainSectorSplitSampling(const TerrainSectorSamplingPlan& a, const TerrainSectorSamplingPlan& b)
    {
        using Blocker = TerrainSectorSplitBlocker;
        AZ::u32 blockers = 0;
        const auto add = [&](Blocker blocker) { blockers |= static_cast<AZ::u32>(blocker); };
        if (a.m_publication != b.m_publication || a.m_channel != b.m_channel) add(Blocker::Publication);
        const auto contains = [](const auto& tickets, const auto& wanted)
        {
            return AZStd::any_of(tickets.begin(), tickets.end(), [&](const auto& ticket)
            { return ticket.m_dependency == wanted.m_dependency && ticket.m_revision == wanted.m_revision; });
        };
        if (a.m_dependencies.empty() || b.m_dependencies.empty()) add(Blocker::SourceTickets);
        for (const auto& ticket : a.m_dependencies) if (!ticket.m_dependency || !contains(b.m_dependencies, ticket)) add(Blocker::SourceTickets);
        for (const auto& ticket : b.m_dependencies) if (!ticket.m_dependency || !contains(a.m_dependencies, ticket)) add(Blocker::SourceTickets);
        if (!a.CanExecuteAcrossFrames() || !b.CanExecuteAcrossFrames()) add(Blocker::LiveSampling);
        // Packed output does not retain raw halo values. Matching tickets, heights
        // or packed normals alone cannot certify all shared samples, especially
        // with ordinary/unsupported live sources. A later proof must cover these.
        if (TerrainSectorLayoutsOverlap(a.m_regular, b.m_regular)) add(Blocker::SharedSamples);
        if ((a.m_clodEnabled && (TerrainSectorLayoutsOverlap(a.m_clod, b.m_regular) ||
                (b.m_clodEnabled && TerrainSectorLayoutsOverlap(a.m_clod, b.m_clod)))) ||
            (b.m_clodEnabled && TerrainSectorLayoutsOverlap(a.m_regular, b.m_clod))) add(Blocker::ClodSamples);
        return blockers;
    }
}
