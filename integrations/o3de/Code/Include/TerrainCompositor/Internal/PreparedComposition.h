#pragma once

#include <TerrainCompositor/HeightmapStampSampling.h>
#include <TerrainCompositor/SurfaceStampSampling.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>

namespace TerrainCompositor::Internal
{
    // Value-owned geometry; the publisher adds reconstruction and immutable query ownership.
    struct PreparedComposition
    {
        AZ::Aabb m_regionBounds = AZ::Aabb::CreateNull();
        HeightmapRegionMapping m_regionMapping;
        AZStd::vector<PreparedHeightContributor> m_heightContributors;
        PreparedSurfacePalette m_surfacePalette;
        AZStd::vector<PreparedSurfaceStamp> m_surfaceStamps;
        AZStd::vector<PreparedTerrainExistenceContributor> m_existenceContributors;
        // Retain render-only gaps to correlate ownership with the query revision.
        AZStd::vector<PreparedTerrainMeshHeightGap> m_meshHeightGaps;
    };
}
