#pragma once

#include <AzCore/std/containers/array.h>
#include <AzCore/std/containers/unordered_set.h>
#include <AzFramework/Entity/EntityContextBus.h>
#include <TerrainCompositor/Internal/CompositionRegistrationState.h>
#include <type_traits>

namespace TerrainCompositor::Internal
{
    enum class RegistrationAdmission { Reject, Replay, Apply };
    enum class RegistrationDiagnostic { Height, Surface, Existence, Cutout, MeshHeight, Count };

    struct PreparedRegistrationDiagnostic
    {
        template<class Registration>
        PreparedRegistrationDiagnostic(RegistrationDiagnostic channel, AZ::EntityId id, const Registration& registration,
            AZStd::string reason, const char* format)
            : m_channel(channel), m_entityId(id), m_format(format)
        {
            if (registration.m_configuration.GetRuntimeOrderKey().empty() && !registration.m_identityPending)
            {
                if constexpr (requires { registration.m_heightmap; })
                    reason = "Ordering identity is unresolved/invalid. Resolve prefab aliases, or assign and persist a unique runtime key.";
                else
                    reason = "Ordering identity is unresolved/invalid. Resolve prefab aliases or assign a unique runtime key.";
            }
            m_reason = AZStd::move(reason);
        }

        RegistrationDiagnostic m_channel;
        AZ::EntityId m_entityId;
        AZStd::string m_reason;
        const char* m_format;
    };

    // Control-thread registration state. Query workers retain separate immutable publications.
    class CompositionRegistrations
    {
    public:
        template<class Registration>
        const auto& Get() const
        {
            if constexpr (std::is_same_v<Registration, HeightmapStampRegistrationData>)
                return m_images.GetRegistrations();
            else if constexpr (std::is_same_v<Registration, TerrainMeshCutoutRegistrationData>)
                return m_cutouts;
            else
            {
                static_assert(std::is_same_v<Registration, TerrainMeshHeightStampRegistrationData>);
                return m_meshHeights;
            }
        }

        template<class Registration, class Classify>
        RegistrationAdmission Register(const Registration& registration, const TerrainCompositionAddress& address,
            const AZ::Uuid& session, AZStd::unordered_map<AZ::EntityId, AZ::u8>& dirty, Classify classify)
        {
            const auto id = EntityId(registration);
            const auto admission = Admit(registration, id, address, session);
            if (admission == RegistrationAdmission::Apply)
            {
                if constexpr (std::is_same_v<Registration, HeightmapStampRegistrationData>)
                    m_images.Apply(registration, dirty, classify);
                else if constexpr (std::is_same_v<Registration, TerrainMeshCutoutRegistrationData>)
                    ApplyRegistrationState(registration, id, m_cutouts, dirty, CutoutAssetRoles, classify);
                else
                    ApplyRegistrationState(registration, id, m_meshHeights, dirty, MeshHeightAssetRoles, classify);
            }
            return admission;
        }

        template<class Registration>
        bool Remove(AZ::EntityId id, const AZ::Uuid& lease, const AZ::Uuid& requestedSession,
            const AZ::Uuid& session, AZStd::unordered_map<AZ::EntityId, AZ::u8>& dirty)
        {
            if (requestedSession != session || lease.IsNull())
                return false;
            // A delayed register must not revive even an unknown lease, in any role.
            m_retired.insert(lease);
            const auto& records = Get<Registration>();
            const auto found = records.find(id);
            if (found == records.end() || found->second.m_registrationId != lease)
                return false;
            if constexpr (std::is_same_v<Registration, HeightmapStampRegistrationData>)
            {
                dirty[id] |= DirtyAll;
                m_images.Remove(id);
                ForgetDiagnostic(RegistrationDiagnostic::Height, id);
                ForgetDiagnostic(RegistrationDiagnostic::Surface, id);
                ForgetDiagnostic(RegistrationDiagnostic::Existence, id);
            }
            else if constexpr (std::is_same_v<Registration, TerrainMeshCutoutRegistrationData>)
            {
                dirty[id] |= DirtyExistence;
                m_cutouts.erase(found);
                ForgetDiagnostic(RegistrationDiagnostic::Cutout, id);
            }
            else
            {
                dirty[id] |= DirtyMeshHeightAll;
                m_meshHeights.erase(found);
                ForgetDiagnostic(RegistrationDiagnostic::MeshHeight, id);
            }
            return true;
        }

        void RecordDiagnostic(PreparedRegistrationDiagnostic diagnostic, AZStd::vector<AZStd::string>& pending)
        {
            auto& [channel, id, reason, format] = diagnostic;
            auto& history = m_diagnostics[static_cast<size_t>(channel)];
            const auto prior = history.find(id);
            if (prior == history.end() || prior->second != reason)
            {
                if (!reason.empty())
                    pending.push_back(AZStd::string::format(format, id.ToString().c_str(), reason.c_str()));
                history.insert_or_assign(id, AZStd::move(reason));
            }
        }

        void Clear()
        {
            m_images.Clear();
            m_cutouts.clear();
            m_meshHeights.clear();
            m_retired.clear();
            for (auto& history : m_diagnostics)
                history.clear();
        }

    private:
        template<class Registration>
        static AZ::EntityId EntityId(const Registration& registration)
        {
            if constexpr (std::is_same_v<Registration, TerrainMeshCutoutRegistrationData>)
                return registration.m_cutoutEntityId;
            else
                return registration.m_stampEntityId;
        }

        template<class Registration>
        RegistrationAdmission Admit(const Registration& registration, AZ::EntityId entityId,
            const TerrainCompositionAddress& address, const AZ::Uuid& session) const
        {
            if (address.first.IsNull() || registration.m_contextId != address.first ||
                registration.m_compositionSession != session || registration.m_registrationId.IsNull() ||
                registration.m_updateRevision == 0 || m_retired.contains(registration.m_registrationId) ||
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
            const auto& registrations = Get<Registration>();
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

        void ForgetDiagnostic(RegistrationDiagnostic channel, AZ::EntityId id)
        {
            m_diagnostics[static_cast<size_t>(channel)].erase(id);
        }

        ImageRegistrationState m_images;
        AZStd::unordered_map<AZ::EntityId, TerrainMeshCutoutRegistrationData> m_cutouts;
        AZStd::unordered_map<AZ::EntityId, TerrainMeshHeightStampRegistrationData> m_meshHeights;
        AZStd::unordered_set<AZ::Uuid> m_retired;
        AZStd::array<AZStd::unordered_map<AZ::EntityId, AZStd::string>,
            static_cast<size_t>(RegistrationDiagnostic::Count)> m_diagnostics;
    };
}
