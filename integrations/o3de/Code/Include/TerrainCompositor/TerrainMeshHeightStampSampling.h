#pragma once

#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/string/string.h>
#include <TerrainCompositor/TerrainCompositionBus.h>

namespace TerrainCompositor
{
    enum class TerrainMeshHeightStampPlacementValidation : AZ::u8
    {
        Valid,
        DataUnavailable,
        MissingTransform,
        NonUniformScale,
        NonFiniteTransform,
        UniformScale,
        Rotation,
        Strength,
        Feather,
        FeatherExponent,
        EdgeInset,
        Bounds
    };

    struct PreparedTerrainMeshHeightStamp
    {
        TerrainMeshHeightDataPtr m_data;
        AZ::Aabb m_worldBounds = AZ::Aabb::CreateNull();
        AZ::EntityId m_stampEntityId{};
        AZStd::string m_stableOrderKey;
        AZ::s32 m_priority = 0;
        double m_originX = 0.0;
        double m_originY = 0.0;
        double m_heightOrigin = 0.0;
        double m_scale = 1.0;
        double m_inverseScale = 1.0;
        double m_cosYaw = 1.0;
        double m_sinYaw = 0.0;
        double m_localMinX = 0.0;
        double m_localMaxX = 0.0;
        double m_localMinY = 0.0;
        double m_localMaxY = 0.0;
        double m_strength = 0.0;
        double m_feather = 0.0;
        double m_featherExponent = 1.0;
        double m_edgeInset = 0.0;
        bool m_relativeEdgeBlend = true;
        TerrainMeshHeightUncoveredAreaPolicy m_uncoveredAreaPolicy = TerrainMeshHeightUncoveredAreaPolicy::PreserveLowerTerrain;
        bool m_affectTerrainRendering = true;
        bool m_affectTerrainCollisionQueries = true;
    };

    struct TerrainMeshHeightContribution
    {
        double m_displacement = 0.0;
        double m_targetHeight = 0.0;
        double m_weight = 0.0;
        double m_replaceBlend = 1.0;
    };

    TerrainMeshHeightStampPlacementValidation PrepareTerrainMeshHeightStamp(
        const TerrainMeshHeightStampRegistrationData& registration, bool hasNonUniformScale, PreparedTerrainMeshHeightStamp& result);
    bool SampleTerrainMeshHeightStamp(
        const AZ::Vector3& position, const PreparedTerrainMeshHeightStamp& stamp, TerrainMeshHeightContribution& result);
    const char* GetTerrainMeshHeightStampPlacementValidationMessage(TerrainMeshHeightStampPlacementValidation validation);
    AZStd::string GetTerrainMeshHeightDataDiagnostic(const TerrainMeshHeightDataSnapshot& snapshot);
} // namespace TerrainCompositor
