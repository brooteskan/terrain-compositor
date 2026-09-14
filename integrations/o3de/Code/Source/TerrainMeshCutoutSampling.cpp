#include "MeshCutoutTraversal.h"
#include <TerrainCompositor/TerrainMeshCutoutSampling.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace TerrainCompositor
{
    namespace
    {
        double DistanceSquaredToAabb(const AZ::Vector3& point, const AZ::Aabb& bounds)
        {
            double distance = 0.0;
            for (int axis = 0; axis < 3; ++axis)
            {
                const double value = point.GetElement(axis);
                const double minimum = bounds.GetMin().GetElement(axis);
                const double maximum = bounds.GetMax().GetElement(axis);
                const double delta = value < minimum ? minimum - value : value > maximum ? value - maximum : 0.0;
                distance += delta * delta;
            }
            return distance;
        }

        double DistanceSquaredToTriangle(const AZ::Vector3& point, const TerrainMeshCutoutTriangle& triangle)
        {
            // Real-Time Collision Detection, closest point on triangle.
            const AZ::Vector3 ab = triangle.m_b - triangle.m_a;
            const AZ::Vector3 ac = triangle.m_c - triangle.m_a;
            const AZ::Vector3 ap = point - triangle.m_a;
            const double d1 = ab.Dot(ap);
            const double d2 = ac.Dot(ap);
            if (d1 <= 0.0 && d2 <= 0.0) return double(ap.GetLengthSq());

            const AZ::Vector3 bp = point - triangle.m_b;
            const double d3 = ab.Dot(bp);
            const double d4 = ac.Dot(bp);
            if (d3 >= 0.0 && d4 <= d3) return double(bp.GetLengthSq());

            const double vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
            {
                const double v = d1 / (d1 - d3);
                return double((point - (triangle.m_a + ab * float(v))).GetLengthSq());
            }

            const AZ::Vector3 cp = point - triangle.m_c;
            const double d5 = ab.Dot(cp);
            const double d6 = ac.Dot(cp);
            if (d6 >= 0.0 && d5 <= d6) return double(cp.GetLengthSq());

            const double vb = d5 * d2 - d1 * d6;
            if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
            {
                const double w = d2 / (d2 - d6);
                return double((point - (triangle.m_a + ac * float(w))).GetLengthSq());
            }

            const double va = d3 * d6 - d5 * d4;
            if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0)
            {
                const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                return double((point - (triangle.m_b + (triangle.m_c - triangle.m_b) * float(w))).GetLengthSq());
            }

            const double denominator = 1.0 / (va + vb + vc);
            const double v = vb * denominator;
            const double w = vc * denominator;
            const AZ::Vector3 closest = triangle.m_a + ab * float(v) + ac * float(w);
            return double((point - closest).GetLengthSq());
        }

        bool RayIntersectsAabb(const AZ::Vector3& origin, const AZ::Vector3& direction, const AZ::Aabb& bounds)
        {
            double nearValue = 0.0;
            double farValue = std::numeric_limits<double>::infinity();
            for (int axis = 0; axis < 3; ++axis)
            {
                const double value = origin.GetElement(axis);
                const double delta = direction.GetElement(axis);
                const double minimum = bounds.GetMin().GetElement(axis);
                const double maximum = bounds.GetMax().GetElement(axis);
                if (std::abs(delta) < 1.0e-15)
                {
                    if (value < minimum || value > maximum) return false;
                    continue;
                }
                double a = (minimum - value) / delta;
                double b = (maximum - value) / delta;
                if (a > b) AZStd::swap(a, b);
                nearValue = std::max(nearValue, a);
                farValue = std::min(farValue, b);
                if (nearValue > farValue) return false;
            }
            return farValue >= 0.0;
        }

        bool RayIntersectsTriangle(
            const AZ::Vector3& origin, const AZ::Vector3& direction, const TerrainMeshCutoutTriangle& triangle)
        {
            const AZ::Vector3 edge1 = triangle.m_b - triangle.m_a;
            const AZ::Vector3 edge2 = triangle.m_c - triangle.m_a;
            const AZ::Vector3 p = direction.Cross(edge2);
            const double determinant = edge1.Dot(p);
            if (std::abs(determinant) < 1.0e-12) return false;
            const double inverse = 1.0 / determinant;
            const AZ::Vector3 t = origin - triangle.m_a;
            const double u = t.Dot(p) * inverse;
            // Half-open barycentric ownership prevents shared-edge double counts.
            if (u < 0.0 || u >= 1.0) return false;
            const AZ::Vector3 q = t.Cross(edge1);
            const double v = direction.Dot(q) * inverse;
            if (v < 0.0 || u + v >= 1.0) return false;
            return edge2.Dot(q) * inverse > 0.0;
        }

        bool IsWithinDistance(const AZ::Vector3& point, const TerrainMeshCutoutData& data, double maximumDistance)
        {
            const double maximumSquared = maximumDistance * maximumDistance;
            return Internal::VisitMeshCutoutTriangles(data,
                [&](const AZ::Aabb& bounds) { return !(DistanceSquaredToAabb(point, bounds) > maximumSquared); },
                [&](AZ::u32 triangle) { return DistanceSquaredToTriangle(point, data.m_triangles[triangle]) <= maximumSquared; });
        }

        bool IsInside(const AZ::Vector3& point, const TerrainMeshCutoutData& data)
        {
            const AZ::Vector3 direction = AZ::Vector3(1.0f, 0.37139067f, 0.19792317f).GetNormalized();
            size_t intersections = 0;
            Internal::VisitMeshCutoutTriangles(data,
                [&](const AZ::Aabb& bounds) { return RayIntersectsAabb(point, direction, bounds); },
                [&](AZ::u32 triangle)
                {
                    intersections += RayIntersectsTriangle(point, direction, data.m_triangles[triangle]);
                    return false; // Parity requires every hit, including after the first intersection.
                });
            return (intersections & 1) != 0;
        }
    } // namespace

    TerrainMeshCutoutPlacementValidation PrepareTerrainMeshCutout(
        const TerrainMeshCutoutRegistrationData& registration,
        bool hasNonUniformScale,
        PreparedTerrainMeshCutout& result)
    {
        result = PreparedTerrainMeshCutout{};
        if (registration.m_mesh.m_status != TerrainMeshCutoutDataStatus::Ready || !registration.m_mesh.m_data)
        {
            return TerrainMeshCutoutPlacementValidation::DataUnavailable;
        }
        if (!registration.m_transformAvailable)
        {
            return TerrainMeshCutoutPlacementValidation::TransformUnavailable;
        }
        if (hasNonUniformScale)
        {
            return TerrainMeshCutoutPlacementValidation::NonUniformScale;
        }
        const auto& transform = registration.m_worldTransform;
        const double scale = transform.GetUniformScale();
        const auto rotation = transform.GetRotation();
        const double rotationLengthSquared = double(rotation.GetLengthSq());
        if (!transform.IsFinite() || !std::isfinite(scale) || scale <= 0.0 ||
            !std::isfinite(rotationLengthSquared) || std::abs(rotationLengthSquared - 1.0) > 1.0e-4)
        {
            return TerrainMeshCutoutPlacementValidation::Transform;
        }
        if (!std::isfinite(registration.m_configuration.m_renderMargin) ||
            registration.m_configuration.m_renderMargin < 0.0f ||
            !std::isfinite(registration.m_configuration.m_collisionMargin) ||
            registration.m_configuration.m_collisionMargin < 0.0f)
        {
            return TerrainMeshCutoutPlacementValidation::Margin;
        }
        const auto stableKey = registration.m_configuration.GetRuntimeOrderKey();
        if (stableKey.empty())
        {
            return TerrainMeshCutoutPlacementValidation::OrderingIdentity;
        }

        PreparedTerrainMeshCutout prepared;
        prepared.m_entityId = registration.m_cutoutEntityId;
        prepared.m_data = registration.m_mesh.m_data;
        prepared.m_worldFromLocal = transform;
        prepared.m_localFromWorld = transform.GetInverse();
        prepared.m_renderLocalMargin = double(registration.m_configuration.m_renderMargin) / scale;
        prepared.m_collisionLocalMargin = double(registration.m_configuration.m_collisionMargin) / scale;
        prepared.m_affectTerrainRendering = registration.m_configuration.m_affectTerrainRendering;
        prepared.m_affectTerrainCollisionQueries = registration.m_configuration.m_affectTerrainCollisionQueries;
        prepared.m_priority = registration.m_configuration.m_priority;
        prepared.m_stableOrderKey = stableKey;
        prepared.m_operation = registration.m_configuration.m_operation;
        prepared.m_renderWorldBounds = prepared.m_data->m_localBounds.GetTransformedAabb(transform);
        prepared.m_renderWorldBounds.Expand(AZ::Vector3(registration.m_configuration.m_renderMargin));
        prepared.m_collisionWorldBounds = prepared.m_data->m_localBounds.GetTransformedAabb(transform);
        prepared.m_collisionWorldBounds.Expand(AZ::Vector3(registration.m_configuration.m_collisionMargin));
        if (!prepared.m_renderWorldBounds.IsValid() || !prepared.m_renderWorldBounds.GetMin().IsFinite() ||
            !prepared.m_renderWorldBounds.GetMax().IsFinite() || !prepared.m_collisionWorldBounds.IsValid() ||
            !prepared.m_collisionWorldBounds.GetMin().IsFinite() || !prepared.m_collisionWorldBounds.GetMax().IsFinite())
        {
            return TerrainMeshCutoutPlacementValidation::Bounds;
        }
        result = AZStd::move(prepared);
        return TerrainMeshCutoutPlacementValidation::Valid;
    }

    bool SampleTerrainMeshCutout(
        const AZ::Vector3& worldSurfacePoint, const PreparedTerrainMeshCutout& cutout,
        TerrainMeshCutoutConsumer consumer)
    {
        const bool enabled = consumer == TerrainMeshCutoutConsumer::Rendering
            ? cutout.m_affectTerrainRendering : cutout.m_affectTerrainCollisionQueries;
        const AZ::Aabb& worldBounds = consumer == TerrainMeshCutoutConsumer::Rendering
            ? cutout.m_renderWorldBounds : cutout.m_collisionWorldBounds;
        const double localMargin = consumer == TerrainMeshCutoutConsumer::Rendering
            ? cutout.m_renderLocalMargin : cutout.m_collisionLocalMargin;
        if (!enabled || !cutout.m_data || !worldSurfacePoint.IsFinite() || !worldBounds.Contains(worldSurfacePoint))
        {
            return false;
        }
        const AZ::Vector3 localPoint = cutout.m_localFromWorld.TransformPoint(worldSurfacePoint);
        if (!localPoint.IsFinite())
        {
            return false;
        }
        const double boundaryTolerance = std::max(1.0e-7,
            double(cutout.m_data->m_localBounds.GetExtents().GetLength()) * 1.0e-7);
        if (IsWithinDistance(localPoint, *cutout.m_data, localMargin + boundaryTolerance))
        {
            return true;
        }
        return IsInside(localPoint, *cutout.m_data);
    }

    void ApplyTerrainMeshCutoutCollisionCellPadding(
        PreparedTerrainMeshCutout& cutout, float worldHeightfieldGridSpacing)
    {
        if (!cutout.m_affectTerrainCollisionQueries || !cutout.m_data ||
            !std::isfinite(worldHeightfieldGridSpacing) || worldHeightfieldGridSpacing <= 0.0f)
        {
            return;
        }
        const double worldPadding = double(worldHeightfieldGridSpacing) * std::sqrt(2.0);
        const double scale = cutout.m_worldFromLocal.GetUniformScale();
        cutout.m_collisionLocalMargin += worldPadding / scale;
        cutout.m_collisionWorldBounds.Expand(AZ::Vector3(aznumeric_cast<float>(worldPadding)));
    }

    const char* GetTerrainMeshCutoutPlacementValidationMessage(TerrainMeshCutoutPlacementValidation validation)
    {
        switch (validation)
        {
        case TerrainMeshCutoutPlacementValidation::Valid: return "Ready: cutter placement is valid.";
        case TerrainMeshCutoutPlacementValidation::DataUnavailable: return "The selected cutter model is loading, missing, failed, or invalid.";
        case TerrainMeshCutoutPlacementValidation::TransformUnavailable: return "Entity world transform is unavailable.";
        case TerrainMeshCutoutPlacementValidation::NonUniformScale: return "Non-uniform scale is unsupported; use positive uniform scale.";
        case TerrainMeshCutoutPlacementValidation::Transform: return "Transform must be finite with normalized rotation and positive uniform scale.";
        case TerrainMeshCutoutPlacementValidation::Margin: return "Render and Collision Margins must be finite and nonnegative.";
        case TerrainMeshCutoutPlacementValidation::OrderingIdentity: return "Ordering identity is unresolved or malformed.";
        case TerrainMeshCutoutPlacementValidation::Bounds: return "Transformed cutter bounds are not finite or representable.";
        }
        return "Cutter placement is unsupported.";
    }
} // namespace TerrainCompositor
