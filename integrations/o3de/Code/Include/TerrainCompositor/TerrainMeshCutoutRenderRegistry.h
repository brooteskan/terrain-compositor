#pragma once

#include <AzCore/Interface/Interface.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Uuid.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/function/function_template.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>

namespace TerrainCompositor
{
    struct TerrainRenderGeometryQuery
    {
        AZ::Aabb m_regionBounds = AZ::Aabb::CreateNull();
        AZStd::function<bool(const AZ::Vector3&)> m_getTerrainExists;
        AZStd::function<float(const AZ::Vector3&)> m_getHeight;
        //! Optional bulk equivalent of both scalar callbacks. Positions retain the
        //! ordinary terrain query's surface Z, including its collision fallback.
        AZStd::function<void(AZStd::span<const AZ::Vector3>, AZStd::span<float>, AZStd::span<bool>)> m_getGeometry;
    };

    struct TerrainMeshCutoutRenderSnapshot
    {
        AZ::u64 m_revision = 0;
        AZStd::vector<PreparedTerrainMeshCutout> m_cutouts;
        AZStd::vector<PreparedTerrainMeshHeightGap> m_meshHeightGaps;
        //! Per-composition revisions correlate this scene aggregation with the
        //! QueryState that produced each retained contribution.
        AZStd::vector<AZStd::pair<AZ::Uuid, AZ::u64>> m_compositionGenerations;
        //! Value-owned queries retain immutable composition state, so terrain worker
        //! jobs never call a component.
        AZStd::vector<TerrainRenderGeometryQuery> m_renderGeometryQueries;
    };
    using TerrainMeshCutoutRenderSnapshotPtr = std::shared_ptr<const TerrainMeshCutoutRenderSnapshot>;

    inline bool TryGetTerrainRenderGeometryHeight(
        const TerrainMeshCutoutRenderSnapshot& snapshot, const AZ::Vector3& position, float& height)
    {
        for (const auto& query : snapshot.m_renderGeometryQueries)
        {
            const auto& bounds = query.m_regionBounds;
            if (query.m_getHeight && bounds.IsValid() && position.GetX() >= bounds.GetMin().GetX() &&
                position.GetX() <= bounds.GetMax().GetX() && position.GetY() >= bounds.GetMin().GetY() &&
                position.GetY() <= bounds.GetMax().GetY())
            {
                height = query.m_getHeight(position);
                return true;
            }
        }
        return false;
    }

    //! Resolve the terrain-existence value used to build render mesh topology.
    //! Terrain regions are heightfields, so ownership is determined in XY. The
    //! sampled Z value can be outside the region bounds when the ordinary terrain
    //! query reports a collision-only hole; using a three-dimensional containment
    //! test there would leak that coarse hole into render geometry.
    inline bool TryGetTerrainRenderGeometryExists(
        const TerrainMeshCutoutRenderSnapshot& snapshot, const AZ::Vector3& position, bool& terrainExists)
    {
        for (const auto& query : snapshot.m_renderGeometryQueries)
        {
            const AZ::Aabb& bounds = query.m_regionBounds;
            if (query.m_getTerrainExists && bounds.IsValid() && position.GetX() >= bounds.GetMin().GetX() &&
                position.GetX() <= bounds.GetMax().GetX() && position.GetY() >= bounds.GetMin().GetY() &&
                position.GetY() <= bounds.GetMax().GetY())
            {
                terrainExists = query.m_getTerrainExists(position);
                return true;
            }
        }
        return false;
    }

