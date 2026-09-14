#pragma once

#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Quaternion.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace TerrainCompositor::Internal
{
    inline bool TryGetStampYaw(const AZ::Quaternion& rotation, double& yaw)
    {
        constexpr double RotationTolerance = 1.0e-4;
        const double x = rotation.GetX();
        const double y = rotation.GetY();
        const double z = rotation.GetZ();
        const double w = rotation.GetW();
        const double lengthSquared = x * x + y * y + z * z + w * w;
        // Normalize only accepted rounding drift, not malformed quaternions.
        if (!std::isfinite(lengthSquared) || std::abs(lengthSquared - 1.0) > RotationTolerance ||
            std::abs(2.0 * (x * z + w * y) / lengthSquared) > RotationTolerance ||
            std::abs(2.0 * (y * z - w * x) / lengthSquared) > RotationTolerance ||
            2.0 * (x * x + y * y) / lengthSquared > RotationTolerance)
        {
            return false;
        }
        yaw = std::atan2(2.0 * (x * y + w * z), lengthSquared - 2.0 * (y * y + z * z));
        return true;
    }

    struct StampXYBounds
    {
        double m_minX = std::numeric_limits<double>::max();
        double m_maxX = -std::numeric_limits<double>::max();
        double m_minY = std::numeric_limits<double>::max();
        double m_maxY = -std::numeric_limits<double>::max();

        bool IsRepresentable() const
        {
            const double limit = std::numeric_limits<float>::max();
            return std::isfinite(m_minX) && std::isfinite(m_maxX) && std::isfinite(m_minY) && std::isfinite(m_maxY) &&
                std::abs(m_minX) <= limit && std::abs(m_maxX) <= limit && std::abs(m_minY) <= limit && std::abs(m_maxY) <= limit;
        }
        AZ::Aabb ToAabb() const;
    };

    inline StampXYBounds TransformStampXYBounds(
        const AZ::Aabb& localBounds, double originX, double originY, double scale, double cosYaw, double sinYaw)
    {
        StampXYBounds bounds;
        for (const double localX : { double(localBounds.GetMin().GetX()), double(localBounds.GetMax().GetX()) })
        {
            for (const double localY : { double(localBounds.GetMin().GetY()), double(localBounds.GetMax().GetY()) })
            {
                const double worldX = originX + scale * (cosYaw * localX - sinYaw * localY);
                const double worldY = originY + scale * (sinYaw * localX + cosYaw * localY);
                bounds.m_minX = std::min(bounds.m_minX, worldX);
                bounds.m_maxX = std::max(bounds.m_maxX, worldX);
                bounds.m_minY = std::min(bounds.m_minY, worldY);
                bounds.m_maxY = std::max(bounds.m_maxY, worldY);
            }
        }
        return bounds;
    }

    inline double SmoothStep01(double value)
    {
        value = std::clamp(value, 0.0, 1.0);
        return value * value * (3.0 - 2.0 * value);
    }

    inline float RoundOutward(double value, bool lower)
    {
        float result = static_cast<float>(value);
        if ((lower && result > value) || (!lower && result < value))
        {
            result = std::nextafter(result, lower ? -std::numeric_limits<float>::infinity()
                                                 : std::numeric_limits<float>::infinity());
        }
        return result;
    }

    inline AZ::Aabb StampXYBounds::ToAabb() const
    {
        return AZ::Aabb::CreateFromMinMax(
            AZ::Vector3(RoundOutward(m_minX, true), RoundOutward(m_minY, true), 0.0f),
            AZ::Vector3(RoundOutward(m_maxX, false), RoundOutward(m_maxY, false), 0.0f));
    }
}
