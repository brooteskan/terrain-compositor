#include <TerrainCompositor/TerrainInvalidation.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace TerrainCompositor
{
    namespace
    {
        bool FiniteBounds(const AZ::Aabb& bounds)
        {
            return bounds.IsValid() && bounds.GetMin().IsFinite() && bounds.GetMax().IsFinite();
        }

        bool UsableRegion(const AZ::Aabb& bounds)
        {
            return FiniteBounds(bounds) && bounds.GetMax().GetX() > bounds.GetMin().GetX() &&
                bounds.GetMax().GetY() > bounds.GetMin().GetY();
        }

        double AreaXY(const AZ::Aabb& bounds)
        {
            return (double(bounds.GetMax().GetX()) - bounds.GetMin().GetX()) *
                (double(bounds.GetMax().GetY()) - bounds.GetMin().GetY());
        }

        bool CanMerge(const AZ::Aabb& a, const AZ::Aabb& b)
        {
            if (a.Contains(b) || b.Contains(a))
            {
                return true;
            }
            // Bounds are extruded to the same region Z. Never bridge disjoint XY footprints.
            if (!a.Overlaps(b))
            {
                return false;
            }
            AZ::Aabb combined = a;
            combined.AddAabb(b);
            const double coveredArea = AreaXY(a) + AreaXY(b) - AreaXY(a.GetClamped(b));
            // Avoid turning a bent/diagonal drag into a large rectangular refresh. No count-based global fallback.
            constexpr double MaxMergeAreaRatio = 1.1;
            return AreaXY(combined) <= coveredArea * MaxMergeAreaRatio;
        }

        float RoundOutward(double value, bool lower)
        {
            float result = static_cast<float>(value);
            if ((lower && double(result) > value) || (!lower && double(result) < value))
            {
                result = std::nextafter(result, lower ? -std::numeric_limits<float>::infinity()
                                                     : std::numeric_limits<float>::infinity());
            }
            return result;
        }
    } // namespace

    void TerrainInvalidation::AddFootprint(
        AZ::EntityId regionEntityId, const AZ::Aabb& regionBounds, const AZ::Aabb& footprint)
    {
        if (!regionEntityId.IsValid() || !UsableRegion(regionBounds) || !FiniteBounds(footprint))
        {
            return;
        }
        // Stamp-origin/query Z never controls overlap. Keep raw XY until spacing is available: expand before clipping.
        const AZ::Aabb extruded = AZ::Aabb::CreateFromMinMax(
            AZ::Vector3(footprint.GetMin().GetX(), footprint.GetMin().GetY(), regionBounds.GetMin().GetZ()),
            AZ::Vector3(footprint.GetMax().GetX(), footprint.GetMax().GetY(), regionBounds.GetMax().GetZ()));
        Add({ regionEntityId, regionBounds, extruded, false });
    }

    void TerrainInvalidation::AddRegion(AZ::EntityId regionEntityId, const AZ::Aabb& regionBounds)
    {
        if (regionEntityId.IsValid() && UsableRegion(regionBounds))
        {
            Add({ regionEntityId, regionBounds, regionBounds, true });
        }
    }

    void TerrainInvalidation::Add(Region region)
    {
        for (size_t index = 0; index < m_regions.size();)
        {
            const auto& other = m_regions[index];
            if (other.m_entityId != region.m_entityId || other.m_regionBounds != region.m_regionBounds)
            {
                ++index;
                continue;
            }
            if (other.m_wholeRegion)
            {
                return;
            }
            if (!region.m_wholeRegion && !CanMerge(other.m_bounds, region.m_bounds))
            {
                ++index;
                continue;
            }
            if (!region.m_wholeRegion)
            {
                region.m_bounds.AddAabb(other.m_bounds);
            }
            m_regions.erase(m_regions.begin() + index);
            index = 0; // The enlarged region can now contain/merge an earlier entry.
        }
        m_regions.push_back(AZStd::move(region));
    }

    bool TerrainInvalidation::RequiresQueryResolution() const
    {
        for (const auto& region : m_regions)
        {
            if (!region.m_wholeRegion)
            {
                return true;
            }
        }
        return false;
    }

    void TerrainInvalidation::MakeWholeRegions()
    {
        auto regions = AZStd::move(m_regions);
        m_regions.clear();
        for (const auto& region : regions)
        {
            AddRegion(region.m_entityId, region.m_regionBounds);
        }
    }

    AZStd::vector<AZ::Aabb> TerrainInvalidation::BuildRegions(float heightQueryResolution) const
    {
        TerrainInvalidation expanded;
        const bool validSpacing = std::isfinite(heightQueryResolution) && heightQueryResolution > 0.0f;
        const double margin = validSpacing ? 2.0 * double(heightQueryResolution) : 0.0;
        for (const auto& region : m_regions)
        {
            if (region.m_wholeRegion)
            {
                expanded.AddRegion(region.m_entityId, region.m_regionBounds);
                continue;
            }
            if (!validSpacing)
            {
                continue;
            }
            const auto& limits = region.m_regionBounds;
            // Double intermediates and clipping before float conversion avoid overflow for extreme valid inputs.
            const double minX = std::max(double(limits.GetMin().GetX()), double(region.m_bounds.GetMin().GetX()) - margin);
            const double minY = std::max(double(limits.GetMin().GetY()), double(region.m_bounds.GetMin().GetY()) - margin);
            const double maxX = std::min(double(limits.GetMax().GetX()), double(region.m_bounds.GetMax().GetX()) + margin);
            const double maxY = std::min(double(limits.GetMax().GetY()), double(region.m_bounds.GetMax().GetY()) + margin);
            if (minX >= maxX || minY >= maxY)
            {
                continue; // A remote footprint is no notification, never a null/global notification.
            }
            const auto bounds = AZ::Aabb::CreateFromMinMax(
                AZ::Vector3(RoundOutward(minX, true), RoundOutward(minY, true), limits.GetMin().GetZ()),
                AZ::Vector3(RoundOutward(maxX, false), RoundOutward(maxY, false), limits.GetMax().GetZ()));
            expanded.Add({ region.m_entityId, limits, bounds.GetClamped(limits), false });
        }
        AZStd::vector<AZ::Aabb> result;
        result.reserve(expanded.m_regions.size());
        for (const auto& region : expanded.m_regions)
        {
            result.push_back(region.m_bounds);
        }
        return result;
    }
} // namespace TerrainCompositor
