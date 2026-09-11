#pragma once

#include <TerrainCompositor/Internal/CompositionRegistrationState.h>
#include <TerrainCompositor/Internal/PendingCompositionInvalidation.h>
#include <TerrainCompositor/SurfaceStampSampling.h>
#include "PublicationState.h"

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

    inline AZ::Aabb IntersectFootprintsXY(const AZ::Aabb& left, const AZ::Aabb& right)
    {
        if (!left.IsValid() || !right.IsValid())
        {
            return AZ::Aabb::CreateNull();
        }
        const AZ::Vector3 minimum(
            AZStd::max(left.GetMin().GetX(), right.GetMin().GetX()), AZStd::max(left.GetMin().GetY(), right.GetMin().GetY()), 0.0f);
        const AZ::Vector3 maximum(
            AZStd::min(left.GetMax().GetX(), right.GetMax().GetX()), AZStd::min(left.GetMax().GetY(), right.GetMax().GetY()), 0.0f);
        return minimum.GetX() <= maximum.GetX() && minimum.GetY() <= maximum.GetY() ? AZ::Aabb::CreateFromMinMax(minimum, maximum)
                                                                                    : AZ::Aabb::CreateNull();
    }

    // Pure value transformation of accepted publication work. Fold into the existing queues:
    // TerrainInvalidation coalescing is order-sensitive, so separately merged plans are not equivalent.
    // State supplies the query publication's identity, source, region, palette and geometry fields.
    template<class State>
    PendingCompositionInvalidation PlanCompositionInvalidation(const State& previous, const State& replacement,
        const PublicationFootprints& currentFootprints, AZStd::unordered_map<AZ::EntityId, AZ::u8> affected,
        PendingCompositionInvalidation pending)
    {
        const PublicationFootprints oldFootprints(previous);
        MarkMembershipChanges(oldFootprints.m_height, currentFootprints.m_height, DirtyHeight, affected);
        MarkMembershipChanges(oldFootprints.m_surface, currentFootprints.m_surface, DirtySurface, affected);
        MarkMembershipChanges(oldFootprints.m_existence, currentFootprints.m_existence, DirtyExistence, affected);
        MarkMembershipChanges(oldFootprints.m_gapQueries, currentFootprints.m_gapQueries, DirtyExistence, affected);
        MarkMembershipChanges(oldFootprints.m_gapRendering, currentFootprints.m_gapRendering, DirtyExistence, affected);
        const bool regionChanged = previous.m_sourceEntityId != replacement.m_sourceEntityId ||
            previous.m_regionEntityId != replacement.m_regionEntityId || previous.m_regionBounds != replacement.m_regionBounds;
        if (regionChanged)
        {
            // Mapping/reference changes affect the base and every stamp, including an
            // empty stamp set. Keep old/new region contexts separate; the old shape may
            // already be gone when we dispatch.
            pending.m_heightTerrain.AddRegion(previous.m_regionEntityId, previous.m_regionBounds);
            pending.m_heightTerrain.AddRegion(replacement.m_regionEntityId, replacement.m_regionBounds);
        }
        if (regionChanged ||
            !SurfacePalettesEqual(previous.m_surfacePalette, replacement.m_surfacePalette))
        {
            pending.m_surfaceTerrain.AddRegion(previous.m_regionEntityId, previous.m_regionBounds);
            pending.m_surfaceTerrain.AddRegion(replacement.m_regionEntityId, replacement.m_regionBounds);
        }
        const auto visitFootprints = [&](AZ::EntityId id, auto member, auto visit)
        {
            using Side = AZStd::pair<const State*, const PublicationFootprints*>;
            for (const auto& [state, footprints] : { Side{ &previous, &oldFootprints }, Side{ &replacement, &currentFootprints } })
            {
                const auto& bounds = footprints->*member;
                if (const auto found = bounds.find(id); found != bounds.end())
                    visit(*state, found->second, *footprints);
            }
        };
        const auto queueSurface = [&pending](const auto& state, const auto& bounds, const auto&)
        {
            pending.m_surfaceTerrain.AddFootprint(state.m_regionEntityId, state.m_regionBounds, bounds);
        };
        const auto queueHeight = [&pending](const auto& state, const auto& bounds, const auto&)
        {
            pending.m_heightTerrain.AddFootprint(state.m_regionEntityId, state.m_regionBounds, bounds);
        };
        for (const auto& [id, dirty] : affected)
        {
            if ((dirty & DirtyHeight) != 0)
            {
                HeightmapStampFootprintChange change;
                change.m_stampEntityId = id;
                change.m_address = replacement.m_address;
                change.m_compositionSession = replacement.m_session;
                change.m_snapshotRevision = replacement.m_revision;
                if (const auto old = oldFootprints.m_height.find(id); old != oldFootprints.m_height.end())
                {
                    change.m_previousBounds = old->second;
                    change.m_previousRegionEntityId = previous.m_regionEntityId;
                    change.m_previousRegionBounds = previous.m_regionBounds;
                }
                if (const auto current = currentFootprints.m_height.find(id); current != currentFootprints.m_height.end())
                {
                    change.m_currentBounds = current->second;
                    change.m_currentRegionEntityId = replacement.m_regionEntityId;
                    change.m_currentRegionBounds = replacement.m_regionBounds;
                }
                if (change.m_previousBounds.IsValid() || change.m_currentBounds.IsValid())
                {
                    pending.AddHeightChange(change);
                }
                visitFootprints(id, &PublicationFootprints::m_height,
                    [&](const auto& state, const auto& heightBounds, const auto& footprints)
                    {
                        if (!heightBounds.IsValid())
                            return;
                        for (const auto& [cutoutId, cutoutBounds] : footprints.m_cutouts)
                        {
                            const AZ::Aabb overlap = IntersectFootprintsXY(heightBounds, cutoutBounds);
                            if (overlap.IsValid())
                                pending.m_surfaceTerrain.AddFootprint(state.m_regionEntityId, state.m_regionBounds, overlap);
                        }
                    });
            }
            if ((dirty & DirtySurface) != 0)
                visitFootprints(id, &PublicationFootprints::m_surface, queueSurface);
            if ((dirty & DirtyExistence) != 0)
            {
                visitFootprints(id, &PublicationFootprints::m_existence,
                    [&](const auto& state, const auto& bounds, const auto& footprints)
                    {
                        queueHeight(state, bounds, footprints);
                        queueSurface(state, bounds, footprints);
                    });
                visitFootprints(id, &PublicationFootprints::m_gapQueries, queueHeight);
                visitFootprints(id, &PublicationFootprints::m_gapRendering, queueSurface);
            }
        }
        return pending;
    }
}
