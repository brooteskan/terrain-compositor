#pragma once

#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/fixed_vector.h>
#include <AzCore/std/algorithm.h>
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

namespace TerrainCompositor::Internal
{
    struct ImageRegistrationTraversal
    {
        size_t m_claimsVisited = 0;
        size_t m_fallbackRegistrationsVisited = 0;
    };

    //! Session-owned records and canonical image revisions; all mutations maintain their dependent-role index.
    class ImageRegistrationState
    {
        using Record = HeightmapStampRegistrationData;
        using AssetId = AZ::Data::AssetId;
        using TouchedAssets = AZStd::fixed_vector<AssetId, 2 * AZ_ARRAY_SIZE(ImageAssetRoles)>;
        struct Claim
        {
            AZ::EntityId m_entityId;
            size_t m_role;
        };
        struct AssetState
        {
            HeightmapDataSnapshot m_latest;
            AZStd::vector<Claim> m_claims;
            bool m_conflictingTie = false;
        };
    public:
        const auto& GetRegistrations() const { return m_records; }

        template<class Classify>
        void Apply(Record current, AZStd::unordered_map<AZ::EntityId, AZ::u8>& dirty, Classify classify,
            ImageRegistrationTraversal* traversal = nullptr)
        {
            for (const auto& role : ImageAssetRoles)
            {
                auto& snapshot = current.*role.m_snapshot;
                const auto asset = m_assets.find(snapshot.m_assetId);
                if (asset != m_assets.end() && asset->second.m_latest.m_revision > snapshot.m_revision)
                {
                    snapshot = ResolveLatest(asset->second, traversal);
                }
            }
            const AZ::EntityId id = current.m_stampEntityId;
            const auto previous = m_records.find(id);
            const auto* old = previous != m_records.end() ? &previous->second : nullptr;
            const AZ::u8 directDirty = classify(old, current);
            TouchedAssets touched;
            UpdateClaims(id, old, &current, touched, traversal);
            m_records.insert_or_assign(id, current);
            if (directDirty != 0) { dirty[id] |= directDirty; }

            // Retain source-role order and copy from the reconciled input, never the record being changed by fan-out.
            for (const auto& role : ImageAssetRoles)
            {
                const auto& source = current.*role.m_snapshot;
                const auto found = m_assets.find(source.m_assetId);
                if (found == m_assets.end()) { continue; }
                auto& asset = found->second;
                // Every retained claim already has the latest revision. Equal-revision payloads remain distinct.
                if (source.m_revision <= asset.m_latest.m_revision) { continue; }
                if (traversal) { traversal->m_claimsVisited += asset.m_claims.size(); }
                for (const auto& claim : asset.m_claims)
                {
                    auto& target = SnapshotFor(claim);
                    if (target.m_revision < source.m_revision)
                    {
                        target = source;
                        dirty[claim.m_entityId] |= ImageAssetRoles[claim.m_role].m_dirty;
                    }
                }
                asset.m_latest = source;
            }
            RebuildTouched(touched, traversal);
        }

        bool Remove(AZ::EntityId id, ImageRegistrationTraversal* traversal = nullptr)
        {
            const auto found = m_records.find(id);
            if (found == m_records.end()) { return false; }
            TouchedAssets touched;
            UpdateClaims(id, &found->second, nullptr, touched, traversal);
            m_records.erase(found);
            RebuildTouched(touched, traversal);
            return true;
        }

        void Clear()
        {
            m_records.clear();
            m_assets.clear();
        }

    private:
        friend class ImageRegistrationStateTests;

        HeightmapDataSnapshot& SnapshotFor(const Claim& claim)
        {
            return m_records.at(claim.m_entityId).*ImageAssetRoles[claim.m_role].m_snapshot;
        }

        const HeightmapDataSnapshot& ResolveLatest(const AssetState& asset, ImageRegistrationTraversal* traversal) const
        {
            if (!asset.m_conflictingTie) { return asset.m_latest; }
            // Preserve the scan's first-maximum choice, including rehash order, for conflicting equal revisions.
            for (const auto& [id, record] : m_records)
            {
                if (traversal) { ++traversal->m_fallbackRegistrationsVisited; }
                for (const auto& role : ImageAssetRoles)
                {
                    const auto& snapshot = record.*role.m_snapshot;
                    if (snapshot.m_assetId == asset.m_latest.m_assetId && snapshot.m_revision == asset.m_latest.m_revision)
                    {
                        return snapshot;
                    }
                }
            }
            AZ_Assert(false, "Indexed image revision has no remaining claim.");
            return asset.m_latest;
        }

        void UpdateClaims(AZ::EntityId id, const Record* previous, const Record* current, TouchedAssets& touched,
            ImageRegistrationTraversal* traversal)
        {
            for (size_t role = 0; role < AZ_ARRAY_SIZE(ImageAssetRoles); ++role)
            {
                const auto member = ImageAssetRoles[role].m_snapshot;
                const AssetId oldAsset = previous ? (previous->*member).m_assetId : AssetId{};
                const AssetId newAsset = current ? (current->*member).m_assetId : AssetId{};
                for (const auto& asset : { oldAsset, newAsset })
                {
                    if (asset.IsValid() && AZStd::find(touched.begin(), touched.end(), asset) == touched.end())
                    {
                        touched.push_back(asset);
                    }
                }
                if (oldAsset == newAsset) { continue; }
                if (oldAsset.IsValid())
                {
                    auto old = m_assets.find(oldAsset);
                    auto& claims = old->second.m_claims;
                    const auto claim = AZStd::find_if(claims.begin(), claims.end(), [&](const Claim& candidate)
                    {
                        if (traversal) { ++traversal->m_claimsVisited; }
                        return candidate.m_entityId == id && candidate.m_role == role;
                    });
                    AZ_Assert(claim != claims.end(), "Image registration is missing its dependent-role claim.");
                    claims.erase(claim);
                    if (claims.empty()) { m_assets.erase(old); }
                }
                if (newAsset.IsValid()) { m_assets[newAsset].m_claims.push_back({ id, role }); }
            }
        }

        void RebuildTouched(const TouchedAssets& touched, ImageRegistrationTraversal* traversal)
        {
            for (const auto& id : touched)
            {
                const auto found = m_assets.find(id);
                if (found == m_assets.end()) { continue; }
                auto& asset = found->second;
                asset.m_latest = SnapshotFor(asset.m_claims.front());
                asset.m_conflictingTie = false;
                if (traversal) { traversal->m_claimsVisited += asset.m_claims.size(); }
                for (size_t index = 1; index < asset.m_claims.size(); ++index)
                {
                    const auto& snapshot = SnapshotFor(asset.m_claims[index]);
                    if (snapshot.m_revision > asset.m_latest.m_revision)
                    {
                        asset.m_latest = snapshot;
                        asset.m_conflictingTie = false;
                    }
                    else if (snapshot.m_revision == asset.m_latest.m_revision &&
                        (snapshot.m_status != asset.m_latest.m_status || snapshot.m_data != asset.m_latest.m_data))
                    {
                        asset.m_conflictingTie = true;
                    }
                }
            }
        }

        AZStd::unordered_map<AZ::EntityId, Record> m_records;
        AZStd::unordered_map<AssetId, AssetState> m_assets;
    };
}
