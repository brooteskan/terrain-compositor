#pragma once

#include <AzCore/Math/Vector3.h>

namespace Terrain
{
    struct TerrainRaycastCandidate
    {
        AZ::Vector3 m_normal = AZ::Vector3::CreateZero();
        float m_distance = 0.0f;
        bool m_hit = false;
    };

    //! Validate both cell-triangle intersections before selecting the closest.
    //! The predicate receives segment distance and performs the exact existence
    //! query at the corresponding intersection point.
    template<class ExistencePredicate>
    const TerrainRaycastCandidate* SelectNearestExistingTerrainCandidate(
        const TerrainRaycastCandidate& first,
        const TerrainRaycastCandidate& second,
        ExistencePredicate&& exists)
    {
        const bool firstExists = first.m_hit && exists(first.m_distance);
        const bool secondExists = second.m_hit && exists(second.m_distance);
        if (firstExists && (!secondExists || first.m_distance < second.m_distance))
        {
            return &first;
        }
        return secondExists ? &second : nullptr;
    }
} // namespace Terrain
