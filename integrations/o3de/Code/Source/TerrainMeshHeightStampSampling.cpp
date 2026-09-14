#include <TerrainCompositor/TerrainMeshHeightStampSampling.h>
#include "StampMath.h"
#include <TerrainCompositor/TerrainMeshHeightMapping.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace TerrainCompositor
{
    TerrainMeshHeightStampPlacementValidation PrepareTerrainMeshHeightStamp(
        const TerrainMeshHeightStampRegistrationData& registration, bool hasNonUniformScale, PreparedTerrainMeshHeightStamp& result)
    {
        result = {};
        const auto& config = registration.m_configuration;
        if (registration.m_mesh.m_status != TerrainMeshHeightDataStatus::Ready || !registration.m_mesh.m_data)
        {
            return TerrainMeshHeightStampPlacementValidation::DataUnavailable;
        }
        if (!registration.m_transformAvailable)
        {
            return TerrainMeshHeightStampPlacementValidation::MissingTransform;
        }
        if (hasNonUniformScale)
        {
            return TerrainMeshHeightStampPlacementValidation::NonUniformScale;
        }
        const auto& transform = registration.m_worldTransform;
        if (!transform.IsFinite())
        {
            return TerrainMeshHeightStampPlacementValidation::NonFiniteTransform;
        }
        const double scale = transform.GetUniformScale();
        if (!std::isfinite(scale) || scale <= 0.0)
        {
            return TerrainMeshHeightStampPlacementValidation::UniformScale;
        }
        double yaw;
        if (!Internal::TryGetStampYaw(transform.GetRotation(), yaw))
        {
            return TerrainMeshHeightStampPlacementValidation::Rotation;
        }

        if (!std::isfinite(config.m_strength) || config.m_strength < 0.0f || config.m_strength > 1.0f)
        {
            return TerrainMeshHeightStampPlacementValidation::Strength;
        }
        if (!std::isfinite(config.m_featherExponent) || config.m_featherExponent < 1.0f)
        {
            return TerrainMeshHeightStampPlacementValidation::FeatherExponent;
        }
        const auto& data = *registration.m_mesh.m_data;
        if (!data.m_localBounds.IsValid() || !data.m_localBounds.GetMin().IsFinite() || !data.m_localBounds.GetMax().IsFinite())
        {
            return TerrainMeshHeightStampPlacementValidation::Bounds;
        }
        const double width = double(data.m_localBounds.GetMax().GetX()) - data.m_localBounds.GetMin().GetX();
        const double depth = double(data.m_localBounds.GetMax().GetY()) - data.m_localBounds.GetMin().GetY();
        const double halfSmaller = 0.5 * std::min(width, depth);
        if (!std::isfinite(config.m_edgeInset) || config.m_edgeInset < 0.0f || config.m_edgeInset >= halfSmaller)
        {
            return TerrainMeshHeightStampPlacementValidation::EdgeInset;
        }
        if (!std::isfinite(config.m_featherWidth) || config.m_featherWidth < 0.0f ||
            double(config.m_featherWidth) > halfSmaller - config.m_edgeInset)
        {
            return TerrainMeshHeightStampPlacementValidation::Feather;
        }

        PreparedTerrainMeshHeightStamp prepared;
        prepared.m_data = registration.m_mesh.m_data;
        prepared.m_stampEntityId = registration.m_stampEntityId;
        prepared.m_stableOrderKey = config.GetRuntimeOrderKey();
        prepared.m_priority = config.m_priority;
        prepared.m_originX = transform.GetTranslation().GetX();
        prepared.m_originY = transform.GetTranslation().GetY();
        prepared.m_heightOrigin = transform.GetTranslation().GetZ();
        prepared.m_scale = scale;
        prepared.m_inverseScale = 1.0 / scale;
        prepared.m_cosYaw = std::cos(yaw);
        prepared.m_sinYaw = std::sin(yaw);
        prepared.m_localMinX = data.m_localBounds.GetMin().GetX();
        prepared.m_localMaxX = data.m_localBounds.GetMax().GetX();
        prepared.m_localMinY = data.m_localBounds.GetMin().GetY();
        prepared.m_localMaxY = data.m_localBounds.GetMax().GetY();
        prepared.m_strength = config.m_strength;
        prepared.m_feather = config.m_featherWidth;
        prepared.m_featherExponent = config.m_featherExponent;
        prepared.m_edgeInset = config.m_edgeInset;
        prepared.m_relativeEdgeBlend = config.m_relativeEdgeBlend;
        prepared.m_uncoveredAreaPolicy = config.m_uncoveredAreaPolicy;
        prepared.m_affectTerrainRendering = config.m_affectTerrainRendering;
        prepared.m_affectTerrainCollisionQueries = config.m_affectTerrainCollisionQueries;

        const auto bounds = Internal::TransformStampXYBounds(data.m_localBounds,
            prepared.m_originX, prepared.m_originY, scale, prepared.m_cosYaw, prepared.m_sinYaw);
        const double minimumHeight = prepared.m_heightOrigin + scale * data.m_localBounds.GetMin().GetZ();
        const double maximumHeight = prepared.m_heightOrigin + scale * data.m_localBounds.GetMax().GetZ();
        const double floatMaximum = std::numeric_limits<float>::max();
        if (!bounds.IsRepresentable() || !std::isfinite(minimumHeight) || !std::isfinite(maximumHeight) ||
            std::abs(minimumHeight) > floatMaximum || std::abs(maximumHeight) > floatMaximum)
        {
            return TerrainMeshHeightStampPlacementValidation::Bounds;
        }
        prepared.m_worldBounds = bounds.ToAabb();
        result = AZStd::move(prepared);
        return TerrainMeshHeightStampPlacementValidation::Valid;
    }

    bool SampleTerrainMeshHeightStamp(
        const AZ::Vector3& position, const PreparedTerrainMeshHeightStamp& stamp, TerrainMeshHeightContribution& result)
    {
        result = {};
        if (!stamp.m_data || !std::isfinite(position.GetX()) || !std::isfinite(position.GetY()) ||
            position.GetX() < stamp.m_worldBounds.GetMin().GetX() || position.GetX() > stamp.m_worldBounds.GetMax().GetX() ||
            position.GetY() < stamp.m_worldBounds.GetMin().GetY() || position.GetY() > stamp.m_worldBounds.GetMax().GetY())
        {
            return false;
        }
        float mappedX, mappedY;
        MapTerrainMeshHeightXY(position.GetX(), position.GetY(), float(stamp.m_originX), float(stamp.m_originY),
            float(stamp.m_cosYaw), float(stamp.m_sinYaw), float(stamp.m_inverseScale), mappedX, mappedY);
        const double localX = mappedX;
        const double localY = mappedY;
        const double edgeX = std::min(localX - stamp.m_localMinX, stamp.m_localMaxX - localX);
        const double edgeY = std::min(localY - stamp.m_localMinY, stamp.m_localMaxY - localY);
        if (edgeX < 0.0 || edgeY < 0.0 || (stamp.m_edgeInset > 0.0 && (edgeX <= stamp.m_edgeInset || edgeY <= stamp.m_edgeInset)))
        {
            return false;
        }
        double localHeight = 0.0;
        if (!SampleTerrainMeshLocalHeight(*stamp.m_data, localX, localY, localHeight))
        {
            return false;
        }
        TerrainMeshHeightContribution contribution;
        contribution.m_weight = stamp.m_strength;
        if (stamp.m_feather > 0.0)
        {
            const double featherX = Internal::SmoothStep01((edgeX - stamp.m_edgeInset) / stamp.m_feather);
            const double featherY = Internal::SmoothStep01((edgeY - stamp.m_edgeInset) / stamp.m_feather);
            contribution.m_replaceBlend = std::clamp(featherX * featherY, 0.0, 1.0);
            contribution.m_weight = stamp.m_strength *
                (stamp.m_featherExponent == 1.0 ? contribution.m_replaceBlend
                                                : ((contribution.m_replaceBlend == 0.0 || contribution.m_replaceBlend == 1.0)
                                                       ? contribution.m_replaceBlend
                                                       : std::pow(contribution.m_replaceBlend, stamp.m_featherExponent)));
        }
        contribution.m_displacement = stamp.m_scale * localHeight;
        contribution.m_targetHeight = stamp.m_heightOrigin + contribution.m_displacement;
        result = contribution;
        return contribution.m_weight > 0.0;
    }

    const char* GetTerrainMeshHeightStampPlacementValidationMessage(TerrainMeshHeightStampPlacementValidation validation)
    {
        switch (validation)
        {
        case TerrainMeshHeightStampPlacementValidation::Valid:
            return "Valid";
        case TerrainMeshHeightStampPlacementValidation::DataUnavailable:
            return "Prepared terrain mesh height data is unavailable.";
        case TerrainMeshHeightStampPlacementValidation::MissingTransform:
            return "The matching terrain Mesh requires an active Transform.";
        case TerrainMeshHeightStampPlacementValidation::NonUniformScale:
            return "Terrain mesh height stamps require positive uniform scale.";
        case TerrainMeshHeightStampPlacementValidation::NonFiniteTransform:
            return "Terrain mesh placement must be finite.";
        case TerrainMeshHeightStampPlacementValidation::UniformScale:
            return "Terrain mesh placement scale must be positive.";
        case TerrainMeshHeightStampPlacementValidation::Rotation:
            return "Terrain mesh world orientation must be yaw-only with no pitch or roll.";
        case TerrainMeshHeightStampPlacementValidation::Strength:
            return "Strength must be finite and between 0 and 1.";
        case TerrainMeshHeightStampPlacementValidation::Feather:
            return "Feather must fit inside the mesh bounds after Edge Inset.";
        case TerrainMeshHeightStampPlacementValidation::FeatherExponent:
            return "Feather exponent must be finite and at least 1.";
        case TerrainMeshHeightStampPlacementValidation::EdgeInset:
            return "Edge Inset must be finite and smaller than half the mesh's smaller XY dimension.";
        case TerrainMeshHeightStampPlacementValidation::Bounds:
            return "Mesh bounds or transformed heights exceed the representable range.";
        }
        return "Unsupported terrain mesh placement.";
    }

    AZStd::string GetTerrainMeshHeightDataDiagnostic(const TerrainMeshHeightDataSnapshot& snapshot)
    {
        switch (snapshot.m_status)
        {
        case TerrainMeshHeightDataStatus::Unassigned:
            return "Select a Terrain Mesh model asset.";
        case TerrainMeshHeightDataStatus::Loading:
            return {};
        case TerrainMeshHeightDataStatus::Missing:
            return "The terrain model product is missing from the asset catalog.";
        case TerrainMeshHeightDataStatus::Error:
            return "The terrain model failed to load or reload.";
        case TerrainMeshHeightDataStatus::Unsupported:
            return "The selected asset is not an Atom Model product.";
        case TerrainMeshHeightDataStatus::InvalidModel:
            return GetTerrainModelGeometryValidationMessage(snapshot.m_modelValidation);
        case TerrainMeshHeightDataStatus::InvalidGeometry:
        {
            AZStd::string diagnostic = GetTerrainMeshHeightValidationMessage(snapshot.m_validation);
            if (!snapshot.m_diagnostics.m_details.empty())
            {
                const auto& detail = snapshot.m_diagnostics.m_details.front();
                if (detail.m_gridX != TerrainMeshHeightInvalidDiagnosticIndex &&
                    detail.m_gridY != TerrainMeshHeightInvalidDiagnosticIndex)
                {
                    diagnostic += AZStd::string::format(" Grid location (%u, %u).", detail.m_gridX, detail.m_gridY);
                }
                if (detail.m_triangleIndex != TerrainMeshHeightInvalidDiagnosticIndex)
                {
                    diagnostic += AZStd::string::format(" Triangle %u.", detail.m_triangleIndex);
                }
                if (detail.m_relatedTriangleIndex != TerrainMeshHeightInvalidDiagnosticIndex)
                {
                    diagnostic += AZStd::string::format(" Related triangle %u.", detail.m_relatedTriangleIndex);
                }
                if (snapshot.m_diagnostics.m_totalOffenseCount > snapshot.m_diagnostics.m_details.size())
                {
                    diagnostic += AZStd::string::format(
                        " %llu total offenses; first %zu retained.",
                        static_cast<unsigned long long>(snapshot.m_diagnostics.m_totalOffenseCount),
                        snapshot.m_diagnostics.m_details.size());
                }
            }
            return diagnostic;
        }
        case TerrainMeshHeightDataStatus::Ready:
            return "Prepared terrain mesh height data is unavailable.";
        }
        return "Terrain mesh height data is unavailable.";
    }
} // namespace TerrainCompositor
