#pragma once

#include <AzCore/std/algorithm.h>
#include <AzCore/std/containers/span.h>
#include <AzCore/std/containers/vector.h>
#include <TerrainCompositor/HeightmapStampSampling.h>
#include <TerrainCompositor/TerrainExistenceConfig.h>
#include <TerrainCompositor/TerrainMeshCutoutSampling.h>
#include <memory>
#include <cmath>
#include <limits>

namespace TerrainCompositor
{
    enum class TerrainExistenceStampValidation
    {
        Valid,
        Absent,
        Placement,
        Threshold,
        DataUnavailable,
        ImageData
    };

    struct PreparedTerrainExistenceStamp
    {
        PreparedStampPlacement m_placement;
        HeightmapDataPtr m_mask;
        AZ::u64 m_maskRevision = 0;
        double m_threshold = 0.5;
        TerrainExistenceOperation m_operation = TerrainExistenceOperation::RemoveTerrain;
    };

    enum class TerrainMeshHeightGapConsumer : AZ::u8
    {
        Rendering,
        Queries,
        Collision
    };

    //! Integer address of a terrain heightfield cell. The address is relative to
    //! the global terrain grid, not to a particular collider update region.
    struct TerrainHeightfieldCellAddress
    {
        AZ::s64 m_x = 0;
        AZ::s64 m_y = 0;

        bool operator==(const TerrainHeightfieldCellAddress&) const = default;
    };

    //! Immutable conservative classification prepared on the composition control
    //! thread. Terrain/physics workers only perform a binary search in m_cells.
    struct PreparedTerrainHeightfieldCellMask
    {
        AZ::u64 m_meshRevision = 0;
        float m_gridSpacing = 0.0f;
        AZ::Aabb m_regionBounds = AZ::Aabb::CreateNull();
        AZ::Aabb m_worldBounds = AZ::Aabb::CreateNull();
        AZStd::vector<TerrainHeightfieldCellAddress> m_cells;
    };
    using PreparedTerrainHeightfieldCellMaskPtr = std::shared_ptr<const PreparedTerrainHeightfieldCellMask>;

