#pragma once

#include <TerrainCompositor/Internal/CompositionRegistrations.h>
#include <TerrainCompositor/Internal/PreparedComposition.h>
#include "PublicationState.h"

namespace TerrainCompositor
{
    class TerrainCompositionConfig;
}

namespace TerrainCompositor::Internal
{
    struct CompositionPreparation
    {
        PreparedComposition m_query;
        PublicationFootprints m_footprints;
        AZStd::vector<PreparedTerrainMeshCutout> m_renderCutouts;
        // Preserve claim traversal order for diagnostics, independently of blend sorting.
        AZStd::vector<AZStd::pair<AZStd::string, AZStd::string>> m_collisions;
        AZStd::vector<PreparedRegistrationDiagnostic> m_diagnostics;
    };

    // Deterministic preparation from admitted records and captured environment values.
    // No buses, cache acquisition, diagnostic history, or publication side effects.
    CompositionPreparation PrepareComposition(const CompositionRegistrations& registrations,
        const TerrainCompositionConfig& configuration, const AZ::Aabb& regionBounds, const AZ::Uuid& session,
        float collisionGridSpacing, const AZStd::unordered_set<AZ::EntityId>& nonUniformScaleEntities);
}
