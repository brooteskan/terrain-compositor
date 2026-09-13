#pragma once
#include <AzCore/Math/Aabb.h>
#include <AzCore/std/containers/fixed_vector.h>
#include <AzCore/std/containers/span.h>
#include <cmath>

namespace TerrainCompositor
{
    //! A complete explicit batch includes normal halos and sparse CLOD positions.
    //! Bound actual float coordinates, with inclusive edges and conservative
    //! invalid-input fallback. Pointers live only inside the owning query call.
    inline AZ::Aabb TerrainBatchBounds(AZStd::span<const AZ::Vector3> positions)
    {
        auto bounds = AZ::Aabb::CreateNull();
        for (const auto& p : positions)
        {
            if (!std::isfinite(p.GetX()) || !std::isfinite(p.GetY())) return AZ::Aabb::CreateNull();
            bounds.AddPoint(AZ::Vector3(p.GetX(), p.GetY(), 0));
        }
        return bounds;
    }

    template<class Record>
    struct TerrainBatchCandidates
    {
        AZStd::fixed_vector<const Record*, 64> m_records;
        bool m_full = false;
        TerrainBatchCandidates(AZStd::span<const Record> records, const AZ::Aabb& region)
        {
            for (const auto& record : records)
            {
                const auto& bounds = record.GetWorldBounds();
                if (!region.IsValid() || !bounds.IsValid() ||
                    (bounds.GetMin().GetX() <= region.GetMax().GetX() && bounds.GetMax().GetX() >= region.GetMin().GetX() &&
                     bounds.GetMin().GetY() <= region.GetMax().GetY() && bounds.GetMax().GetY() >= region.GetMin().GetY()))
                {
                    if (m_records.size() == m_records.capacity()) { m_full = true; return; }
                    m_records.push_back(&record);
                }
            }
        }
        AZStd::span<const Record* const> Selected() const { return { m_records.data(), m_records.size() }; }
    };
}
