#pragma once

#include <AzCore/Debug/Trace.h>
#include <AzCore/std/containers/span.h>
#include <AzFramework/SurfaceData/SurfaceData.h>

namespace TerrainCompositor
{
    inline bool PrepareSurfaceWeightBatch(
        AZStd::span<const AZ::Vector3> positions,
        AZStd::span<AzFramework::SurfaceData::SurfaceTagWeightList> outSurfaceWeights)
    {
        if (positions.size() != outSurfaceWeights.size())
        {
            AZ_Assert(false, "Input and surface output lists are different sizes (%zu vs %zu).",
                positions.size(), outSurfaceWeights.size());
            return false;
        }
        for (auto& weights : outSurfaceWeights)
        {
            weights.clear();
        }
        return true;
    }
} // namespace TerrainCompositor
