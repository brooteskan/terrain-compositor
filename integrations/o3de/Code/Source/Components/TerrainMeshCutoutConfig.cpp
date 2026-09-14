#include <TerrainCompositor/Components/TerrainMeshCutoutConfig.h>

#include <AzCore/Asset/AssetSerializer.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <TerrainCompositor/HeightmapStampIdentity.h>

namespace TerrainCompositor
{
    namespace
    {
        bool TerrainMeshCutoutConfigVersionConverter(
            AZ::SerializeContext& context, AZ::SerializeContext::DataElementNode& classElement)
        {
            if (classElement.GetVersion() < 4)
            {
                float legacyMargin = 0.05f;
                classElement.GetChildData(AZ_CRC_CE("IntersectionMargin"), legacyMargin);
                classElement.RemoveElementByName(AZ_CRC_CE("IntersectionMargin"));
                classElement.AddElementWithData(context, "RenderMargin", legacyMargin);
                classElement.AddElementWithData(context, "CollisionMargin", legacyMargin);
            }
            if (classElement.GetVersion() < 5)
            {
                // The entity source mode was ambiguous and could apply a different transform than the directly
                // selected model. Existing components already serialize CutoutMeshAsset, so retain that single source.
                classElement.RemoveElementByName(AZ_CRC_CE("CutoutMeshEntityId"));
                classElement.RemoveElementByName(AZ_CRC_CE("CutoutMeshVisibleInGame"));
            }
            return true;
        }
    } // namespace

    void TerrainMeshCutoutConfig::AssignNewPersistentOrderingIdentity()
    {
        AssignNewStampOrderingIdentity(m_orderingId, m_stableOrderKey);
    }

    AZStd::string TerrainMeshCutoutConfig::GetRuntimeOrderKey() const
    {
        return GetRuntimeStampOrderKey(m_orderingId, m_stableOrderKey);
    }

    void TerrainMeshCutoutConfig::Reflect(AZ::ReflectContext* context)
    {
        TerrainHoleMaskConfig::Reflect(context);
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection() && serialize->FindClassData(azrtti_typeid<TerrainMeshCutoutConfig>()))
            {
                return;
            }
            serialize->Class<TerrainMeshCutoutConfig, AZ::ComponentConfig>()
                ->Version(5, TerrainMeshCutoutConfigVersionConverter)
                ->Field("TargetCompositionEntityId", &TerrainMeshCutoutConfig::m_targetCompositionEntityId)
                ->Field("CutoutMeshAsset", &TerrainMeshCutoutConfig::m_cutoutMeshAsset)
                ->Field("Operation", &TerrainMeshCutoutConfig::m_operation)
                ->Field("Priority", &TerrainMeshCutoutConfig::m_priority)
                ->Field("AffectTerrainRendering", &TerrainMeshCutoutConfig::m_affectTerrainRendering)
                ->Field("AffectTerrainCollisionQueries", &TerrainMeshCutoutConfig::m_affectTerrainCollisionQueries)
                ->Field("RenderMargin", &TerrainMeshCutoutConfig::m_renderMargin)
                ->Field("CollisionMargin", &TerrainMeshCutoutConfig::m_collisionMargin)
                ->Field("DebugDrawRenderMask", &TerrainMeshCutoutConfig::m_debugDrawRenderMask)
                ->Field("DebugDrawCollisionMask", &TerrainMeshCutoutConfig::m_debugDrawCollisionMask)
                ->Field("OrderingId", &TerrainMeshCutoutConfig::m_orderingId)
                ->Field("StableOrderKey", &TerrainMeshCutoutConfig::m_stableOrderKey);

            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<TerrainMeshCutoutConfig>("Terrain Mesh Cutout Configuration",
                    "A closed static mesh volume which explicitly removes or restores composed terrain.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainMeshCutoutConfig::m_targetCompositionEntityId,
                        "Target Composition", "Terrain Composition Gradient in this entity context.")
                    ->Attribute(AZ::Edit::Attributes::RequiredService, AZ_CRC_CE("TerrainCompositionService"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainMeshCutoutConfig::m_cutoutMeshAsset,
                        "Cutout Mesh",
                        "Closed Atom Model product. Its unique matching Mesh instance on this entity or a descendant supplies the full world transform.")
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &TerrainMeshCutoutConfig::m_operation,
                        "Operation", "Explicit existence assignment applied when the final terrain surface lies in the volume.")
                    ->EnumAttribute(TerrainExistenceOperation::RemoveTerrain, "Remove Terrain")
                    ->EnumAttribute(TerrainExistenceOperation::RestoreTerrain, "Restore Terrain")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainMeshCutoutConfig::m_priority,
                        "Priority", "Lower priorities apply first; stable prefab identity breaks equal-priority ties.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainMeshCutoutConfig::m_affectTerrainRendering,
                        "Affect Rendering", "Apply this closed volume to terrain rendering and terrain shadows.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainMeshCutoutConfig::m_renderMargin,
                        "Render Margin", "Nonnegative world-space dilation of the rendered terrain opening.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainMeshCutoutConfig::m_affectTerrainCollisionQueries,
                        "Affect Collision / Queries", "Remove terrain existence, raycasts, surfaces, and heightfield collision cells.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainMeshCutoutConfig::m_collisionMargin,
                        "Collision Margin", "Nonnegative world-space dilation used by terrain existence and collision queries.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainMeshCutoutConfig::m_debugDrawRenderMask,
                        "Debug Render Mask", "Preview the rendered-terrain cutout footprint in the editor.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainMeshCutoutConfig::m_debugDrawCollisionMask,
                        "Debug Collision Mask", "Preview the collision/query cutout footprint in the editor.");
            }
        }
    }
} // namespace TerrainCompositor
