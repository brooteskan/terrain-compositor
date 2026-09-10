#pragma once

#include <AzCore/std/containers/unordered_map.h>
#include <TerrainCompositor/TerrainCompositionBus.h>

namespace TerrainCompositor::Internal
{
    constexpr AZ::u8 DirtyHeight = 1;
    constexpr AZ::u8 DirtySurface = 2;
    constexpr AZ::u8 DirtyExistence = 4;
    constexpr AZ::u8 DirtyAll = DirtyHeight | DirtySurface | DirtyExistence;
    constexpr AZ::u8 DirtyMeshHeightAll = DirtyHeight | DirtyExistence;

    template<class Registration, class Snapshot>
    struct RegistrationAssetRole
    {
        Snapshot Registration::*m_snapshot;
        AZ::u8 m_dirty;
        bool m_reconcileUnassigned = false;
    };

    constexpr RegistrationAssetRole<HeightmapStampRegistrationData, HeightmapDataSnapshot> ImageAssetRoles[] = {
        { &HeightmapStampRegistrationData::m_heightmap, DirtyHeight },
        { &HeightmapStampRegistrationData::m_surfaceIdA, DirtySurface },
        { &HeightmapStampRegistrationData::m_surfaceIdB, DirtySurface },
        { &HeightmapStampRegistrationData::m_surfaceBlend, DirtySurface },
        { &HeightmapStampRegistrationData::m_holeMask, DirtyExistence }
    };
    // Mesh registrations historically adopt newer unassigned snapshots, but never fan them out.
    constexpr RegistrationAssetRole<TerrainMeshCutoutRegistrationData, TerrainMeshCutoutDataSnapshot> CutoutAssetRoles[] = {
        { &TerrainMeshCutoutRegistrationData::m_mesh, DirtyExistence, true }
    };
    constexpr RegistrationAssetRole<TerrainMeshHeightStampRegistrationData, TerrainMeshHeightDataSnapshot> MeshHeightAssetRoles[] = {
        { &TerrainMeshHeightStampRegistrationData::m_mesh, DirtyMeshHeightAll, true }
    };

    // Value-only control state: no buses, cache access, publication, or retained component references.
    template<class Registration, class Snapshot, size_t RoleCount, class Classify>
    void ApplyRegistrationState(
        Registration current, AZ::EntityId entityId, AZStd::unordered_map<AZ::EntityId, Registration>& registrations,
        AZStd::unordered_map<AZ::EntityId, AZ::u8>& affected,
        const RegistrationAssetRole<Registration, Snapshot> (&roles)[RoleCount], Classify classify)
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
