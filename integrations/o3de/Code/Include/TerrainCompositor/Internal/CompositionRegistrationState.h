#pragma once

#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/fixed_vector.h>
#include <AzCore/std/containers/span.h>
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

    inline constexpr RegistrationAssetRole<HeightmapStampRegistrationData, HeightmapDataSnapshot> ImageAssetRoles[] = {
        { &HeightmapStampRegistrationData::m_heightmap, DirtyHeight },
        { &HeightmapStampRegistrationData::m_surfaceIdA, DirtySurface },
        { &HeightmapStampRegistrationData::m_surfaceIdB, DirtySurface },
        { &HeightmapStampRegistrationData::m_surfaceBlend, DirtySurface },
        { &HeightmapStampRegistrationData::m_holeMask, DirtyExistence }
    };
    // Mesh registrations historically adopt newer unassigned snapshots, but never fan them out.
    inline constexpr RegistrationAssetRole<TerrainMeshCutoutRegistrationData, TerrainMeshCutoutDataSnapshot> CutoutAssetRoles[] = {
        { &TerrainMeshCutoutRegistrationData::m_mesh, DirtyExistence, true }
    };
    inline constexpr RegistrationAssetRole<TerrainMeshHeightStampRegistrationData, TerrainMeshHeightDataSnapshot> MeshHeightAssetRoles[] = {
        { &TerrainMeshHeightStampRegistrationData::m_mesh, DirtyMeshHeightAll, true }
    };

    struct RegistrationTraversal
    {
        size_t m_claimsVisited = 0;
        size_t m_fallbackRegistrationsVisited = 0;
    };

    //! Session-owned records and canonical revisions; each specialization owns an independent asset index.
    template<class Record, class Snapshot, const auto& AssetRoles, AZ::EntityId Record::* Entity>
    class IndexedRegistrationState
    {
        using AssetId = AZ::Data::AssetId;
        using TouchedAssets = AZStd::fixed_vector<AssetId, 2 * AZ_ARRAY_SIZE(AssetRoles)>;
        struct Claim
        {
            AZ::EntityId m_entityId;
            size_t m_role;
        };
        struct AssetState
        {
            Snapshot m_latest;
            AZStd::vector<Claim> m_claims;
            bool m_conflictingTie = false;
        };
    public:
        const auto& GetRegistrations() const { return m_records; }

        template<class Classify>
        void Apply(Record current, AZStd::unordered_map<AZ::EntityId, AZ::u8>& dirty, Classify classify,
            RegistrationTraversal* traversal = nullptr)
        {
            for (const auto& role : AZStd::span{ AssetRoles })
            {
                auto& snapshot = current.*role.m_snapshot;
                const auto asset = m_assets.find(snapshot.m_assetId);
                if (asset != m_assets.end() && asset->second.m_latest.m_revision > snapshot.m_revision)
                {
                    snapshot = ResolveLatest(asset->second, traversal);
                }
            }
            const AZ::EntityId id = current.*Entity;
            const auto previous = m_records.find(id);
            const auto* old = previous != m_records.end() ? &previous->second : nullptr;
            const AZ::u8 directDirty = classify(old, current);
            TouchedAssets touched;
            UpdateClaims(id, old, &current, touched, traversal);
            m_records.insert_or_assign(id, current);
            if (directDirty != 0) { dirty[id] |= directDirty; }

            // Retain source-role order and copy from the reconciled input, never the record being changed by fan-out.
            for (const auto& role : AZStd::span{ AssetRoles })
            {
                const auto& source = current.*role.m_snapshot;
                if (!source.m_assetId.IsValid()) { continue; }
                const auto found = m_assets.find(source.m_assetId);
                if (found == m_assets.end()) { continue; }
                auto& asset = found->second;
                // Every assigned claim already has the latest revision. Equal-revision payloads remain distinct.
                if (source.m_revision <= asset.m_latest.m_revision) { continue; }
                if (traversal) { traversal->m_claimsVisited += asset.m_claims.size(); }
                for (const auto& claim : asset.m_claims)
                {
                    auto& target = SnapshotFor(claim);
                    if (target.m_revision < source.m_revision)
                    {
                        target = source;
                        dirty[claim.m_entityId] |= AssetRoles[claim.m_role].m_dirty;
                    }
                }
                asset.m_latest = source;
            }
            RebuildTouched(touched, traversal);
        }

        bool Remove(AZ::EntityId id, RegistrationTraversal* traversal = nullptr)
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
        template<class> friend class MeshRegistrationStateTests;

        Snapshot& SnapshotFor(const Claim& claim)
        {
            return m_records.at(claim.m_entityId).*AssetRoles[claim.m_role].m_snapshot;
        }

        const Snapshot& ResolveLatest(const AssetState& asset, RegistrationTraversal* traversal) const
        {
            if (!asset.m_conflictingTie) { return asset.m_latest; }
            // Preserve the scan's first-maximum choice, including rehash order, for conflicting equal revisions.
            for (const auto& [id, record] : m_records)
            {
                if (traversal) { ++traversal->m_fallbackRegistrationsVisited; }
                for (const auto& role : AZStd::span{ AssetRoles })
                {
                    const auto& snapshot = record.*role.m_snapshot;
                    if (snapshot.m_assetId == asset.m_latest.m_assetId && snapshot.m_revision == asset.m_latest.m_revision)
                    {
                        return snapshot;
                    }
                }
            }
            AZ_Assert(false, "Indexed revision has no remaining claim.");
            return asset.m_latest;
        }

        void UpdateClaims(AZ::EntityId id, const Record* previous, const Record* current, TouchedAssets& touched,
            RegistrationTraversal* traversal)
        {
            for (size_t role = 0; role < AZ_ARRAY_SIZE(AssetRoles); ++role)
            {
                const auto member = AssetRoles[role].m_snapshot;
                const AssetId oldAsset = previous ? (previous->*member).m_assetId : AssetId{};
                const AssetId newAsset = current ? (current->*member).m_assetId : AssetId{};
                // Reconciled, unchanged snapshots cannot alter claim membership or revision authority.
                if (previous && current && oldAsset == newAsset &&
                    (previous->*member).m_revision == (current->*member).m_revision &&
                    PayloadsEqual(previous->*member, current->*member))
                {
                    continue;
                }
                const bool hadClaim = previous && (oldAsset.IsValid() || AssetRoles[role].m_reconcileUnassigned);
                const bool hasClaim = current && (newAsset.IsValid() || AssetRoles[role].m_reconcileUnassigned);
                for (const auto& [asset, claimed] : { AZStd::pair{ oldAsset, hadClaim }, AZStd::pair{ newAsset, hasClaim } })
                {
                    if (claimed && AZStd::find(touched.begin(), touched.end(), asset) == touched.end())
                    {
                        touched.push_back(asset);
                    }
                }
                if (hadClaim == hasClaim && oldAsset == newAsset) { continue; }
                if (hadClaim)
                {
                    auto old = m_assets.find(oldAsset);
                    auto& claims = old->second.m_claims;
                    const auto claim = AZStd::find_if(claims.begin(), claims.end(), [&](const Claim& candidate)
                    {
                        if (traversal) { ++traversal->m_claimsVisited; }
                        return candidate.m_entityId == id && candidate.m_role == role;
                    });
                    AZ_Assert(claim != claims.end(), "Registration is missing its dependent-role claim.");
                    claims.erase(claim);
                    if (claims.empty()) { m_assets.erase(old); }
                }
                if (hasClaim) { m_assets[newAsset].m_claims.push_back({ id, role }); }
            }
        }

        static bool PayloadsEqual(const Snapshot& left, const Snapshot& right)
        {
            if (left.m_status != right.m_status || left.m_data != right.m_data) { return false; }
            if constexpr (requires { left.m_validation; })
            {
                if (left.m_validation != right.m_validation) { return false; }
            }
            if constexpr (requires { left.m_diagnostics; })
            {
                if (left.m_modelValidation != right.m_modelValidation) { return false; }
                const auto& a = left.m_diagnostics;
                const auto& b = right.m_diagnostics;
                if (a.m_totalOffenseCount != b.m_totalOffenseCount || a.m_localBounds != b.m_localBounds ||
                    a.m_localOrigin != b.m_localOrigin || a.m_gridSpacing != b.m_gridSpacing ||
                    a.m_gridWidth != b.m_gridWidth || a.m_gridHeight != b.m_gridHeight) { return false; }
                return AZStd::equal(a.m_details.begin(), a.m_details.end(), b.m_details.begin(), b.m_details.end(),
                    [](const auto& x, const auto& y)
                    {
                        return x.m_validation == y.m_validation && x.m_gridX == y.m_gridX && x.m_gridY == y.m_gridY &&
                            x.m_triangleIndex == y.m_triangleIndex && x.m_relatedTriangleIndex == y.m_relatedTriangleIndex;
                    });
            }
            return true;
        }

        void RebuildTouched(const TouchedAssets& touched, RegistrationTraversal* traversal)
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
                        !PayloadsEqual(snapshot, asset.m_latest))
                    {
                        asset.m_conflictingTie = true;
                    }
                }
            }
        }

        AZStd::unordered_map<AZ::EntityId, Record> m_records;
        AZStd::unordered_map<AssetId, AssetState> m_assets;
    };

    using ImageRegistrationState = IndexedRegistrationState<HeightmapStampRegistrationData, HeightmapDataSnapshot,
        ImageAssetRoles, &HeightmapStampRegistrationData::m_stampEntityId>;
    using CutoutRegistrationState = IndexedRegistrationState<TerrainMeshCutoutRegistrationData, TerrainMeshCutoutDataSnapshot,
        CutoutAssetRoles, &TerrainMeshCutoutRegistrationData::m_cutoutEntityId>;
    using MeshHeightRegistrationState = IndexedRegistrationState<TerrainMeshHeightStampRegistrationData, TerrainMeshHeightDataSnapshot,
        MeshHeightAssetRoles, &TerrainMeshHeightStampRegistrationData::m_stampEntityId>;
}
