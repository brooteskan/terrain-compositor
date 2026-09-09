#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Vector2.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/span.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>
#include <TerrainCompositor/TerrainModelGeometry.h>

namespace TerrainCompositor
{
    // This bounds the compact, welded height field rather than the larger
    // render-mesh streams.
    inline constexpr size_t TerrainMeshHeightMaximumGridSamples = 3'000'000;
    inline constexpr size_t TerrainMeshHeightMaximumDiagnosticDetails = 8;
    inline constexpr AZ::u32 TerrainMeshHeightInvalidDiagnosticIndex = ~AZ::u32{ 0 };
    inline constexpr AZ::u32 TerrainMeshHeightGapTileSize = 8;

    enum class TerrainMeshHeightDataStatus : AZ::u8
    {
        Unassigned,
        Loading,
        Missing,
        Error,
        Unsupported,
        InvalidModel,
        InvalidGeometry,
        Ready
    };

    enum class TerrainMeshHeightValidation : AZ::u8
    {
        Valid,
        Empty,
        IndexCount,
        IndexOutOfRange,
        NonFinitePosition,
        GridDimensions,
        InconsistentGridSpacing,
        MissingGridPoint,
        ConflictingHeight,
        DegenerateTriangle,
        VerticalTriangle,
        NonAdjacentTriangle,
        OverlappingTriangles,
        HalfCell,
        UnexpectedCellTopology,
        ResourceLimit
    };

    struct TerrainMeshHeightDiagnosticDetail
    {
        TerrainMeshHeightValidation m_validation = TerrainMeshHeightValidation::Valid;
        //! Grid-point coordinates for vertex errors, and cell coordinates for
        //! topology errors.
        AZ::u32 m_gridX = TerrainMeshHeightInvalidDiagnosticIndex;
        AZ::u32 m_gridY = TerrainMeshHeightInvalidDiagnosticIndex;
        AZ::u32 m_triangleIndex = TerrainMeshHeightInvalidDiagnosticIndex;
        AZ::u32 m_relatedTriangleIndex = TerrainMeshHeightInvalidDiagnosticIndex;
    };

    struct TerrainMeshHeightBuildDiagnostics
    {
        AZ::u64 m_totalOffenseCount = 0;
        //! Grid context is retained for editor diagnostics even when validation
        //! rejects the mesh. It is never sampled or published as terrain data.
        AZ::Aabb m_localBounds = AZ::Aabb::CreateNull();
        AZ::Vector2 m_localOrigin = AZ::Vector2::CreateZero();
        AZ::Vector2 m_gridSpacing = AZ::Vector2::CreateZero();
        AZ::u32 m_gridWidth = 0;
        AZ::u32 m_gridHeight = 0;
        AZStd::vector<TerrainMeshHeightDiagnosticDetail> m_details;
    };

    //! Deterministic half-open cell ownership. Interior boundaries choose the
    //! positive-axis cell; exact maximum-domain edges are clamped to the final
    //! cell.
    struct TerrainMeshHeightCellAddress
    {
        AZ::u32 m_x = 0;
        AZ::u32 m_y = 0;
        size_t m_index = 0;
        double m_fractionX = 0.0;
        double m_fractionY = 0.0;
    };

    enum class TerrainMeshHeightGapTileOccupancy : AZ::u8
    {
        Covered,
        Mixed,
        Uncovered
    };

    struct TerrainMeshHeightData
    {
        AZ::Data::AssetId m_assetId;
        AZ::u64 m_revision = 0;
        AZ::Aabb m_localBounds = AZ::Aabb::CreateNull();
        AZ::Vector2 m_localOrigin = AZ::Vector2::CreateZero();
        AZ::Vector2 m_gridSpacing = AZ::Vector2::CreateZero();
        AZ::u32 m_width = 0;
        AZ::u32 m_height = 0;
        AZStd::vector<float> m_localHeights;
        //! One bit per grid cell. Set bits are intentional complete-cell gaps inside
        //! the proven Cartesian domain.
        AZStd::vector<AZ::u32> m_uncoveredCellBits;
        //! One bit per grid cell, used only for covered cells: 0 is
        //! bottom-left/top-right, 1 the opposite diagonal.
        AZStd::vector<AZ::u32> m_cellDiagonalBits;
        //! Coarse row-major classification of fixed 8 by 8 cell tiles. This is
        //! derived from m_uncoveredCellBits and lets CPU/GPU consumers reject
        //! covered regions without scanning individual mask words.
        AZ::u32 m_gapTileWidth = 0;
        AZ::u32 m_gapTileHeight = 0;
        AZStd::vector<TerrainMeshHeightGapTileOccupancy> m_gapTileOccupancy;
        //! Tight XY bounds of all uncovered cells in local mesh space. Z is zero.
        AZ::Aabb m_localUncoveredBounds = AZ::Aabb::CreateNull();
        AZ::u32 m_coveredCellCount = 0;
        AZ::u32 m_uncoveredCellCount = 0;
    };
    using TerrainMeshHeightDataPtr = AZStd::shared_ptr<const TerrainMeshHeightData>;

    struct TerrainMeshHeightDataSnapshot
    {
        TerrainMeshHeightDataStatus m_status = TerrainMeshHeightDataStatus::Unassigned;
        TerrainModelGeometryValidation m_modelValidation = TerrainModelGeometryValidation::Valid;
        TerrainMeshHeightValidation m_validation = TerrainMeshHeightValidation::Valid;
        AZ::u64 m_revision = 0;
        TerrainMeshHeightDataPtr m_data;
        AZ::Data::AssetId m_assetId;
        TerrainMeshHeightBuildDiagnostics m_diagnostics;
    };

    TerrainMeshHeightValidation BuildTerrainMeshHeightData(
        AZStd::span<const AZ::Vector3> positions,
        AZStd::span<const AZ::u32> indices,
        TerrainMeshHeightData& result,
        TerrainMeshHeightBuildDiagnostics* diagnostics = nullptr);
    size_t GetTerrainMeshHeightCellCount(const TerrainMeshHeightData& data);
    size_t GetTerrainMeshHeightCellWordCount(const TerrainMeshHeightData& data);
    //! Selects a deterministic two-dimensional sampling stride whose displayed
    //! cell count never exceeds the supplied editor budget.
    size_t CalculateTerrainMeshHeightDiagnosticStride(size_t cellWidth, size_t cellHeight, size_t maximumDisplayedCells);
    bool ResolveTerrainMeshHeightCell(
        const TerrainMeshHeightData& data, double localX, double localY, TerrainMeshHeightCellAddress& address);
    bool IsTerrainMeshHeightCellUncovered(const TerrainMeshHeightData& data, size_t cellIndex);
    TerrainMeshHeightGapTileOccupancy GetTerrainMeshHeightGapTileOccupancy(const TerrainMeshHeightData& data, AZ::u32 cellX, AZ::u32 cellY);
    bool SampleTerrainMeshLocalHeight(const TerrainMeshHeightData& data, double localX, double localY, double& height);
    const char* GetTerrainMeshHeightValidationMessage(TerrainMeshHeightValidation validation);
} // namespace TerrainCompositor
