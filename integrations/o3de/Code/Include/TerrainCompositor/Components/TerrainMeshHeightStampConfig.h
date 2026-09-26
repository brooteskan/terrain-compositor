#pragma once

#include <Atom/RPI.Reflect/Model/ModelAsset.h>
#include <AzCore/Component/ComponentBus.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Math/Uuid.h>
#include <AzCore/std/string/string.h>
#include <AzCore/std/containers/vector.h>
#include <TerrainCompositor/TerrainCompositorTypeIds.h>

namespace TerrainCompositor
{
    struct TerrainMeshSurfaceMapping
    {
        AZ_TYPE_INFO(TerrainMeshSurfaceMapping, "{0927328F-3325-47EB-8414-1F92D667C4E6}");
        AZStd::string m_slotName;
        AZ::u16 m_surfaceId = 0;
        bool operator==(const TerrainMeshSurfaceMapping& other) const
        { return m_slotName == other.m_slotName && m_surfaceId == other.m_surfaceId; }
    };
    enum class TerrainMeshHeightUncoveredAreaPolicy : AZ::u8
    {
        PreserveLowerTerrain,
        CutOutTerrain
    };

    //! Serialized authoring contract for a regular-grid Atom model used as absolute terrain height.
    //! Placement comes from the unique matching Mesh entity on the component entity or a descendant.
    class TerrainMeshHeightStampConfig final : public AZ::ComponentConfig
    {
    public:
        AZ_CLASS_ALLOCATOR(TerrainMeshHeightStampConfig, AZ::SystemAllocator);
        AZ_RTTI(TerrainMeshHeightStampConfig, TerrainMeshHeightStampConfigTypeId, AZ::ComponentConfig);

        static void Reflect(AZ::ReflectContext* context);
        void AssignNewPersistentOrderingIdentity();
        AZStd::string GetRuntimeOrderKey() const;

        AZ::EntityId m_targetCompositionEntityId;
        AZStd::vector<TerrainMeshSurfaceMapping> m_surfaceMappings;
        AZ::Data::Asset<AZ::RPI::ModelAsset> m_terrainMeshAsset{ AZ::Data::AssetLoadBehavior::NoLoad };
        float m_strength = 1.0f;
        float m_featherWidth = 0.0f;
        float m_featherExponent = 1.0f;
        float m_edgeInset = 0.0f;
        bool m_relativeEdgeBlend = true;
        TerrainMeshHeightUncoveredAreaPolicy m_uncoveredAreaPolicy = TerrainMeshHeightUncoveredAreaPolicy::PreserveLowerTerrain;
        bool m_affectTerrainRendering = true;
        bool m_affectTerrainCollisionQueries = true;
        AZ::s32 m_priority = 0;
        AZ::Uuid m_orderingId = AZ::Uuid::CreateNull();
        AZStd::string m_stableOrderKey;
    };
} // namespace TerrainCompositor

namespace AZ
{
    AZ_TYPE_INFO_SPECIALIZE(TerrainCompositor::TerrainMeshHeightUncoveredAreaPolicy, "{087A6425-AF48-4B95-938E-00D40ABFC60F}");
}
