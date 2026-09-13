#pragma once

#include <TerrainCompositor/TerrainSectorSampling.h>
#include <TerrainCompositor/TerrainSectorCoverage.h>
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

    //! Adapter preserves the O3DE span API; the topology/policy header is engine-neutral.
    inline const TerrainSectorCoverageSelection& SelectTerrainSectorCoverage(
        AZStd::span<const TerrainSectorCoverageClaim> claims, size_t lodCount, TerrainSectorCoverageScratch& scratch)
    {
        return EvaluateTerrainSectorCoverage({ claims.data(), claims.size() }, lodCount, scratch);
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
