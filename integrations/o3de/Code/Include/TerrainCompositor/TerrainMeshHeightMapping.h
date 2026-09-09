#pragma once

#include <cmath>

namespace TerrainCompositor
{
    // GPU contract: IEEE single precision, explicitly rounded intermediates, no
    // multiply-add contraction. AZSL uses precise for the corresponding values.
    // Store reciprocals in the descriptor instead of permitting CPU/GPU division
    // implementations to choose different cell owners.
    inline float TerrainMeshHeightMul(float a, float b)
    {
        volatile float result = a * b;
        return result;
    }
    inline float TerrainMeshHeightAdd(float a, float b)
    {
        volatile float result = a + b;
        return result;
    }
    inline void MapTerrainMeshHeightXY(
        float worldX,
        float worldY,
        float originX,
        float originY,
        float cosYaw,
        float sinYaw,
        float inverseScale,
        float& localX,
        float& localY)
    {
        const float x = TerrainMeshHeightAdd(worldX, -originX);
        const float y = TerrainMeshHeightAdd(worldY, -originY);
        localX = TerrainMeshHeightMul(TerrainMeshHeightAdd(TerrainMeshHeightMul(cosYaw, x), TerrainMeshHeightMul(sinYaw, y)), inverseScale);
        localY =
            TerrainMeshHeightMul(TerrainMeshHeightAdd(TerrainMeshHeightMul(-sinYaw, x), TerrainMeshHeightMul(cosYaw, y)), inverseScale);
    }
    inline float TerrainMeshHeightGridCoordinate(float local, float origin, float inverseSpacing)
    {
        return TerrainMeshHeightMul(TerrainMeshHeightAdd(local, -origin), inverseSpacing);
    }
} // namespace TerrainCompositor
