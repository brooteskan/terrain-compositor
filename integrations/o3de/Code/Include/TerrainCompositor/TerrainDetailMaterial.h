#pragma once

#include <AzCore/Math/Aabb.h>
#include <AzCore/std/containers/span.h>
#include <optional>
#include <cmath>
#include <utility>

namespace TerrainCompositor
{
    struct TerrainDetailMaterialPixel
    {
        AZ::u8 m_material1 = 255, m_material2 = 255, m_blend = 0, m_padding = 0;
    };
    static_assert(sizeof(TerrainDetailMaterialPixel) == 4);

    struct TerrainDetailMaterialRegion
    {
        AZ::Aabb m_bounds;
        AZ::u8 m_defaultMaterial = 0;
        bool m_hasSurfaceMappings = false;
    };

    // Certify the same coordinates for every asynchronous job subdivision.
    // Dyadic spacing and bounded integer grid indices make both affine float
    // constructions exact. Other grids retain the ordinary surface query.
    inline std::optional<AZ::Aabb> ExactTerrainDetailQueryBounds(
        AZ::Vector3 start, size_t width, size_t height, float spacing)
    {
        int exponent = 0;
        if (!width || !height || !start.IsFinite() || !std::isnormal(spacing) ||
            std::frexp(spacing, &exponent) != 0.5f) return {};
        for (const auto [value, count] : { std::pair{ start.GetX(), width }, std::pair{ start.GetY(), height } })
        {
            const double index = double(value) / spacing;
            if (index != std::trunc(index) || std::abs(index) + double(count) > 4194304.0) return {};
        }
        const AZ::Vector3 last(start.GetX() + float(width - 1) * spacing, start.GetY() + float(height - 1) * spacing, start.GetZ());
        if (!last.IsFinite()) return {};
        return AZ::Aabb::CreateFromMinMax(start, last);
    }

    // Region order is the renderer's first-containing-region order. The whole
    // conservative XY rectangle must have one owner and no surface mappings.
    // Partial overlaps (including touching boundaries) use the ordinary query.
    // No cached decision survives movement or a material/region notification.
    inline std::optional<TerrainDetailMaterialPixel> FindUniformTerrainDetailMaterial(
        const AZ::Aabb& bounds, AZStd::span<const TerrainDetailMaterialRegion> regions, AZ::u8 passthrough)
    {
        if (!bounds.IsValid()) return {};
        for (const auto& region : regions)
        {
            const auto& a = region.m_bounds;
            if (!a.GetMin().IsFinite() || !a.GetMax().IsFinite()) return {};
            if (a.GetMin().GetX() > bounds.GetMax().GetX() ||
                a.GetMin().GetY() > bounds.GetMax().GetY() ||
                a.GetMax().GetX() < bounds.GetMin().GetX() || a.GetMax().GetY() < bounds.GetMin().GetY()) continue;
            if (region.m_hasSurfaceMappings || a.GetMin().GetX() > bounds.GetMin().GetX() ||
                a.GetMin().GetY() > bounds.GetMin().GetY() || a.GetMax().GetX() < bounds.GetMax().GetX() ||
                a.GetMax().GetY() < bounds.GetMax().GetY()) return {};
            return TerrainDetailMaterialPixel{ region.m_defaultMaterial, 255, 0, 0 };
        }
        return TerrainDetailMaterialPixel{ passthrough, 255, 0, 0 };
    }
}
