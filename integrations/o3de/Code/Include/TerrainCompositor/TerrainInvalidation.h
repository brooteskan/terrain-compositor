#pragma once

#include <AzCore/Component/EntityId.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/std/containers/vector.h>

namespace TerrainCompositor
{
    //! Value-owned control-plane work. Region bounds belong to the publication that produced the change,
    //! not to whatever shape/reference happens to be current when the next frame is dispatched.
    class TerrainInvalidation
    {
    public:
        void AddFootprint(AZ::EntityId regionEntityId, const AZ::Aabb& regionBounds, const AZ::Aabb& footprint);
        void AddRegion(AZ::EntityId regionEntityId, const AZ::Aabb& regionBounds);
        bool IsEmpty() const { return m_regions.empty(); }
        bool RequiresQueryResolution() const;
        //! Composition shutdown removes its base as well as its stamps in all retained region contexts.
        void MakeWholeRegions();
        //! Pure geometry: expand by two height-query spacings, extrude through region Z, then clip.
        //! Whole-region work needs no spacing. Nonpositive/nonfinite spacing cannot resolve footprints.
        AZStd::vector<AZ::Aabb> BuildRegions(float heightQueryResolution) const;

    private:
        struct Region
        {
            AZ::EntityId m_entityId;
            AZ::Aabb m_regionBounds;
            AZ::Aabb m_bounds;
            bool m_wholeRegion = false;
        };
        void Add(Region region);
        AZStd::vector<Region> m_regions;
    };
} // namespace TerrainCompositor
