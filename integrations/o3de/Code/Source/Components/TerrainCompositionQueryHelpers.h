#pragma once

#include <AzCore/Debug/Trace.h>
#include <TerrainCompositor/TerrainCompositionBus.h>
#include <AzCore/std/containers/span.h>
#include <AzFramework/SurfaceData/SurfaceData.h>

namespace TerrainCompositor
{
    inline bool PrepareSurfaceWeightBatch(
        AZStd::span<const AZ::Vector3> positions,
        AZStd::span<AzFramework::SurfaceData::SurfaceTagWeightList> outSurfaceWeights)
    {
        if (positions.size() != outSurfaceWeights.size())
        {
            AZ_Assert(false, "Input and surface output lists are different sizes (%zu vs %zu).",
                positions.size(), outSurfaceWeights.size());
            return false;
        }
        for (auto& weights : outSurfaceWeights)
        {
            weights.clear();
        }
        return true;
    }
} // namespace TerrainCompositor

namespace TerrainCompositor::Internal
{
    template<class Bounds, class Affected>
    void MarkMembershipChanges(const Bounds& previous, const Bounds& current, AZ::u8 dirty, Affected& affected)
    {
        for (const auto& [id, bounds] : previous)
        {
            if (!current.contains(id))
            {
                affected[id] |= dirty;
            }
        }
        for (const auto& [id, bounds] : current)
        {
            if (!previous.contains(id))
            {
                affected[id] |= dirty;
            }
        }
    }

    template<class Registrations>
    AZStd::vector<typename Registrations::mapped_type> RegistrationValues(const Registrations& registrations)
    {
        AZStd::vector<typename Registrations::mapped_type> values;
        values.reserve(registrations.size());
        for (const auto& [id, registration] : registrations)
        {
            values.push_back(registration);
        }
        return values;
    }
}
