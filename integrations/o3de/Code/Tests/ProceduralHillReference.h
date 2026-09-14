#pragma once

// Independent baseline oracle from 9ae5107. Do not delegate to production kernels.
#include <TerrainCompositor/Components/ProceduralGroundGradientComponent.h>
#include <AzCore/Math/MathUtils.h>
#include <cmath>

namespace TerrainCompositor::TestSupport::HillReference
{
    constexpr AZ::u64 NoiseSeed = 0x9E3779B97F4A7C15ULL;
    constexpr float HillRadiusInCells = 0.85f;

    inline float SmoothCurve(float value)
    {
        const float clampedValue = AZ::GetClamp(value, 0.0f, 1.0f);
        return clampedValue * clampedValue * (3.0f - (2.0f * clampedValue));
    }

    inline AZ::u64 MixBits(AZ::u64 value)
    {
        value ^= value >> 30;
        value *= 0xBF58476D1CE4E5B9ULL;
        value ^= value >> 27;
        value *= 0x94D049BB133111EBULL;
        value ^= value >> 31;
        return value;
    }

    inline float CoordinateValue(AZ::s64 x, AZ::s64 y, AZ::u64 salt)
    {
        const AZ::u64 xBits = static_cast<AZ::u64>(x);
        const AZ::u64 yBits = static_cast<AZ::u64>(y);
        const AZ::u64 hash = MixBits(NoiseSeed ^ salt ^ MixBits(xBits) ^ (MixBits(yBits) << 1));
        constexpr float HashScale = 1.0f / static_cast<float>(0x00FFFFFFu);
        return static_cast<float>(hash & 0x00FFFFFFu) * HashScale;
    }

    inline float EvaluateHillField(float x, float y, float density, float riseFrequency, bool pruneZeroProfiles)
    {
        const float cellX = x * density;
        const float cellY = y * density;
        const AZ::s64 baseCellX = static_cast<AZ::s64>(std::floor(cellX));
        const AZ::s64 baseCellY = static_cast<AZ::s64>(std::floor(cellY));
        float blendedBumps = 0.0f;
        float blendedDepressions = 0.0f;

        // Each cell contains exactly one deterministically jittered hill center. Searching neighboring cells keeps the field
        // continuous at cell boundaries while density remains a direct hills-per-meter spacing control.
        for (AZ::s64 offsetY = -1; offsetY <= 1; ++offsetY)
        {
            for (AZ::s64 offsetX = -1; offsetX <= 1; ++offsetX)
            {
                const AZ::s64 hillCellX = baseCellX + offsetX;
                const AZ::s64 hillCellY = baseCellY + offsetY;
                const float hillCenterX = static_cast<float>(hillCellX) +
                    (0.15f + (CoordinateValue(hillCellX, hillCellY, 0xA24BAED4963EE407ULL) * 0.7f));
                const float hillCenterY = static_cast<float>(hillCellY) +
                    (0.15f + (CoordinateValue(hillCellX, hillCellY, 0x9FB21C651E98DF25ULL) * 0.7f));
                const float deltaX = cellX - hillCenterX;
                const float deltaY = cellY - hillCenterY;
                const float normalizedDistance = std::sqrt((deltaX * deltaX) + (deltaY * deltaY)) / HillRadiusInCells;
                const float baseProfile = SmoothCurve(1.0f - normalizedDistance);
                // Frequency is clamped positive. pow(+0, frequency) is +0,
                // and adding a zero-weight feature leaves either union intact.
                // Preserve the exact distance/profile operations at the rim.
                if (pruneZeroProfiles && baseProfile == 0.0f) continue;
                const float hillProfile = std::pow(baseProfile, riseFrequency);

                // Randomly assign each feature as a bump or depression, then combine each group as a smooth bounded union.
                // Subtracting the two smooth fields also gives smooth transitions where opposite feature types overlap.
                const bool isDepression =
                    CoordinateValue(hillCellX, hillCellY, 0xD1B54A32D192ED03ULL) < 0.5f;
                float& blendedFeature = isDepression ? blendedDepressions : blendedBumps;
                blendedFeature += (1.0f - blendedFeature) * hillProfile;
            }
        }

        return blendedBumps - blendedDepressions;
    }


    inline float Evaluate(const AZ::Vector3& position, const ProceduralGroundGradientConfig& configuration, bool pruneZeroProfiles = false)
    {
        const float density = AZ::GetClamp(configuration.m_hillDensity, 0.0f, 1.0f);
        const float amplitudeMeters = AZ::GetClamp(configuration.m_amplitudeMeters, 0.0f, 2048.0f * 0.5f);
        if ((density <= 0.0f) || (amplitudeMeters <= 0.0f))
        {
            return 0.5f;
        }

        const float riseFrequency = AZ::GetClamp(configuration.m_frequency, 0.1f, 16.0f);
        const float hillAmount = EvaluateHillField(position.GetX(), position.GetY(), density, riseFrequency, pruneZeroProfiles);
        const float normalizedAmplitude = amplitudeMeters / 2048.0f;
        return AZ::GetClamp(0.5f + (hillAmount * normalizedAmplitude), 0.0f, 1.0f);
    }
}
