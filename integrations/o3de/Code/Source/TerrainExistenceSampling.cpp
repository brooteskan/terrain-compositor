#include <TerrainCompositor/TerrainExistenceSampling.h>
#include <TerrainCompositor/TerrainMeshHeightMapping.h>

#include <AzCore/std/containers/array.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace TerrainCompositor
{
    namespace
    {
        bool HasValidImage(const HeightmapDataPtr& image)
        {
            return image && image->m_width > 0 && image->m_height > 0 &&
                size_t(image->m_height) <= std::numeric_limits<size_t>::max() / image->m_width &&
                image->m_samples.size() == size_t(image->m_width) * image->m_height;
        }

        double SampleBilinear(const HeightmapData& image, double u, double v)
        {
            const double pixelX = std::clamp(u, 0.0, 1.0) * (image.m_width - 1);
            const double pixelY = std::clamp(1.0 - v, 0.0, 1.0) * (image.m_height - 1);
            const size_t x0 = static_cast<size_t>(pixelX);
            const size_t y0 = static_cast<size_t>(pixelY);
            const size_t x1 = std::min(x0 + 1, size_t(image.m_width - 1));
            const size_t y1 = std::min(y0 + 1, size_t(image.m_height - 1));
            const double tx = pixelX - double(x0);
            const double ty = pixelY - double(y0);
            const size_t row0 = y0 * size_t(image.m_width);
            const size_t row1 = y1 * size_t(image.m_width);
            const double top = image.m_samples[row0 + x0] * (1.0 - tx) + image.m_samples[row0 + x1] * tx;
            const double bottom = image.m_samples[row1 + x0] * (1.0 - tx) + image.m_samples[row1 + x1] * tx;
            return top * (1.0 - ty) + bottom * ty;
        }

        float RoundGapBound(double value, bool lower)
        {
            float result = static_cast<float>(value);
            if ((lower && result > value) || (!lower && result < value))
            {
                result = std::nextafter(result, lower ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity());
            }
            return result;
        }

        bool HeightfieldCellLess(const TerrainHeightfieldCellAddress& left, const TerrainHeightfieldCellAddress& right)
        {
            return left.m_y != right.m_y ? left.m_y < right.m_y : left.m_x < right.m_x;
        }

        struct Double2
        {
            double m_x = 0.0;
            double m_y = 0.0;
        };

        Double2 TransformGapPoint(const PreparedTerrainMeshHeightGap& gap, double localX, double localY)
        {
            const double scale = 1.0 / gap.m_inverseScale;
            return {
                gap.m_originX + scale * (gap.m_cosYaw * localX - gap.m_sinYaw * localY),
                gap.m_originY + scale * (gap.m_sinYaw * localX + gap.m_cosYaw * localY)
            };
        }

        bool IntersectsHeightfieldCell(
            const AZStd::array<Double2, 4>& gapCell,
            AZ::s64 terrainCellX,
            AZ::s64 terrainCellY,
            double spacing)
        {
            const double minimumX = double(terrainCellX) * spacing;
            const double minimumY = double(terrainCellY) * spacing;
            const AZStd::array<Double2, 4> terrainCell{
                Double2{ minimumX, minimumY },
                Double2{ minimumX + spacing, minimumY },
                Double2{ minimumX + spacing, minimumY + spacing },
                Double2{ minimumX, minimumY + spacing }
            };
            const Double2 edgeX{ gapCell[1].m_x - gapCell[0].m_x, gapCell[1].m_y - gapCell[0].m_y };
            const Double2 edgeY{ gapCell[3].m_x - gapCell[0].m_x, gapCell[3].m_y - gapCell[0].m_y };
            const AZStd::array<Double2, 4> axes{
                Double2{ 1.0, 0.0 }, Double2{ 0.0, 1.0 },
                Double2{ -edgeX.m_y, edgeX.m_x }, Double2{ -edgeY.m_y, edgeY.m_x }
            };
            for (const Double2& axis : axes)
            {
                double gapMinimum = std::numeric_limits<double>::max();
                double gapMaximum = -std::numeric_limits<double>::max();
                double terrainMinimum = std::numeric_limits<double>::max();
                double terrainMaximum = -std::numeric_limits<double>::max();
                for (const Double2& point : gapCell)
                {
                    const double projection = point.m_x * axis.m_x + point.m_y * axis.m_y;
                    gapMinimum = std::min(gapMinimum, projection);
                    gapMaximum = std::max(gapMaximum, projection);
                }
                for (const Double2& point : terrainCell)
                {
                    const double projection = point.m_x * axis.m_x + point.m_y * axis.m_y;
                    terrainMinimum = std::min(terrainMinimum, projection);
                    terrainMaximum = std::max(terrainMaximum, projection);
                }
                const double magnitude = std::max(
                    { 1.0, std::abs(gapMinimum), std::abs(gapMaximum), std::abs(terrainMinimum), std::abs(terrainMaximum) });
                const double tolerance = 32.0 * std::numeric_limits<double>::epsilon() * magnitude;
                if (gapMaximum < terrainMinimum - tolerance || terrainMaximum < gapMinimum - tolerance)
                {
                    return false;
                }
            }
            return true;
        }

        PreparedTerrainHeightfieldCellMaskPtr PrepareHeightfieldCellMask(
            const PreparedTerrainMeshHeightGap& gap,
            float worldHeightfieldGridSpacing,
            const AZ::Aabb& terrainRegionBounds)
        {
            if (!gap.m_affectTerrainCollisionQueries || !gap.m_data || !std::isfinite(worldHeightfieldGridSpacing) ||
                worldHeightfieldGridSpacing <= 0.0f || !std::isfinite(gap.m_inverseScale) || gap.m_inverseScale <= 0.0 ||
                !terrainRegionBounds.IsValid() || !terrainRegionBounds.GetMin().IsFinite() || !terrainRegionBounds.GetMax().IsFinite())
            {
                return {};
            }

            constexpr size_t MaximumPreparedCollisionCells = 4 * 1024 * 1024;
            const double spacing = worldHeightfieldGridSpacing;
            const double minimumGridXValue = std::ceil(double(terrainRegionBounds.GetMin().GetX()) / spacing);
            const double minimumGridYValue = std::ceil(double(terrainRegionBounds.GetMin().GetY()) / spacing);
            const double maximumGridXValue = std::floor(double(terrainRegionBounds.GetMax().GetX()) / spacing) - 1.0;
            const double maximumGridYValue = std::floor(double(terrainRegionBounds.GetMax().GetY()) / spacing) - 1.0;
            constexpr double MinimumIndex = double(std::numeric_limits<AZ::s64>::min() + 1);
            constexpr double MaximumIndex = double(std::numeric_limits<AZ::s64>::max() - 1);
            if (minimumGridXValue < MinimumIndex || minimumGridYValue < MinimumIndex || maximumGridXValue > MaximumIndex ||
                maximumGridYValue > MaximumIndex || maximumGridXValue < minimumGridXValue || maximumGridYValue < minimumGridYValue)
            {
                return {};
            }
            const AZ::s64 regionMinimumX = aznumeric_cast<AZ::s64>(minimumGridXValue);
            const AZ::s64 regionMinimumY = aznumeric_cast<AZ::s64>(minimumGridYValue);
            const AZ::s64 regionMaximumX = aznumeric_cast<AZ::s64>(maximumGridXValue);
            const AZ::s64 regionMaximumY = aznumeric_cast<AZ::s64>(maximumGridYValue);

            auto prepared = std::make_shared<PreparedTerrainHeightfieldCellMask>();
            prepared->m_meshRevision = gap.m_data->m_revision;
            prepared->m_gridSpacing = worldHeightfieldGridSpacing;
            prepared->m_regionBounds = terrainRegionBounds;
            const double localOriginX = gap.m_data->m_localOrigin.GetX();
            const double localOriginY = gap.m_data->m_localOrigin.GetY();
            const double localSpacingX = gap.m_data->m_gridSpacing.GetX();
            const double localSpacingY = gap.m_data->m_gridSpacing.GetY();
            const size_t cellWidth = gap.m_data->m_width - 1;
            const size_t cellHeight = gap.m_data->m_height - 1;
            for (size_t y = 0; y < cellHeight; ++y)
            {
                for (size_t x = 0; x < cellWidth; ++x)
                {
                    if (!IsTerrainMeshHeightCellUncovered(*gap.m_data, y * cellWidth + x))
                    {
                        continue;
                    }
                    const double localMinimumX = localOriginX + double(x) * localSpacingX;
                    const double localMinimumY = localOriginY + double(y) * localSpacingY;
                    const double localMaximumX = localMinimumX + localSpacingX;
                    const double localMaximumY = localMinimumY + localSpacingY;
                    const AZStd::array<Double2, 4> worldCell{
                        TransformGapPoint(gap, localMinimumX, localMinimumY),
                        TransformGapPoint(gap, localMaximumX, localMinimumY),
                        TransformGapPoint(gap, localMaximumX, localMaximumY),
                        TransformGapPoint(gap, localMinimumX, localMaximumY)
                    };
                    double worldMinimumX = std::numeric_limits<double>::max();
                    double worldMinimumY = std::numeric_limits<double>::max();
                    double worldMaximumX = -std::numeric_limits<double>::max();
                    double worldMaximumY = -std::numeric_limits<double>::max();
                    for (const Double2& point : worldCell)
                    {
                        worldMinimumX = std::min(worldMinimumX, point.m_x);
                        worldMinimumY = std::min(worldMinimumY, point.m_y);
                        worldMaximumX = std::max(worldMaximumX, point.m_x);
                        worldMaximumY = std::max(worldMaximumY, point.m_y);
                    }
                    const double beginXValue = std::floor(worldMinimumX / spacing);
                    const double beginYValue = std::floor(worldMinimumY / spacing);
                    const double endXValue = std::floor(std::nextafter(worldMaximumX, -std::numeric_limits<double>::infinity()) / spacing);
                    const double endYValue = std::floor(std::nextafter(worldMaximumY, -std::numeric_limits<double>::infinity()) / spacing);
                    if (beginXValue < MinimumIndex || beginYValue < MinimumIndex || endXValue > MaximumIndex || endYValue > MaximumIndex)
                    {
                        return {};
                    }
                    const AZ::s64 beginX = AZStd::max(regionMinimumX, aznumeric_cast<AZ::s64>(beginXValue));
                    const AZ::s64 beginY = AZStd::max(regionMinimumY, aznumeric_cast<AZ::s64>(beginYValue));
                    const AZ::s64 endX = AZStd::min(regionMaximumX, aznumeric_cast<AZ::s64>(endXValue));
                    const AZ::s64 endY = AZStd::min(regionMaximumY, aznumeric_cast<AZ::s64>(endYValue));
                    for (AZ::s64 terrainY = beginY; terrainY <= endY; ++terrainY)
                    {
                        for (AZ::s64 terrainX = beginX; terrainX <= endX; ++terrainX)
                        {
                            if (IntersectsHeightfieldCell(worldCell, terrainX, terrainY, spacing))
                            {
                                if (prepared->m_cells.size() >= MaximumPreparedCollisionCells)
                                {
                                    return {};
                                }
                                prepared->m_cells.push_back({ terrainX, terrainY });
                            }
                        }
                    }
                }
            }
            AZStd::sort(prepared->m_cells.begin(), prepared->m_cells.end(), HeightfieldCellLess);
            prepared->m_cells.erase(AZStd::unique(prepared->m_cells.begin(), prepared->m_cells.end()), prepared->m_cells.end());
            for (const TerrainHeightfieldCellAddress& cell : prepared->m_cells)
            {
                const AZ::Vector3 minimum(
                    RoundGapBound(double(cell.m_x) * spacing, true),
                    RoundGapBound(double(cell.m_y) * spacing, true), 0.0f);
                const AZ::Vector3 maximum(
                    RoundGapBound(double(cell.m_x + 1) * spacing, false),
                    RoundGapBound(double(cell.m_y + 1) * spacing, false), 0.0f);
                prepared->m_worldBounds.AddPoint(minimum);
                prepared->m_worldBounds.AddPoint(maximum);
            }
            return prepared;
        }
    } // namespace

    AZ::s32 PreparedTerrainExistenceContributor::GetPriority() const
    {
        switch (m_type)
        {
        case Type::ImageMask:
            return m_imageMask.m_placement.m_priority;
        case Type::MeshCutout:
            return m_meshCutout.m_priority;
        case Type::MeshHeightGap:
            return m_meshHeightGap.m_priority;
        }
        return 0;
    }

    AZStd::string_view PreparedTerrainExistenceContributor::GetStableOrderKey() const
    {
        switch (m_type)
        {
        case Type::ImageMask:
            return m_imageMask.m_placement.m_stableOrderKey;
        case Type::MeshCutout:
            return m_meshCutout.m_stableOrderKey;
        case Type::MeshHeightGap:
            return m_meshHeightGap.m_stableOrderKey;
        }
        return {};
    }

    AZ::EntityId PreparedTerrainExistenceContributor::GetEntityId() const
    {
        switch (m_type)
        {
        case Type::ImageMask:
            return m_imageMask.m_placement.m_stampEntityId;
        case Type::MeshCutout:
            return m_meshCutout.m_entityId;
        case Type::MeshHeightGap:
            return m_meshHeightGap.m_entityId;
        }
        return AZ::EntityId(AZ::u64{ 0 });
    }

    const AZ::Aabb& PreparedTerrainExistenceContributor::GetWorldBounds() const
    {
        switch (m_type)
        {
        case Type::ImageMask:
            return m_imageMask.m_placement.m_worldBounds;
        case Type::MeshCutout:
            return m_meshCutout.m_collisionWorldBounds;
        case Type::MeshHeightGap:
            return m_meshHeightGap.m_worldBounds;
        }
        return m_imageMask.m_placement.m_worldBounds;
    }

    TerrainExistenceStampValidation PrepareTerrainExistenceStamp(
        const HeightmapStampRegistrationData& registration,
        bool hasNonUniformScale,
        PreparedTerrainExistenceStamp& result,
        HeightmapStampValidation* placementValidation)
    {
        result = {};
        const auto& holes = registration.m_configuration.m_holeMask;
        if (!holes.m_maskAsset.GetId().IsValid())
        {
            return TerrainExistenceStampValidation::Absent;
        }
        if (!std::isfinite(holes.m_threshold) || holes.m_threshold < 0.0f || holes.m_threshold > 1.0f)
        {
            return TerrainExistenceStampValidation::Threshold;
        }

        PreparedStampPlacement placement;
        const auto placementResult = PrepareStampPlacement(registration, hasNonUniformScale, placement);
        if (placementValidation)
        {
            *placementValidation = placementResult;
        }
        if (placementResult != HeightmapStampValidation::Valid)
        {
            return TerrainExistenceStampValidation::Placement;
        }
        if (registration.m_holeMask.m_status != HeightmapDataStatus::Ready || !registration.m_holeMask.m_data)
        {
            return TerrainExistenceStampValidation::DataUnavailable;
        }
        if (!HasValidImage(registration.m_holeMask.m_data))
        {
            return TerrainExistenceStampValidation::ImageData;
        }

        result.m_placement = AZStd::move(placement);
        result.m_mask = registration.m_holeMask.m_data;
        result.m_maskRevision = registration.m_holeMask.m_revision;
        result.m_threshold = holes.m_threshold;
        result.m_operation = holes.m_operation;
        return TerrainExistenceStampValidation::Valid;
    }

    const char* GetTerrainExistenceStampValidationMessage(TerrainExistenceStampValidation validation)
    {
        switch (validation)
        {
        case TerrainExistenceStampValidation::Valid:
            return "Valid terrain hole mask.";
        case TerrainExistenceStampValidation::Absent:
            return "No terrain hole mask assigned.";
        case TerrainExistenceStampValidation::Placement:
            return "Hole-mask placement is invalid; see the stamp placement "
                   "diagnostic.";
        case TerrainExistenceStampValidation::Threshold:
            return "Hole threshold must be finite and between zero and one.";
        case TerrainExistenceStampValidation::DataUnavailable:
            return "The selected hole mask is loading, missing, failed, or "
                   "unsupported.";
        case TerrainExistenceStampValidation::ImageData:
            return "Hole-mask mip-zero dimensions or normalized samples are malformed.";
        }
        return "Unsupported terrain hole-mask configuration.";
    }

    bool SampleTerrainExistenceStamp(const AZ::Vector3& position, const PreparedTerrainExistenceStamp& stamp, bool& terrainExists)
    {
        if (!stamp.m_mask)
        {
            return false;
        }
        MappedStampPosition mapped;
        if (!TryMapPositionToStamp(position, stamp.m_placement, mapped))
        {
            return false;
        }
        if (SampleBilinear(*stamp.m_mask, mapped.m_u, mapped.m_v) < stamp.m_threshold)
        {
            return false;
        }
        terrainExists = stamp.m_operation == TerrainExistenceOperation::RestoreTerrain;
        return true;
    }

    bool PrepareTerrainMeshHeightGap(
        const PreparedTerrainMeshHeightStamp& preparedHeight,
        float worldHeightfieldGridSpacing,
        const AZ::Aabb& terrainRegionBounds,
        PreparedTerrainMeshHeightGap& result)
    {
        result = {};
        if (!preparedHeight.m_data || preparedHeight.m_uncoveredAreaPolicy != TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain ||
            preparedHeight.m_data->m_uncoveredCellCount == 0 || !preparedHeight.m_data->m_localUncoveredBounds.IsValid() ||
            (!preparedHeight.m_affectTerrainRendering && !preparedHeight.m_affectTerrainCollisionQueries))
        {
            return false;
        }

        const AZ::Aabb& localBounds = preparedHeight.m_data->m_localUncoveredBounds;
        double minimumX = std::numeric_limits<double>::max();
        double maximumX = -std::numeric_limits<double>::max();
        double minimumY = std::numeric_limits<double>::max();
        double maximumY = -std::numeric_limits<double>::max();
        for (const double localX : { double(localBounds.GetMin().GetX()), double(localBounds.GetMax().GetX()) })
        {
            for (const double localY : { double(localBounds.GetMin().GetY()), double(localBounds.GetMax().GetY()) })
            {
                const double worldX = preparedHeight.m_originX +
                    preparedHeight.m_scale * (preparedHeight.m_cosYaw * localX - preparedHeight.m_sinYaw * localY);
                const double worldY = preparedHeight.m_originY +
                    preparedHeight.m_scale * (preparedHeight.m_sinYaw * localX + preparedHeight.m_cosYaw * localY);
                minimumX = std::min(minimumX, worldX);
                maximumX = std::max(maximumX, worldX);
                minimumY = std::min(minimumY, worldY);
                maximumY = std::max(maximumY, worldY);
            }
        }
        const double floatMaximum = std::numeric_limits<float>::max();
        if (!std::isfinite(minimumX) || !std::isfinite(maximumX) || !std::isfinite(minimumY) || !std::isfinite(maximumY) ||
            std::abs(minimumX) > floatMaximum || std::abs(maximumX) > floatMaximum || std::abs(minimumY) > floatMaximum ||
            std::abs(maximumY) > floatMaximum)
        {
            return false;
        }

        PreparedTerrainMeshHeightGap gap;
        gap.m_data = preparedHeight.m_data;
        gap.m_entityId = preparedHeight.m_stampEntityId;
        gap.m_stableOrderKey = preparedHeight.m_stableOrderKey;
        gap.m_priority = preparedHeight.m_priority;
        gap.m_originX = preparedHeight.m_originX;
        gap.m_originY = preparedHeight.m_originY;
        gap.m_inverseScale = preparedHeight.m_inverseScale;
        gap.m_cosYaw = preparedHeight.m_cosYaw;
        gap.m_sinYaw = preparedHeight.m_sinYaw;
        gap.m_affectTerrainRendering = preparedHeight.m_affectTerrainRendering;
        gap.m_affectTerrainCollisionQueries = preparedHeight.m_affectTerrainCollisionQueries;
        gap.m_worldBounds = AZ::Aabb::CreateFromMinMax(
            AZ::Vector3(RoundGapBound(minimumX, true), RoundGapBound(minimumY, true), 0.0f),
            AZ::Vector3(RoundGapBound(maximumX, false), RoundGapBound(maximumY, false), 0.0f));
        gap.m_collisionWorldBounds = gap.m_worldBounds;
        AZ::Aabb collisionRegion = terrainRegionBounds;
        if (!collisionRegion.IsValid() && std::isfinite(worldHeightfieldGridSpacing) && worldHeightfieldGridSpacing > 0.0f)
        {
            collisionRegion = gap.m_worldBounds;
            collisionRegion.Expand(AZ::Vector3(worldHeightfieldGridSpacing * 2.0f));
        }
        gap.m_collisionCells = PrepareHeightfieldCellMask(gap, worldHeightfieldGridSpacing, collisionRegion);
        // A caller that supplies a live terrain region requires a complete,
        // immutable collision view. Do not publish exact-only behavior when
        // preparation fails (for example because the grid is invalid or the
        // safety limit is exceeded), since that could leave a blocking lip.
        if (gap.m_affectTerrainCollisionQueries && terrainRegionBounds.IsValid() && !gap.m_collisionCells)
        {
            return false;
        }
        if (gap.m_collisionCells && gap.m_collisionCells->m_worldBounds.IsValid())
        {
            gap.m_collisionWorldBounds = gap.m_collisionCells->m_worldBounds;
        }
        result = AZStd::move(gap);
        return true;
    }

    bool PrepareTerrainMeshHeightGap(
        const PreparedTerrainMeshHeightStamp& preparedHeight, float worldHeightfieldGridSpacing, PreparedTerrainMeshHeightGap& result)
    {
        return PrepareTerrainMeshHeightGap(preparedHeight, worldHeightfieldGridSpacing, AZ::Aabb::CreateNull(), result);
    }

    bool SampleTerrainMeshHeightGap(
        const AZ::Vector3& position, const PreparedTerrainMeshHeightGap& gap, TerrainMeshHeightGapConsumer consumer)
    {
        const bool enabled =
            consumer == TerrainMeshHeightGapConsumer::Rendering ? gap.m_affectTerrainRendering : gap.m_affectTerrainCollisionQueries;
        const AZ::Aabb& bounds = consumer == TerrainMeshHeightGapConsumer::Collision ? gap.m_collisionWorldBounds : gap.m_worldBounds;
        if (!enabled || !gap.m_data || !position.IsFinite() || !bounds.IsValid() || position.GetX() < bounds.GetMin().GetX() ||
            position.GetX() > bounds.GetMax().GetX() || position.GetY() < bounds.GetMin().GetY() ||
            position.GetY() > bounds.GetMax().GetY())
        {
            return false;
        }

        float mappedX, mappedY;
        MapTerrainMeshHeightXY(position.GetX(), position.GetY(), float(gap.m_originX), float(gap.m_originY),
            float(gap.m_cosYaw), float(gap.m_sinYaw), float(gap.m_inverseScale), mappedX, mappedY);
        if (!std::isfinite(mappedX) || !std::isfinite(mappedY)) return false;
        const double localX = mappedX;
        const double localY = mappedY;
        if (consumer == TerrainMeshHeightGapConsumer::Collision && gap.m_collisionCells)
        {
            const double spacing = gap.m_collisionCells->m_gridSpacing;
            const AZ::Vector2 cellMinimum(
                aznumeric_cast<float>(std::floor(double(position.GetX()) / spacing) * spacing),
                aznumeric_cast<float>(std::floor(double(position.GetY()) / spacing) * spacing));
            return IsTerrainMeshHeightGapCollisionCell(cellMinimum, AZ::Vector2(float(spacing)), gap);
        }
        if (consumer != TerrainMeshHeightGapConsumer::Collision || gap.m_collisionWorldPadding <= 0.0)
        {
            TerrainMeshHeightCellAddress address;
            if (!ResolveTerrainMeshHeightCell(*gap.m_data, localX, localY, address) ||
                GetTerrainMeshHeightGapTileOccupancy(*gap.m_data, address.m_x, address.m_y) == TerrainMeshHeightGapTileOccupancy::Covered)
            {
                return false;
            }
            return IsTerrainMeshHeightCellUncovered(*gap.m_data, address.m_index);
        }

        const double spacingX = gap.m_data->m_gridSpacing.GetX();
        const double spacingY = gap.m_data->m_gridSpacing.GetY();
        const double originX = gap.m_data->m_localOrigin.GetX();
        const double originY = gap.m_data->m_localOrigin.GetY();
        const double localPadding = gap.m_collisionWorldPadding * gap.m_inverseScale;
        const int maximumCellX = aznumeric_cast<int>(gap.m_data->m_width) - 2;
        const int maximumCellY = aznumeric_cast<int>(gap.m_data->m_height) - 2;
        const int beginX = AZStd::max(0, aznumeric_cast<int>(std::floor((localX - localPadding - originX) / spacingX)));
        const int endX = AZStd::min(maximumCellX, aznumeric_cast<int>(std::floor((localX + localPadding - originX) / spacingX)));
        const int beginY = AZStd::max(0, aznumeric_cast<int>(std::floor((localY - localPadding - originY) / spacingY)));
        const int endY = AZStd::min(maximumCellY, aznumeric_cast<int>(std::floor((localY + localPadding - originY) / spacingY)));
        const double paddingSquared = localPadding * localPadding;
        const size_t cellWidth = gap.m_data->m_width - 1;
        for (int y = beginY; y <= endY; ++y)
        {
            for (int x = beginX; x <= endX; ++x)
            {
                if (GetTerrainMeshHeightGapTileOccupancy(*gap.m_data, x, y) == TerrainMeshHeightGapTileOccupancy::Covered ||
                    !IsTerrainMeshHeightCellUncovered(*gap.m_data, size_t(y) * cellWidth + x))
                {
                    continue;
                }
                const double cellMinX = originX + x * spacingX;
                const double cellMaxX = cellMinX + spacingX;
                const double cellMinY = originY + y * spacingY;
                const double cellMaxY = cellMinY + spacingY;
                const double distanceX = localX < cellMinX ? cellMinX - localX : localX > cellMaxX ? localX - cellMaxX : 0.0;
                const double distanceY = localY < cellMinY ? cellMinY - localY : localY > cellMaxY ? localY - cellMaxY : 0.0;
                if (distanceX * distanceX + distanceY * distanceY <= paddingSquared)
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool IsTerrainMeshHeightGapCollisionCell(
        const AZ::Vector2& worldCellMinimum,
        const AZ::Vector2& gridSpacing,
        const PreparedTerrainMeshHeightGap& gap)
    {
        const auto& prepared = gap.m_collisionCells;
        if (!gap.m_affectTerrainCollisionQueries || !prepared || prepared->m_cells.empty() || !worldCellMinimum.IsFinite() ||
            !gridSpacing.IsFinite() || gridSpacing.GetX() != prepared->m_gridSpacing || gridSpacing.GetY() != prepared->m_gridSpacing)
        {
            return false;
        }
        const double spacing = prepared->m_gridSpacing;
        const double x = double(worldCellMinimum.GetX()) / spacing;
        const double y = double(worldCellMinimum.GetY()) / spacing;
        if (!std::isfinite(x) || !std::isfinite(y) || x < double(std::numeric_limits<AZ::s64>::min()) ||
            x > double(std::numeric_limits<AZ::s64>::max()) || y < double(std::numeric_limits<AZ::s64>::min()) ||
            y > double(std::numeric_limits<AZ::s64>::max()))
        {
            return false;
        }
        const TerrainHeightfieldCellAddress address{ aznumeric_cast<AZ::s64>(std::llround(x)), aznumeric_cast<AZ::s64>(std::llround(y)) };
        const auto found = AZStd::lower_bound(prepared->m_cells.begin(), prepared->m_cells.end(), address, HeightfieldCellLess);
        return found != prepared->m_cells.end() && *found == address;
    }

    bool ComposeTerrainExists(const AZ::Vector3& position, bool baseExists, AZStd::span<const PreparedTerrainExistenceStamp> stamps)
    {
        bool result = baseExists;
        for (const auto& stamp : stamps)
        {
            bool authoredExists = result;
            if (SampleTerrainExistenceStamp(position, stamp, authoredExists))
            {
                result = authoredExists;
            }
        }
        return result;
    }

    bool ComposeTerrainExists(
        const AZ::Vector3& surfacePoint, bool baseExists, AZStd::span<const PreparedTerrainExistenceContributor> contributors,
        const AZStd::span<const PreparedTerrainMeshHeightGap>* admitted)
    {
        bool result = baseExists;
        for (const auto& contributor : contributors)
        {
            if (contributor.m_type == PreparedTerrainExistenceContributor::Type::MeshHeightGap)
            {
                continue;
            }
            if (contributor.m_type == PreparedTerrainExistenceContributor::Type::ImageMask)
            {
                bool authored = result;
                if (SampleTerrainExistenceStamp(surfacePoint, contributor.m_imageMask, authored))
                {
                    result = authored;
                }
            }
            else if (SampleTerrainMeshCutout(surfacePoint, contributor.m_meshCutout, TerrainMeshCutoutConsumer::CollisionQueries))
            {
                result = contributor.m_meshCutout.m_operation == TerrainExistenceOperation::RestoreTerrain;
            }
        }
        for (const auto& contributor : contributors)
        {
            if (contributor.m_type == PreparedTerrainExistenceContributor::Type::MeshHeightGap &&
                SampleTerrainMeshHeightGap(surfacePoint, contributor.m_meshHeightGap, TerrainMeshHeightGapConsumer::Queries) &&
                (!admitted || IsTerrainMeshHeightGapAdmitted(contributor.m_meshHeightGap, *admitted)))
            {
                return false;
            }
        }
        return result;
    }

    bool SameTerrainMeshHeightGap(const PreparedTerrainMeshHeightGap& left, const PreparedTerrainMeshHeightGap& right)
    {
        return left.m_data == right.m_data && left.m_compositionSession == right.m_compositionSession &&
            left.m_entityId == right.m_entityId && left.m_originX == right.m_originX && left.m_originY == right.m_originY &&
            left.m_inverseScale == right.m_inverseScale && left.m_cosYaw == right.m_cosYaw && left.m_sinYaw == right.m_sinYaw &&
            left.m_affectTerrainRendering == right.m_affectTerrainRendering;
    }

    bool IsTerrainMeshHeightGapAdmitted(const PreparedTerrainMeshHeightGap& gap, AZStd::span<const PreparedTerrainMeshHeightGap> admitted)
    {
        if (!gap.m_affectTerrainRendering) return true;
        return AZStd::any_of(admitted.begin(), admitted.end(), [&](const auto& candidate) { return SameTerrainMeshHeightGap(gap, candidate); });
    }

    bool ComposeTerrainRenderGeometryExists(
        const AZ::Vector3& surfacePoint, bool baseExists, AZStd::span<const PreparedTerrainExistenceContributor> contributors)
    {
        bool result = baseExists;
        for (const auto& contributor : contributors)
        {
            if (contributor.m_type != PreparedTerrainExistenceContributor::Type::ImageMask)
            {
                continue;
            }
            bool authored = result;
            if (SampleTerrainExistenceStamp(surfacePoint, contributor.m_imageMask, authored))
            {
                result = authored;
            }
        }
        return result;
    }
} // namespace TerrainCompositor
