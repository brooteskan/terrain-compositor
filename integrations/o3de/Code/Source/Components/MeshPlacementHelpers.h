#pragma once

#include <AtomLyIntegration/CommonFeatures/Mesh/MeshComponentBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/std/containers/unordered_map.h>

namespace TerrainCompositor::Internal
{
    inline AZ::EntityId ResolveUniqueModelEntity(AZ::EntityId owner, const AZ::Data::AssetId& assetId, size_t& matchingMeshCount)
    {
        matchingMeshCount = 0;
        AZ::EntityId match;
        if (!owner.IsValid() || !assetId.IsValid())
        {
            return match;
        }

        AZStd::vector<AZ::EntityId> candidates{ owner };
        AZStd::vector<AZ::EntityId> descendants;
        AZ::TransformBus::EventResult(
            descendants, owner, &AZ::TransformInterface::GetAllDescendants);
        candidates.insert(candidates.end(), descendants.begin(), descendants.end());
        for (const AZ::EntityId candidate : candidates)
        {
            if (!AZ::Render::MeshComponentRequestBus::HasHandlers(candidate))
            {
                continue;
            }
            AZ::Data::AssetId modelAssetId;
            AZ::Render::MeshComponentRequestBus::EventResult(
                modelAssetId, candidate, &AZ::Render::MeshComponentRequestBus::Events::GetModelAssetId);
            if (modelAssetId == assetId)
            {
                match = candidate;
                ++matchingMeshCount;
            }
        }
        return matchingMeshCount == 1 ? match : AZ::EntityId{};
    }

    inline bool HideMatchingModel(AZ::EntityId candidate, const AZ::Data::AssetId& assetId,
        AZStd::unordered_map<AZ::EntityId, bool>& previousVisibility)
    {
        if (!candidate.IsValid() || !AZ::Render::MeshComponentRequestBus::HasHandlers(candidate))
        {
            return false;
        }
        AZ::Data::AssetId modelAssetId;
        AZ::Render::MeshComponentRequestBus::EventResult(
            modelAssetId, candidate, &AZ::Render::MeshComponentRequestBus::Events::GetModelAssetId);
        if (modelAssetId != assetId)
        {
            return false;
        }
        if (!previousVisibility.contains(candidate))
        {
            bool wasVisible = true;
            AZ::Render::MeshComponentRequestBus::EventResult(
                wasVisible, candidate, &AZ::Render::MeshComponentRequestBus::Events::GetVisibility);
            previousVisibility.emplace(candidate, wasVisible);
        }
        AZ::Render::MeshComponentRequestBus::Event(
            candidate, &AZ::Render::MeshComponentRequestBus::Events::SetVisibility, false);
        return true;
    }

    inline void RestoreModelVisibility(AZStd::unordered_map<AZ::EntityId, bool>& previousVisibility)
    {
        for (const auto& [entityId, wasVisible] : previousVisibility)
        {
            if (AZ::Render::MeshComponentRequestBus::HasHandlers(entityId))
            {
                AZ::Render::MeshComponentRequestBus::Event(
                    entityId, &AZ::Render::MeshComponentRequestBus::Events::SetVisibility, wasVisible);
            }
        }
        previousVisibility.clear();
    }
}
