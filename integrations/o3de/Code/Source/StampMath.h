#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace TerrainCompositor::Internal
{
    inline double SmoothStep01(double value)
    {
        value = std::clamp(value, 0.0, 1.0);
        return value * value * (3.0 - 2.0 * value);
    }

    inline float RoundOutward(double value, bool lower)
    {
        float result = static_cast<float>(value);
        if ((lower && result > value) || (!lower && result < value))
        {
            result = std::nextafter(result, lower ? -std::numeric_limits<float>::infinity()
                                                 : std::numeric_limits<float>::infinity());
        }
        return result;
    }
}
