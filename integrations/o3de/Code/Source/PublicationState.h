#pragma once

#include <AzCore/std/containers/unordered_map.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>

namespace TerrainCompositor::Internal
{
    struct PublicationFootprints
    {
        using Bounds = AZStd::unordered_map<AZ::EntityId, AZ::Aabb>;
        Bounds m_height;
        Bounds m_surface;
        Bounds m_existence;
        Bounds m_cutouts;
        Bounds m_gapRendering;
        Bounds m_gapQueries;

        PublicationFootprints() = default;

        template<class State>
        explicit PublicationFootprints(const State& state)
        {
            for (const auto& contributor : state.m_heightContributors)
                m_height.emplace(contributor.GetEntityId(), contributor.GetWorldBounds());
            for (const auto& stamp : state.m_surfaceStamps)
                m_surface.emplace(stamp.m_placement.m_stampEntityId, stamp.m_placement.m_worldBounds);
            for (const auto& contributor : state.m_existenceContributors)
            {
                if (contributor.m_type == PreparedTerrainExistenceContributor::Type::MeshHeightGap)
                {
                    // Logical/render bounds differ from conservative heightfield-cell coverage.
                    m_gapQueries.emplace(contributor.GetEntityId(), contributor.m_meshHeightGap.m_collisionWorldBounds);
                }
                else
                {
                    m_existence.emplace(contributor.GetEntityId(), contributor.GetWorldBounds());
                    if (contributor.m_type == PreparedTerrainExistenceContributor::Type::MeshCutout)
                        m_cutouts.emplace(contributor.GetEntityId(), contributor.GetWorldBounds());
                }
            }
            for (const auto& gap : state.m_meshHeightGaps)
            {
                if (gap.m_affectTerrainRendering)
                    m_gapRendering.emplace(gap.m_entityId, gap.m_worldBounds);
            }
        }
    };
}
