#include "HeightmapStampIdentityProcessor.h"
#include "EditorHeightmapStampComponent.h"
#include "EditorTerrainMeshCutoutComponent.h"
#include "EditorTerrainMeshHeightStampComponent.h"

#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/algorithm.h>
#include <AzToolsFramework/ToolsComponents/GenericComponentWrapper.h>
#include <TerrainCompositor/Components/HeightmapStampComponent.h>
#include <TerrainCompositor/Components/TerrainMeshCutoutComponent.h>
#include <TerrainCompositor/Components/TerrainMeshHeightStampComponent.h>
#include <TerrainCompositor/HeightmapStampIdentity.h>

namespace TerrainCompositor
{
    AZStd::string ResolvePrefabStampOrderKey(const AzToolsFramework::Prefab::Instance& root, AZ::EntityId entityId)
    {
        // Direct instance traversal also works for OneToMany conversion documents, which are deliberately
        // absent from the editor's global entity-to-instance mapper. No active entity buses are needed.
        auto [instance, entityAlias] = root.FindInstanceAndAlias(entityId);
        if (!instance || entityAlias.empty())
        {
            return {};
        }
        AZStd::vector<AZStd::string> aliases;
        while (instance != &root)
        {
            aliases.push_back(instance->GetInstanceAlias());
            const auto parent = instance->GetParentInstance();
            if (!parent)
            {
                return {};
            }
            instance = &parent->get();
        }
        AZStd::reverse(aliases.begin(), aliases.end());
        return MakePrefabStampOrderKey(AZStd::span<const AZStd::string>(aliases.data(), aliases.size()), entityAlias);
    }

    void HeightmapStampIdentityProcessor::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            // Bump when identity encoding/export semantics change: the prefab pipeline fingerprints this version.
            serialize->Class<HeightmapStampIdentityProcessor, AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessor>()->Version(
                5);
        }
    }

    void HeightmapStampIdentityProcessor::Process(AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessorContext& context)
    {
        if (!context.HasCompletedSuccessfully())
        {
            return;
        }
        context.ListPrefabs(
            [&context](AzToolsFramework::Prefab::PrefabConversionUtils::PrefabDocument& document)
            {
                auto& root = document.GetInstance();
                AZStd::unordered_map<AZ::EntityId, AZStd::unordered_map<AZStd::string, AZ::EntityId>> claims;
                root.GetAllEntitiesInHierarchy(
                    [&](AZStd::unique_ptr<AZ::Entity>& entity)
                    {
                        if (!entity)
                        {
                            return true;
                        }
                        for (auto* component : entity->GetComponents())
                        {
                            if (const auto* wrapper = azrtti_cast<AzToolsFramework::Components::GenericComponentWrapper*>(component))
                            {
                                component = wrapper->GetTemplate();
                            }
                            auto* stamp = azrtti_cast<HeightmapStampComponent*>(component);
                            auto* editorStamp = azrtti_cast<EditorHeightmapStampComponent*>(component);
                            auto* cutout = azrtti_cast<TerrainMeshCutoutComponent*>(component);
                            auto* editorCutout = azrtti_cast<EditorTerrainMeshCutoutComponent*>(component);
                            auto* meshHeight = azrtti_cast<TerrainMeshHeightStampComponent*>(component);
                            auto* editorMeshHeight = azrtti_cast<EditorTerrainMeshHeightStampComponent*>(component);
                            if (!stamp && !editorStamp && !cutout && !editorCutout && !meshHeight && !editorMeshHeight)
                            {
                                continue;
                            }
                            AZ::EntityId targetComposition;
                            if (editorStamp)
                                targetComposition = editorStamp->GetStampConfiguration().m_targetCompositionEntityId;
                            else if (stamp)
                                targetComposition = stamp->GetStampConfiguration().m_targetCompositionEntityId;
                            else if (editorCutout)
                                targetComposition = editorCutout->GetCutoutConfiguration().m_targetCompositionEntityId;
                            else if (cutout)
                                targetComposition = cutout->GetCutoutConfiguration().m_targetCompositionEntityId;
                            else if (editorMeshHeight)
                                targetComposition = editorMeshHeight->GetStampConfiguration().m_targetCompositionEntityId;
                            else
                                targetComposition = meshHeight->GetStampConfiguration().m_targetCompositionEntityId;
                            const auto key = ResolvePrefabStampOrderKey(root, entity->GetId());
                            if (!IsValidStampOrderKey(key))
                            {
                                AZ_Error(
                                    "HeightmapStampExport",
                                    false,
                                    "Cannot resolve stamp '%s' in prefab '%s'; export stopped. No runtime-ID or random-key fallback is "
                                    "allowed.",
                                    entity->GetName().c_str(),
                                    document.GetName().c_str());
                                context.ErrorEncountered();
                                return false;
                            }
                            auto& targetClaims = claims[targetComposition];
                            if (const auto prior = targetClaims.find(key); prior != targetClaims.end())
                            {
                                AZ_Error(
                                    "HeightmapStampExport",
                                    false,
                                    "Duplicate key '%s' in prefab '%s', composition %s, stamps %s and %s; export stopped.",
                                    key.c_str(),
                                    document.GetName().c_str(),
                                    targetComposition.ToString().c_str(),
                                    prior->second.ToString().c_str(),
                                    entity->GetId().ToString().c_str());
                                context.ErrorEncountered();
                                return false;
                            }
                            targetClaims.emplace(key, entity->GetId());
                            // Only the conversion document is edited. Normal conversion remaps EntityId references;
                            // the key remains byte-for-byte unchanged through either editor adapter's BuildGameEntity.
                            if (editorStamp)
                            {
                                editorStamp->SetExportOrderKey(key);
                            }
                            else if (stamp)
                            {
                                auto configuration = stamp->GetStampConfiguration();
                                configuration.m_stableOrderKey = key;
                                stamp->SetStampConfiguration(configuration);
                            }
                            else if (editorCutout)
                            {
                                editorCutout->SetExportData(key);
                            }
                            else if (cutout)
                            {
                                auto configuration = cutout->GetCutoutConfiguration();
                                configuration.m_stableOrderKey = key;
                                cutout->SetCutoutConfiguration(configuration);
                            }
                            else if (editorMeshHeight)
                            {
                                editorMeshHeight->SetExportData(key);
                            }
                            else
                            {
                                auto configuration = meshHeight->GetStampConfiguration();
                                configuration.m_stableOrderKey = key;
                                meshHeight->SetStampConfiguration(configuration);
                            }
                        }
                        return true;
                    });
            });
    }
} // namespace TerrainCompositor
