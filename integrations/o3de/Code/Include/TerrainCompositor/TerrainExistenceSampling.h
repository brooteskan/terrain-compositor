#pragma once

#include <AzCore/std/containers/span.h>
#include <AzCore/std/containers/vector.h>
#include <TerrainCompositor/HeightmapStampSampling.h>
#include <TerrainCompositor/TerrainExistenceConfig.h>
#include <TerrainCompositor/TerrainMeshCutoutSampling.h>
#include <memory>

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
    bool SameTerrainMeshHeightGap(const PreparedTerrainMeshHeightGap& left, const PreparedTerrainMeshHeightGap& right);
    bool IsTerrainMeshHeightGapAdmitted(
        const PreparedTerrainMeshHeightGap& gap, AZStd::span<const PreparedTerrainMeshHeightGap> admitted);

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
} // namespace TerrainCompositor
