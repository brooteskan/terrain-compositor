#include <TerrainCompositor/Components/TerrainMeshHeightStampConfig.h>

#include <AzCore/Asset/AssetSerializer.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <TerrainCompositor/HeightmapStampIdentity.h>

namespace TerrainCompositor
{
    namespace
    {
        bool TerrainMeshHeightStampConfigVersionConverter(
            AZ::SerializeContext& context, AZ::SerializeContext::DataElementNode& classElement)
        {
            if (classElement.GetVersion() < 2)
            {
                classElement.RemoveElementByName(AZ_CRC_CE("ShowSourceMeshInEditor"));
            }
            if (classElement.GetVersion() < 3)
            {
                classElement.AddElementWithData(context, "UncoveredAreaPolicy", TerrainMeshHeightUncoveredAreaPolicy::PreserveLowerTerrain);
                classElement.AddElementWithData(context, "AffectTerrainRendering", true);
                classElement.AddElementWithData(context, "AffectTerrainCollisionQueries", true);
            }
            return true;
        }
    } // namespace

    void TerrainMeshHeightStampConfig::AssignNewPersistentOrderingIdentity()
    {
        m_orderingId = AZ::Uuid::CreateRandom();
        m_stableOrderKey = MakeUuidStampOrderKey(m_orderingId);
    }

    AZStd::string TerrainMeshHeightStampConfig::GetRuntimeOrderKey() const
    {
        if (!m_stableOrderKey.empty())
        {
            return IsValidStampOrderKey(m_stableOrderKey) ? m_stableOrderKey : AZStd::string{};
        }
        return MakeUuidStampOrderKey(m_orderingId);
    }

    void TerrainMeshHeightStampConfig::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection() && serialize->FindClassData(azrtti_typeid<TerrainMeshHeightStampConfig>()))
            {
                return;
            }
            serialize->Enum<TerrainMeshHeightUncoveredAreaPolicy>()
                ->Value("PreserveLowerTerrain", TerrainMeshHeightUncoveredAreaPolicy::PreserveLowerTerrain)
                ->Value("CutOutTerrain", TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain);
            serialize->Class<TerrainMeshHeightStampConfig, AZ::ComponentConfig>()
                ->Version(3, TerrainMeshHeightStampConfigVersionConverter)
                ->Field("TargetCompositionEntityId", &TerrainMeshHeightStampConfig::m_targetCompositionEntityId)
                ->Field("TerrainMeshAsset", &TerrainMeshHeightStampConfig::m_terrainMeshAsset)
                ->Field("Strength", &TerrainMeshHeightStampConfig::m_strength)
                ->Field("FeatherWidth", &TerrainMeshHeightStampConfig::m_featherWidth)
                ->Field("FeatherExponent", &TerrainMeshHeightStampConfig::m_featherExponent)
                ->Field("EdgeInset", &TerrainMeshHeightStampConfig::m_edgeInset)
                ->Field("RelativeEdgeBlend", &TerrainMeshHeightStampConfig::m_relativeEdgeBlend)
                ->Field("UncoveredAreaPolicy", &TerrainMeshHeightStampConfig::m_uncoveredAreaPolicy)
                ->Field("AffectTerrainRendering", &TerrainMeshHeightStampConfig::m_affectTerrainRendering)
                ->Field("AffectTerrainCollisionQueries", &TerrainMeshHeightStampConfig::m_affectTerrainCollisionQueries)
                ->Field("Priority", &TerrainMeshHeightStampConfig::m_priority)
                ->Field("OrderingId", &TerrainMeshHeightStampConfig::m_orderingId)
                ->Field("StableOrderKey", &TerrainMeshHeightStampConfig::m_stableOrderKey);

            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<TerrainMeshHeightStampConfig>(
                        "Terrain Mesh Height Stamp Configuration",
                        "Use a regular-grid Atom model as an absolute terrain-height contributor.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainMeshHeightStampConfig::m_targetCompositionEntityId,
                        "Target Composition",
                        "Terrain Composition Gradient in this entity context.")
                    ->Attribute(AZ::Edit::Attributes::RequiredService, AZ_CRC_CE("TerrainCompositionService"))
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainMeshHeightStampConfig::m_terrainMeshAsset,
                        "Terrain Mesh",
                        "Open 2.5D regular-grid Atom Model. Its unique matching Mesh descendant supplies placement.")
                    ->DataElement(
                        AZ::Edit::UIHandlers::Slider,
                        &TerrainMeshHeightStampConfig::m_strength,
                        "Strength",
                        "Absolute Replace blend weight.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainMeshHeightStampConfig::m_featherWidth,
                        "Edge Feather Width",
                        "Local transition width after Edge Inset. Zero produces hard edges.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainMeshHeightStampConfig::m_featherExponent,
                        "Edge Feather Exponent",
                        "Finite exponent of at least 1 applied to the feather mask.")
                    ->Attribute(AZ::Edit::Attributes::Min, 1.0f)
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainMeshHeightStampConfig::m_edgeInset,
                        "Edge Inset",
                        "Local border that contributes no terrain height.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainMeshHeightStampConfig::m_relativeEdgeBlend,
                        "Relative Edge Blend",
                        "Blend mesh relief onto accumulated ground through the feather band.")
                    ->DataElement(
                        AZ::Edit::UIHandlers::ComboBox,
                        &TerrainMeshHeightStampConfig::m_uncoveredAreaPolicy,
                        "Uncovered Areas",
                        "Preserve lower terrain in complete empty grid cells, or mark those cells for terrain removal.")
                    ->EnumAttribute(TerrainMeshHeightUncoveredAreaPolicy::PreserveLowerTerrain, "Preserve Lower Terrain")
                    ->EnumAttribute(TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain, "Cut Out Terrain")
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainMeshHeightStampConfig::m_affectTerrainRendering,
                        "Affect Terrain Rendering",
                        "When Cut Out Terrain is selected, apply uncovered cells to terrain rendering and shadows.")
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainMeshHeightStampConfig::m_affectTerrainCollisionQueries,
                        "Affect Terrain Collision / Queries",
                        "When Cut Out Terrain is selected, apply uncovered cells to terrain collision, raycasts, surfaces, and existence queries.")
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainMeshHeightStampConfig::m_priority,
                        "Priority",
                        "Lower priorities apply first; stable prefab identity breaks ties.");
            }
        }
    }
} // namespace TerrainCompositor