    //! Shared by the engine boundary and compositor; no compositor-library link is required.
    inline bool IsTerrainHeightfieldCellMasked(const PreparedTerrainHeightfieldCellMask& prepared,
        const AZ::Vector2& worldCellMinimum, const AZ::Vector2& gridSpacing)
    {
        const auto less = [](const TerrainHeightfieldCellAddress& left, const TerrainHeightfieldCellAddress& right)
        {
            return left.m_y < right.m_y || (left.m_y == right.m_y && left.m_x < right.m_x);
        };
        if (prepared.m_cells.empty() || !worldCellMinimum.IsFinite() ||
            !gridSpacing.IsFinite() || gridSpacing.GetX() <= 0.0f || gridSpacing.GetY() <= 0.0f ||
            !std::isfinite(prepared.m_gridSpacing) || prepared.m_gridSpacing <= 0.0f)
        {
            return false;
        }
        const double spacing = prepared.m_gridSpacing;
        const double x = double(worldCellMinimum.GetX()) / spacing;
        const double y = double(worldCellMinimum.GetY()) / spacing;
        if (gridSpacing.GetX() != prepared.m_gridSpacing || gridSpacing.GetY() != prepared.m_gridSpacing)
        {
            // Physics tiles may use a different sample grid from visual terrain.
            // Remove every physics cell overlapping a conservatively removed source cell.
            const double lastX = std::ceil((double(worldCellMinimum.GetX()) + gridSpacing.GetX()) / spacing) - 1.0;
            const double lastY = std::ceil((double(worldCellMinimum.GetY()) + gridSpacing.GetY()) / spacing) - 1.0;
            constexpr double Limit = 9.0e18;
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(lastX) || !std::isfinite(lastY) ||
                std::abs(x) > Limit || std::abs(y) > Limit || std::abs(lastX) > Limit || std::abs(lastY) > Limit)
                return false;
            const auto minX = static_cast<AZ::s64>(std::floor(x));
            const auto minY = static_cast<AZ::s64>(std::floor(y));
            const auto maxX = static_cast<AZ::s64>(lastX);
            const auto maxY = static_cast<AZ::s64>(lastY);
            auto cell = AZStd::lower_bound(prepared.m_cells.begin(), prepared.m_cells.end(),
                TerrainHeightfieldCellAddress{minX, minY}, less);
            while (cell != prepared.m_cells.end() && cell->m_y <= maxY)
            {
                if (cell->m_x >= minX && cell->m_x <= maxX) return true;
                const auto nextRow = cell->m_y + 1;
                cell = AZStd::lower_bound(cell, prepared.m_cells.end(),
                    TerrainHeightfieldCellAddress{minX, nextRow}, less);
            }
            return false;
        }
        if (!std::isfinite(x) || !std::isfinite(y) || x < double(std::numeric_limits<AZ::s64>::min()) ||
            x > double(std::numeric_limits<AZ::s64>::max()) || y < double(std::numeric_limits<AZ::s64>::min()) ||
            y > double(std::numeric_limits<AZ::s64>::max()))
        {
            return false;
        }
        const TerrainHeightfieldCellAddress address{ aznumeric_cast<AZ::s64>(std::llround(x)), aznumeric_cast<AZ::s64>(std::llround(y)) };
        const auto found = AZStd::lower_bound(prepared.m_cells.begin(), prepared.m_cells.end(), address, less);
        return found != prepared.m_cells.end() && *found == address;
    }


    //! Remove-only XY contributor derived from the same immutable mesh-height
    //! revision and prepared placement as its height record.
    struct PreparedTerrainMeshHeightGap
    {
        TerrainMeshHeightDataPtr m_data;
        AZ::Aabb m_worldBounds = AZ::Aabb::CreateNull();
        AZ::Aabb m_collisionWorldBounds = AZ::Aabb::CreateNull();
        AZ::EntityId m_entityId{};
        AZ::Uuid m_compositionSession{};
        AZStd::string m_stableOrderKey;
        AZ::s32 m_priority = 0;
        double m_originX = 0.0;
        double m_originY = 0.0;
        double m_inverseScale = 1.0;
        double m_cosYaw = 1.0;
        double m_sinYaw = 0.0;
        double m_collisionWorldPadding = 0.0;
        PreparedTerrainHeightfieldCellMaskPtr m_collisionCells;
        bool m_affectTerrainRendering = true;
        bool m_affectTerrainCollisionQueries = true;
    };

    //! Captured once per scalar/batch query. Never mutate a retained activation.
    struct TerrainMeshHeightGapActivation
    {
        AZ::u64 m_revision = 0;
        AZStd::vector<PreparedTerrainMeshHeightGap> m_gaps;
    };
    using TerrainMeshHeightGapActivationPtr = std::shared_ptr<const TerrainMeshHeightGapActivation>;
    // Terrain's engine overrides consume these predicates without linking the
    // compositor module. Keep their single implementation available to both DLLs.
    inline bool SameTerrainMeshHeightGap(const PreparedTerrainMeshHeightGap& left, const PreparedTerrainMeshHeightGap& right)
    {
        return left.m_data == right.m_data && left.m_compositionSession == right.m_compositionSession &&
            left.m_entityId == right.m_entityId && left.m_originX == right.m_originX && left.m_originY == right.m_originY &&
            left.m_inverseScale == right.m_inverseScale && left.m_cosYaw == right.m_cosYaw && left.m_sinYaw == right.m_sinYaw &&
            left.m_affectTerrainRendering == right.m_affectTerrainRendering;
    }

    inline bool IsTerrainMeshHeightGapAdmitted(const PreparedTerrainMeshHeightGap& gap, AZStd::span<const PreparedTerrainMeshHeightGap> admitted)
    {
        if (!gap.m_affectTerrainRendering) return true;
        return AZStd::any_of(admitted.begin(), admitted.end(), [&](const auto& candidate) { return SameTerrainMeshHeightGap(gap, candidate); });
    }

    struct PreparedTerrainExistenceContributor
    {
        enum class Type : AZ::u8
        {
            ImageMask,
            MeshCutout,
            MeshHeightGap
        };
        Type m_type = Type::ImageMask;
        PreparedTerrainExistenceStamp m_imageMask;
        PreparedTerrainMeshCutout m_meshCutout;
        PreparedTerrainMeshHeightGap m_meshHeightGap;

        AZ::s32 GetPriority() const;
        AZStd::string_view GetStableOrderKey() const;
        AZ::EntityId GetEntityId() const;
        const AZ::Aabb& GetWorldBounds() const;
    };

    TerrainExistenceStampValidation PrepareTerrainExistenceStamp(
        const HeightmapStampRegistrationData& registration,
        bool hasNonUniformScale,
        PreparedTerrainExistenceStamp& result,
        HeightmapStampValidation* placementValidation = nullptr);
    const char* GetTerrainExistenceStampValidationMessage(TerrainExistenceStampValidation validation);

    //! Returns true when this stamp explicitly authors existence at the position.
    bool SampleTerrainExistenceStamp(const AZ::Vector3& position, const PreparedTerrainExistenceStamp& stamp, bool& terrainExists);
    bool PrepareTerrainMeshHeightGap(
        const PreparedTerrainMeshHeightStamp& preparedHeight, float worldHeightfieldGridSpacing, PreparedTerrainMeshHeightGap& result);
    bool PrepareTerrainMeshHeightGap(
        const PreparedTerrainMeshHeightStamp& preparedHeight,
        float worldHeightfieldGridSpacing,
        const AZ::Aabb& terrainRegionBounds,
        PreparedTerrainMeshHeightGap& result);
    bool SampleTerrainMeshHeightGap(
        const AZ::Vector3& position, const PreparedTerrainMeshHeightGap& gap, TerrainMeshHeightGapConsumer consumer);
    bool IsTerrainMeshHeightGapCollisionCell(
        const AZ::Vector2& worldCellMinimum,
        const AZ::Vector2& gridSpacing,
        const PreparedTerrainMeshHeightGap& gap);

    bool ComposeTerrainExists(const AZ::Vector3& position, bool baseExists, AZStd::span<const PreparedTerrainExistenceStamp> stamps);
    bool ComposeTerrainExists(
        const AZ::Vector3& surfacePoint, bool baseExists, AZStd::span<const PreparedTerrainExistenceContributor> contributors,
        const AZStd::span<const PreparedTerrainMeshHeightGap>* admitted = nullptr);

    //! Composes authored existence masks that affect render geometry, deliberately
    //! excluding mesh cutouts. Mesh cutouts are evaluated at framebuffer resolution
    //! by the terrain shaders instead of changing mesh topology.
    bool ComposeTerrainRenderGeometryExists(
        const AZ::Vector3& surfacePoint, bool baseExists, AZStd::span<const PreparedTerrainExistenceContributor> contributors);
    bool ComposeTerrainRenderGeometryExists(const AZ::Vector3& position, bool baseExists,
        AZStd::span<const PreparedTerrainExistenceContributor* const> contributors);
} // namespace TerrainCompositor
