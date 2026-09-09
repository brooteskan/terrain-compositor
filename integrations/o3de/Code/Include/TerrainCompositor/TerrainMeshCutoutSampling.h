#pragma once

#include <AzCore/Component/EntityId.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/std/string/string.h>
#include <TerrainCompositor/TerrainExistenceConfig.h>
#include <TerrainCompositor/TerrainCompositionBus.h>
#include <TerrainCompositor/TerrainMeshCutoutData.h>

namespace TerrainCompositor
{
    enum class TerrainMeshCutoutPlacementValidation : AZ::u8
    {
        Valid,
        DataUnavailable,
        TransformUnavailable,
        NonUniformScale,
        Transform,
        Margin,
        OrderingIdentity,
        Bounds
    };

    enum class TerrainMeshCutoutConsumer : AZ::u8
    {
        Rendering,
        CollisionQueries
    };

    struct PreparedTerrainMeshCutout
    {
        AZ::EntityId m_entityId{};
        TerrainMeshCutoutDataPtr m_data;
        AZ::Transform m_worldFromLocal = AZ::Transform::CreateIdentity();
        AZ::Transform m_localFromWorld = AZ::Transform::CreateIdentity();
        AZ::Aabb m_renderWorldBounds = AZ::Aabb::CreateNull();
        AZ::Aabb m_collisionWorldBounds = AZ::Aabb::CreateNull();
        double m_renderLocalMargin = 0.0;
        double m_collisionLocalMargin = 0.0;
        bool m_affectTerrainRendering = true;
        bool m_affectTerrainCollisionQueries = true;
        AZ::s32 m_priority = 0;
        AZStd::string m_stableOrderKey;
        TerrainExistenceOperation m_operation = TerrainExistenceOperation::RemoveTerrain;
    };

    TerrainMeshCutoutPlacementValidation PrepareTerrainMeshCutout(
        const TerrainMeshCutoutRegistrationData& registration,
        bool hasNonUniformScale,
        PreparedTerrainMeshCutout& result);

    //! Expands only collision/query sampling by one XY cell diagonal so every intersected heightfield
    //! quad has a conservatively-holed owning sample, without changing the visible opening.
    void ApplyTerrainMeshCutoutCollisionCellPadding(
        PreparedTerrainMeshCutout& cutout, float worldHeightfieldGridSpacing);

    //! Matches the closed cutter volume using the selected consumer's participation flag and margin.
    bool SampleTerrainMeshCutout(
        const AZ::Vector3& worldSurfacePoint, const PreparedTerrainMeshCutout& cutout,
        TerrainMeshCutoutConsumer consumer);
    const char* GetTerrainMeshCutoutPlacementValidationMessage(TerrainMeshCutoutPlacementValidation validation);
} // namespace TerrainCompositor
