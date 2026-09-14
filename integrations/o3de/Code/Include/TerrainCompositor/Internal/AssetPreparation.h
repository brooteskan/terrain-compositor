#pragma once

#include <AzCore/base.h>

namespace TerrainCompositor::Internal
{
    inline bool IsAssetPreparationCurrent(AZ::u64 expectedGeneration, AZ::u64 expectedTicket,
        AZ::u64 currentGeneration, AZ::u64 currentTicket)
    {
        return expectedGeneration == currentGeneration && expectedTicket == currentTicket;
    }
}
