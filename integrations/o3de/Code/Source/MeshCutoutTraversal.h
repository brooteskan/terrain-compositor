#pragma once

#include <TerrainCompositor/TerrainMeshCutoutData.h>

namespace TerrainCompositor::Internal
{
    // Escape-index traversal is shared; bounds tests and early termination remain
    // explicit policies. A false visitor continues through every candidate.
    template<class AcceptBounds, class VisitTriangle>
    bool VisitMeshCutoutTriangles(const TerrainMeshCutoutData& data, AcceptBounds acceptBounds, VisitTriangle visitTriangle)
    {
        AZ::u32 nodeIndex = 0;
        while (nodeIndex < data.m_bvh.size())
        {
            const auto& node = data.m_bvh[nodeIndex];
            if (!acceptBounds(node.m_bounds))
            {
                nodeIndex = node.m_escapeIndex;
                continue;
            }
            if (node.m_triangleCount == 0)
            {
                ++nodeIndex;
                continue;
            }
            for (AZ::u32 offset = 0; offset < node.m_triangleCount; ++offset)
                if (visitTriangle(node.m_firstTriangle + offset)) return true;
            nodeIndex = node.m_escapeIndex;
        }
        return false;
    }
}
