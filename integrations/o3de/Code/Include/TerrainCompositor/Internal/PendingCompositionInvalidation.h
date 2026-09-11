#pragma once

#include <TerrainCompositor/TerrainCompositionBus.h>
#include <TerrainCompositor/TerrainInvalidation.h>

namespace TerrainCompositor::Internal
{
    // One value owns deferred footprint metadata and both terrain invalidation queues.
    struct PendingCompositionInvalidation
    {
        void AddHeightChange(const HeightmapStampFootprintChange& change)
        {
            m_changes.push_back(change);
            m_heightTerrain.AddFootprint(change.m_previousRegionEntityId, change.m_previousRegionBounds, change.m_previousBounds);
            m_heightTerrain.AddFootprint(change.m_currentRegionEntityId, change.m_currentRegionBounds, change.m_currentBounds);
        }

        AZStd::vector<HeightmapStampFootprintChange> m_changes;
        TerrainInvalidation m_heightTerrain;
        TerrainInvalidation m_surfaceTerrain;
    };
}
