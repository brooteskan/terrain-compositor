#pragma once

#include <TerrainCompositor/TerrainCompositionBus.h>

namespace TerrainCompositor
{
    enum class HeightmapStampValidation
    {
        Valid, Width, Depth, HeightScale, Offset, Strength, Feather,
        MissingTransform, NonUniformScale, NonFiniteTransform, UniformScale, Rotation, Bounds, ImageData,
        FeatherExponent, EdgeInset, SamplingMode, ReconstructionRadius
    };

    //! Shared, value-only placement data for height, surface, and terrain-existence stamp snapshots.
    //! Bounds describe XY at Z=0; query Z and stamp-origin Z never determine footprint coverage.
    struct PreparedStampPlacement
    {
        AZ::Aabb m_worldBounds = AZ::Aabb::CreateNull();
        AZ::s32 m_priority = 0;
        AZ::EntityId m_stampEntityId{}; //!< Change matching only; never used for ordering.
        AZStd::string m_stableOrderKey;
        double m_centerX = 0.0;
        double m_centerY = 0.0;
        double m_inverseScale = 1.0;
        double m_cosYaw = 1.0;
        double m_sinYaw = 0.0;
        double m_halfWidth = 0.0;
        double m_halfDepth = 0.0;
        double m_edgeInset = 0.0;
    };

    struct MappedStampPosition
    {
        double m_localX = 0.0;
        double m_localY = 0.0;
        double m_edgeDistanceX = 0.0;
        double m_edgeDistanceY = 0.0;
        double m_u = 0.0;
        double m_v = 0.0;
    };

    //! Pure preparation: no buses, asset acquisition, logging, or mutation of image data.
    HeightmapStampValidation PrepareStampPlacement(
        const HeightmapStampRegistrationData& registration, bool hasNonUniformScale, PreparedStampPlacement& result);

    //! Applies the common broad/exact footprint, positive-inset, rotation, and UV contract.
    bool TryMapPositionToStamp(
        const AZ::Vector3& position, const PreparedStampPlacement& placement, MappedStampPosition& result);
} // namespace TerrainCompositor
