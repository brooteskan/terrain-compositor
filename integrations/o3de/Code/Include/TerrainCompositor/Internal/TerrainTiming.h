#pragma once

#include <chrono>

namespace TerrainCompositor::Internal
{
    inline double TerrainTimestampMicroseconds()
    {
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}
