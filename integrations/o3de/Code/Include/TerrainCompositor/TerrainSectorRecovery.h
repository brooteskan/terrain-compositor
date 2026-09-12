#pragma once

#include <TerrainCompositor/TerrainSectorScheduling.h>
#include <cmath>

namespace TerrainCompositor
{
    inline bool IsWithinTerrainCoverage(bool hasData, const AZ::Aabb& bounds, AZ::Vector3 camera, float distanceSquared)
    {
        // Empty claims remain authoritative independently of geometric distance.
        return !hasData || bounds.GetDistanceSq(camera) < distanceSquared;
    }

    struct TerrainRecoveryLimits
    {
        static constexpr size_t PendingRequests = 512;
        static constexpr size_t RequestsPerFrame = 4;
        static constexpr size_t PlanBytes = 64 * 1024;
        static constexpr double FirstCoverageUs = 500000;
        static constexpr double RefinementUs = 5000000;
    };

    struct TerrainSectorSamplingCertificate
    {
        TerrainSectorSamplingLayout m_regular, m_clod;
        bool m_clodEnabled = false, m_proven = false;
    };

    inline bool IsExactTerrainSectorLattice(const TerrainSectorSamplingLayout& layout)
    {
        int exponent = 0;
        if (!layout.IsValid() || !std::isnormal(layout.m_spacing) ||
            std::frexp(layout.m_spacing, &exponent) != 0.5f) return false;
        for (const auto value : { layout.m_start.GetX(), layout.m_start.GetY() })
        {
            const double index = double(value) / layout.m_spacing;
            if (index != std::trunc(index) || std::abs(index) + AZStd::max(layout.Width(), layout.Height()) + 2 > 4194304.0)
                return false;
        }
        return true;
    }

    // Across-acquisition equivalence is a stronger, explicit source contract
    // than per-request subset reuse. The built-in adapter grants it only for
    // immutable composed inputs whose exact authority/revision tickets match.
    // Publication, ticket and destination freshness remain commit-time checks.
    inline TerrainSectorSamplingCertificate CertifyTerrainSectorSampling(const TerrainSectorSamplingPlan& plan)
    {
        TerrainSectorSamplingCertificate certificate{ plan.m_regular, plan.m_clod, plan.m_clodEnabled, false };
        if (!plan.CanAvoidOrdinaryResults() || !plan.m_area.m_retainedAcrossFrames ||
            plan.m_queryPolicy != TerrainSectorQueryPolicy::RetainedOnly || !plan.m_sources || plan.m_sources->m_queries.empty() ||
            plan.m_sources->m_publication != plan.m_publication || !IsExactTerrainSectorLattice(plan.m_regular) ||
            (plan.m_clodEnabled && (!IsExactTerrainSectorLattice(plan.m_clod) || plan.m_regular.m_sampler != plan.m_clod.m_sampler)))
            return certificate;
        for (const auto& query : plan.m_sources->m_queries)
            if (!query.m_capability.m_pointwise || !query.m_capability.m_replacementSampling) return certificate;
        certificate.m_proven = true;
        return certificate;
    }

    inline bool TerrainSectorCertificatesOverlap(const TerrainSectorSamplingCertificate& a,
        const TerrainSectorSamplingCertificate& b)
    {
        return TerrainSectorLayoutsOverlap(a.m_regular, b.m_regular) ||
            (a.m_clodEnabled && TerrainSectorLayoutsOverlap(a.m_clod, b.m_regular)) ||
            (b.m_clodEnabled && TerrainSectorLayoutsOverlap(a.m_regular, b.m_clod)) ||
            (a.m_clodEnabled && b.m_clodEnabled && TerrainSectorLayoutsOverlap(a.m_clod, b.m_clod));
    }
}
