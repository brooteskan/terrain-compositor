#pragma once

#include <TerrainCompositor/Internal/CompositionRegistrationState.h>

namespace TerrainCompositor::Internal
{
    // Original production scan retained as an independent behavioral and traversal reference.
    template<class Registration, class Snapshot, size_t RoleCount, class Classify>
    void ApplyRegistrationState(
        Registration current, AZ::EntityId entityId, AZStd::unordered_map<AZ::EntityId, Registration>& registrations,
        AZStd::unordered_map<AZ::EntityId, AZ::u8>& affected,
        const RegistrationAssetRole<Registration, Snapshot> (&roles)[RoleCount], Classify classify, size_t* registrationsVisited = nullptr)
    {
        // A placement edit may arrive before its subscriber receives a peer's newer asset revision.
        for (const auto& role : roles)
        {
            auto& snapshot = current.*role.m_snapshot;
            if (!snapshot.m_assetId.IsValid() && !role.m_reconcileUnassigned)
            {
                continue;
            }
            for (const auto& [id, other] : registrations)
            {
                if (registrationsVisited) { ++*registrationsVisited; }
                for (const auto& peerRole : roles)
                {
                    const auto& candidate = other.*peerRole.m_snapshot;
                    if (candidate.m_assetId == snapshot.m_assetId && candidate.m_revision > snapshot.m_revision)
                    {
                        snapshot = candidate;
                    }
                }
            }
        }
        const auto previous = registrations.find(entityId);
        const AZ::u8 directDirty = classify(previous != registrations.end() ? &previous->second : nullptr, current);
        registrations.insert_or_assign(entityId, current);
        if (directDirty != 0)
        {
            affected[entityId] |= directDirty;
        }

        // Copy from the reconciled input, not the stored record being updated by this fan-out.
        // Canonical asset IDs also carry loading/failure transitions across every dependent role.
        for (const auto& role : roles)
        {
            const auto& source = current.*role.m_snapshot;
            if (!source.m_assetId.IsValid())
            {
                continue;
            }
            for (auto& [id, other] : registrations)
            {
                if (registrationsVisited) { ++*registrationsVisited; }
                for (const auto& targetRole : roles)
                {
                    auto& target = other.*targetRole.m_snapshot;
                    if (target.m_assetId == source.m_assetId && target.m_revision < source.m_revision)
                    {
                        target = source;
                        affected[id] |= targetRole.m_dirty;
                    }
                }
            }
        }
    }
}