    //! Overlay retained render geometry on ordinary terrain results. Unowned
    //! samples stay untouched. Contiguous runs with the same height/existence
    //! owner use one bulk call; legacy or independently owned callbacks keep their
    //! original first-match semantics (including inclusive XY boundaries).
    inline void ApplyTerrainRenderGeometry(
        const TerrainMeshCutoutRenderSnapshot& snapshot,
        AZStd::span<const AZ::Vector3> positions,
        AZStd::span<float> heights,
        AZStd::span<bool> terrainExists)
    {
        if (positions.size() != heights.size() || positions.size() != terrainExists.size())
        {
            AZ_Assert(false, "Render geometry input/output lists have different sizes.");
            return;
        }
        using Owners = AZStd::pair<const TerrainRenderGeometryQuery*, const TerrainRenderGeometryQuery*>;
        const auto findOwners = [&snapshot](const AZ::Vector3& position)
        {
            Owners owners{ nullptr, nullptr };
            for (const auto& query : snapshot.m_renderGeometryQueries)
            {
                const auto& bounds = query.m_regionBounds;
                if (!bounds.IsValid() ||
                    !(position.GetX() >= bounds.GetMin().GetX() && position.GetX() <= bounds.GetMax().GetX() &&
                      position.GetY() >= bounds.GetMin().GetY() && position.GetY() <= bounds.GetMax().GetY()))
                {
                    continue;
                }
                if (!owners.first && query.m_getHeight)
                    owners.first = &query;
                if (!owners.second && query.m_getTerrainExists)
                    owners.second = &query;
                if (owners.first && owners.second)
                    break;
            }
            return owners;
        };
        size_t start = 0;
        Owners owners = positions.empty() ? Owners{} : findOwners(positions.front());
        while (start < positions.size())
        {
            size_t end = start + 1;
            Owners nextOwners{};
            for (; end < positions.size(); ++end)
            {
                nextOwners = findOwners(positions[end]);
                if (nextOwners != owners)
                    break;
            }
            if (owners.first && owners.first == owners.second && owners.first->m_getGeometry)
            {
                owners.first->m_getGeometry(
                    positions.subspan(start, end - start), heights.subspan(start, end - start), terrainExists.subspan(start, end - start));
            }
            else
            {
                for (size_t index = start; index < end; ++index)
                {
                    if (owners.second)
                        terrainExists[index] = owners.second->m_getTerrainExists(positions[index]);
                    if (owners.first)
                        heights[index] = owners.first->m_getHeight(positions[index]);
                }
            }
            start = end;
            owners = nextOwners;
        }
    }

    //! Returns true when the terrain heightfield cell is conservatively removed
    //! by an active mesh-height gap. The retained snapshot and activation keep an
    //! entire asynchronous collider request on one immutable publication.
    inline bool IsTerrainHeightfieldCellRemoved(
        const TerrainMeshCutoutRenderSnapshot& snapshot,
        const TerrainMeshHeightGapActivationPtr& activation,
        const AZ::Vector2& worldCellMinimum,
        const AZ::Vector2& gridSpacing)
    {
        const AZStd::span<const PreparedTerrainMeshHeightGap> admitted =
            activation && activation->m_revision == snapshot.m_revision
            ? AZStd::span<const PreparedTerrainMeshHeightGap>(activation->m_gaps)
            : AZStd::span<const PreparedTerrainMeshHeightGap>{};
        for (const PreparedTerrainMeshHeightGap& gap : snapshot.m_meshHeightGaps)
        {
            const bool gapAdmitted = !gap.m_affectTerrainRendering || AZStd::any_of(
                admitted.begin(), admitted.end(), [&gap](const PreparedTerrainMeshHeightGap& candidate)
                {
                    return gap.m_data == candidate.m_data && gap.m_compositionSession == candidate.m_compositionSession &&
                        gap.m_entityId == candidate.m_entityId && gap.m_originX == candidate.m_originX &&
                        gap.m_originY == candidate.m_originY && gap.m_inverseScale == candidate.m_inverseScale &&
                        gap.m_cosYaw == candidate.m_cosYaw && gap.m_sinYaw == candidate.m_sinYaw &&
                        gap.m_affectTerrainRendering == candidate.m_affectTerrainRendering;
                });
            const auto& cells = gap.m_collisionCells;
            if (!gapAdmitted || !gap.m_affectTerrainCollisionQueries || !cells || cells->m_cells.empty() ||
                !worldCellMinimum.IsFinite() || !gridSpacing.IsFinite() ||
                gridSpacing.GetX() != cells->m_gridSpacing || gridSpacing.GetY() != cells->m_gridSpacing)
            {
                continue;
            }
            const double spacing = cells->m_gridSpacing;
            const double x = double(worldCellMinimum.GetX()) / spacing;
            const double y = double(worldCellMinimum.GetY()) / spacing;
            if (!std::isfinite(x) || !std::isfinite(y) || x < double(std::numeric_limits<AZ::s64>::min()) ||
                x > double(std::numeric_limits<AZ::s64>::max()) || y < double(std::numeric_limits<AZ::s64>::min()) ||
                y > double(std::numeric_limits<AZ::s64>::max()))
            {
                continue;
            }
            const TerrainHeightfieldCellAddress address{
                static_cast<AZ::s64>(std::llround(x)), static_cast<AZ::s64>(std::llround(y)) };
            if (AZStd::binary_search(
                    cells->m_cells.begin(), cells->m_cells.end(), address,
                    [](const TerrainHeightfieldCellAddress& left, const TerrainHeightfieldCellAddress& right)
                    {
                        return left.m_y < right.m_y || (left.m_y == right.m_y && left.m_x < right.m_x);
                    }))
            {
                return true;
            }
        }
        return false;
    }

