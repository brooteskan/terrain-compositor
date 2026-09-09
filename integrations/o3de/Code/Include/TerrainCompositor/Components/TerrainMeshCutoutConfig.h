#pragma once

#include <Atom/RPI.Reflect/Model/ModelAsset.h>
#include <AzCore/Component/ComponentBus.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Math/Uuid.h>
#include <AzCore/std/string/string.h>
#include <TerrainCompositor/TerrainExistenceConfig.h>
#include <TerrainCompositor/TerrainCompositorTypeIds.h>

namespace TerrainCompositor
{
    class TerrainMeshCutoutConfig final : public AZ::ComponentConfig
    {
    public:
        AZ_CLASS_ALLOCATOR(TerrainMeshCutoutConfig, AZ::SystemAllocator);
        AZ_RTTI(TerrainMeshCutoutConfig, TerrainMeshCutoutConfigTypeId, AZ::ComponentConfig);

        static void Reflect(AZ::ReflectContext* context);
        void AssignNewPersistentOrderingIdentity();
        AZStd::string GetRuntimeOrderKey() const;

        AZ::EntityId m_targetCompositionEntityId;
        //! Closed cutter model. Placement comes from the unique matching Atom Mesh instance on the component
        //! entity or one of its descendants, including that instance's complete world transform.
        AZ::Data::Asset<AZ::RPI::ModelAsset> m_cutoutMeshAsset{ AZ::Data::AssetLoadBehavior::NoLoad };
        TerrainExistenceOperation m_operation = TerrainExistenceOperation::RemoveTerrain;
        AZ::s32 m_priority = 0;
        //! Rendering and terrain queries deliberately have independent participation and dilation controls.
        //! This lets collision use a conservative mask without visibly enlarging the opening.
        bool m_affectTerrainRendering = true;
        bool m_affectTerrainCollisionQueries = true;
        float m_renderMargin = 0.05f;
        float m_collisionMargin = 0.05f;
        bool m_debugDrawRenderMask = false;
        bool m_debugDrawCollisionMask = false;
        AZ::Uuid m_orderingId = AZ::Uuid::CreateNull();
        AZStd::string m_stableOrderKey;
    };
} // namespace TerrainCompositor
