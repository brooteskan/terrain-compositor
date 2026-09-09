#include <TerrainCompositor/StampPlacement.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace TerrainCompositor
{
    namespace
    {
        constexpr double RotationTolerance = 1.0e-4;

        // Outward rounding keeps the broad-phase AABB conservative at rotated image edges.
        float RoundBound(double value, bool lower)
        {
            float result = static_cast<float>(value);
            if ((lower && result > value) || (!lower && result < value))
            {
                result = std::nextafter(result, lower ? -std::numeric_limits<float>::infinity()
                                                     : std::numeric_limits<float>::infinity());
            }
            return result;
        }
    } // namespace

    HeightmapStampValidation PrepareStampPlacement(
        const HeightmapStampRegistrationData& registration, bool hasNonUniformScale, PreparedStampPlacement& result)
    {
        result = {};
        const auto& config = registration.m_configuration;
        if (!std::isfinite(config.m_footprintWidth) || config.m_footprintWidth <= 0.0f)
        {
            return HeightmapStampValidation::Width;
        }
        if (!std::isfinite(config.m_footprintDepth) || config.m_footprintDepth <= 0.0f)
        {
            return HeightmapStampValidation::Depth;
        }
        const double halfSmallerDimension = 0.5 * std::min(config.m_footprintWidth, config.m_footprintDepth);
        if (!std::isfinite(config.m_edgeInset) || config.m_edgeInset < 0.0f || config.m_edgeInset > halfSmallerDimension)
        {
            return HeightmapStampValidation::EdgeInset;
        }
        if (!registration.m_transformAvailable)
        {
            return HeightmapStampValidation::MissingTransform;
        }
        if (hasNonUniformScale)
        {
            return HeightmapStampValidation::NonUniformScale;
        }
        const auto& transform = registration.m_worldTransform;
        if (!transform.IsFinite())
        {
            return HeightmapStampValidation::NonFiniteTransform;
        }
        const double scale = transform.GetUniformScale();
        if (scale <= 0.0)
        {
            return HeightmapStampValidation::UniformScale;
        }
        const auto& rotation = transform.GetRotation();
        const double x = rotation.GetX();
        const double y = rotation.GetY();
        const double z = rotation.GetZ();
        const double w = rotation.GetW();
        const double lengthSquared = x * x + y * y + z * z + w * w;
        // Test the effective world orientation, not local Euler angles. Accept only numerical drift from yaw.
        if (std::abs(lengthSquared - 1.0) > RotationTolerance)
        {
            return HeightmapStampValidation::Rotation;
        }
        // Normalize only accepted rounding drift, not arbitrary malformed quaternions.
        if (std::abs(2.0 * (x * z + w * y) / lengthSquared) > RotationTolerance ||
            std::abs(2.0 * (y * z - w * x) / lengthSquared) > RotationTolerance ||
            2.0 * (x * x + y * y) / lengthSquared > RotationTolerance)
        {
            return HeightmapStampValidation::Rotation;
        }

        const double yaw = std::atan2(2.0 * (x * y + w * z), lengthSquared - 2.0 * (y * y + z * z));
        PreparedStampPlacement prepared;
        prepared.m_centerX = transform.GetTranslation().GetX();
        prepared.m_centerY = transform.GetTranslation().GetY();
        prepared.m_inverseScale = 1.0 / scale;
        prepared.m_cosYaw = std::cos(yaw);
        prepared.m_sinYaw = std::sin(yaw);
        prepared.m_halfWidth = 0.5 * double(config.m_footprintWidth);
        prepared.m_halfDepth = 0.5 * double(config.m_footprintDepth);
        prepared.m_edgeInset = config.m_edgeInset;
        prepared.m_priority = config.m_priority;
        prepared.m_stampEntityId = registration.m_stampEntityId;
        prepared.m_stableOrderKey = config.GetRuntimeOrderKey();

        // Extrema of the four yaw-rotated/scaled rectangle corners. Double intermediates avoid float overflow.
        const double extentX = scale * (std::abs(prepared.m_cosYaw) * prepared.m_halfWidth +
                                        std::abs(prepared.m_sinYaw) * prepared.m_halfDepth);
        const double extentY = scale * (std::abs(prepared.m_sinYaw) * prepared.m_halfWidth +
                                        std::abs(prepared.m_cosYaw) * prepared.m_halfDepth);
        const double floatMax = std::numeric_limits<float>::max();
        if (std::abs(prepared.m_centerX) + extentX > floatMax ||
            std::abs(prepared.m_centerY) + extentY > floatMax)
        {
            return HeightmapStampValidation::Bounds;
        }
        prepared.m_worldBounds = AZ::Aabb::CreateFromMinMax(
            AZ::Vector3(RoundBound(prepared.m_centerX - extentX, true),
                RoundBound(prepared.m_centerY - extentY, true), 0.0f),
            AZ::Vector3(RoundBound(prepared.m_centerX + extentX, false),
                RoundBound(prepared.m_centerY + extentY, false), 0.0f));
        result = AZStd::move(prepared);
        return HeightmapStampValidation::Valid;
    }

    bool TryMapPositionToStamp(
        const AZ::Vector3& position, const PreparedStampPlacement& placement, MappedStampPosition& result)
    {
        result = {};
        if (!std::isfinite(position.GetX()) || !std::isfinite(position.GetY()) ||
            position.GetX() < placement.m_worldBounds.GetMin().GetX() ||
            position.GetX() > placement.m_worldBounds.GetMax().GetX() ||
            position.GetY() < placement.m_worldBounds.GetMin().GetY() ||
            position.GetY() > placement.m_worldBounds.GetMax().GetY())
        {
            return false;
        }

        const double deltaX = double(position.GetX()) - placement.m_centerX;
        const double deltaY = double(position.GetY()) - placement.m_centerY;
        MappedStampPosition mapped;
        mapped.m_localX = (placement.m_cosYaw * deltaX + placement.m_sinYaw * deltaY) * placement.m_inverseScale;
        mapped.m_localY = (-placement.m_sinYaw * deltaX + placement.m_cosYaw * deltaY) * placement.m_inverseScale;
        mapped.m_edgeDistanceX = placement.m_halfWidth - std::abs(mapped.m_localX);
        mapped.m_edgeDistanceY = placement.m_halfDepth - std::abs(mapped.m_localY);
        if (mapped.m_edgeDistanceX < 0.0 || mapped.m_edgeDistanceY < 0.0 ||
            (placement.m_edgeInset > 0.0 &&
                (mapped.m_edgeDistanceX <= placement.m_edgeInset || mapped.m_edgeDistanceY <= placement.m_edgeInset)))
        {
            return false;
        }
        mapped.m_u = mapped.m_localX / (2.0 * placement.m_halfWidth) + 0.5;
        mapped.m_v = mapped.m_localY / (2.0 * placement.m_halfDepth) + 0.5;
        result = mapped;
        return true;
    }
} // namespace TerrainCompositor