    struct TerrainMeshCutoutRenderChannel
    {
        std::mutex m_publicationMutex;
        std::atomic<TerrainMeshCutoutRenderSnapshotPtr> m_snapshot{ std::make_shared<const TerrainMeshCutoutRenderSnapshot>() };
        std::atomic<TerrainMeshHeightGapActivationPtr> m_activation{ std::make_shared<const TerrainMeshHeightGapActivation>() };
    };
    using TerrainMeshCutoutRenderChannelPtr = std::shared_ptr<TerrainMeshCutoutRenderChannel>;

    //! Control-thread registry which publishes one value-owned immutable snapshot
    //! to Atom workers.
    class TerrainMeshCutoutRenderRegistry
    {
    public:
        AZ_RTTI(TerrainMeshCutoutRenderRegistry, "{C7951998-E326-43BA-A4ED-A558F8EA28C1}");

        TerrainMeshCutoutRenderRegistry();
        virtual ~TerrainMeshCutoutRenderRegistry();

        //! Virtual so the Terrain module can consume the cross-module AZ::Interface
        //! without linking a consuming game Gem.
        virtual TerrainMeshCutoutRenderChannelPtr AcquireSceneChannel(const void* sceneKey);
        //! Read-only lookup for editor diagnostics. Unlike AcquireSceneChannel,
        //! this never creates publication state.
        TerrainMeshCutoutRenderChannelPtr FindSceneChannel(const void* sceneKey) const;
        bool Publish(
            const void* sceneKey,
            const AZ::Uuid& compositionSession,
            AZStd::vector<PreparedTerrainMeshCutout> cutouts,
            TerrainRenderGeometryQuery renderGeometryQuery = {},
            AZ::u64 compositionRevision = 0,
            AZStd::vector<PreparedTerrainMeshHeightGap> meshHeightGaps = {});
        void Remove(const AZ::Uuid& compositionSession);
        bool ActivateGaps(const void* sceneKey, const TerrainMeshCutoutRenderSnapshotPtr& expected,
            AZStd::vector<PreparedTerrainMeshHeightGap> admitted);
        void ClearGapActivation(const void* sceneKey);

    private:
        struct CompositionEntry
        {
            const void* m_sceneKey = nullptr;
            AZ::u64 m_compositionRevision = 0;
            AZStd::vector<PreparedTerrainMeshCutout> m_cutouts;
            AZStd::vector<PreparedTerrainMeshHeightGap> m_meshHeightGaps;
            TerrainRenderGeometryQuery m_renderGeometryQuery;
        };

        void RebuildSnapshot(const void* sceneKey);

        mutable std::mutex m_updateMutex;
        AZStd::unordered_map<AZ::Uuid, CompositionEntry> m_byComposition;
        AZStd::unordered_map<const void*, TerrainMeshCutoutRenderChannelPtr> m_sceneChannels;
        AZ::u64 m_revision = 0;
    };
} // namespace TerrainCompositor
