#pragma once

#include <AzCore/Debug/Trace.h>
#include <TerrainCompositor/TerrainCompositionBus.h>
#include <AzFramework/Entity/EntityContextBus.h>
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
    enum class RegistrationAdmission { Reject, Replay, Apply };

    template<class Registration, class Registrations, class Retired>
    RegistrationAdmission AdmitRegistration(
        const Registration& registration, AZ::EntityId entityId, const Registrations& registrations,
        const TerrainCompositionAddress& address, const AZ::Uuid& session, const Retired& retired)
    {
        if (address.first.IsNull() || registration.m_contextId != address.first ||
            registration.m_compositionSession != session || registration.m_registrationId.IsNull() ||
            registration.m_updateRevision == 0 || retired.contains(registration.m_registrationId) ||
            registration.m_configuration.m_targetCompositionEntityId != address.second)
        {
            return RegistrationAdmission::Reject;
        }
        AzFramework::EntityContextId context{};
        AzFramework::EntityIdContextQueryBus::EventResult(
            context, entityId, &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
        if (!entityId.IsValid() || context != address.first)
        {
            return RegistrationAdmission::Reject;
        }
        if (const auto previous = registrations.find(entityId); previous != registrations.end())
        {
            if (previous->second.m_registrationId != registration.m_registrationId ||
                previous->second.m_updateRevision > registration.m_updateRevision)
            {
                return RegistrationAdmission::Reject;
            }
            if (previous->second.m_updateRevision == registration.m_updateRevision)
            {
                return RegistrationAdmission::Replay;
            }
        }
        return RegistrationAdmission::Apply;
    }

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
