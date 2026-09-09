#include <AzCore/IO/ByteContainerStream.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/Utils.h>
#include <AzTest/AzTest.h>
#include <TerrainCompositor/HeightmapStampSampling.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>
#include <TerrainCompositor/TerrainMeshHeightData.h>
#include <TerrainCompositor/TerrainMeshHeightDataCache.h>
#include <TerrainCompositor/TerrainMeshHeightStampSampling.h>

#include <AzCore/std/algorithm.h>
#include <limits>

namespace TerrainCompositor
{
    namespace
    {
        const AZStd::vector<AZ::Vector3> GridPositions{
            { 10.0f, -5.0f, 0.0f }, { 12.0f, -5.0f, 0.0f }, { 10.0f, -2.0f, 0.0f }, { 12.0f, -2.0f, 1.0f }
        };
        const AZStd::vector<AZ::u32> DiagonalBottomLeftTopRight{ 0, 1, 3, 0, 3, 2 };

        struct GridMesh
        {
            AZStd::vector<AZ::Vector3> m_positions;
            AZStd::vector<AZ::u32> m_indices;
        };

        struct TerrainMeshHeightStampConfigV2
        {
            AZ_TYPE_INFO(TerrainMeshHeightStampConfigV2, "{25D76510-C413-4EC4-99D6-6A1960A14441}");

            float m_strength = 0.375f;
            bool m_showSourceMeshInEditor = true;
        };

        bool HasCell(const AZStd::vector<AZStd::pair<AZ::u32, AZ::u32>>& cells, AZ::u32 x, AZ::u32 y)
        {
            return AZStd::find(cells.begin(), cells.end(), AZStd::pair<AZ::u32, AZ::u32>{ x, y }) != cells.end();
        }

        GridMesh MakeGrid(
            AZ::u32 width,
            AZ::u32 height,
            const AZStd::vector<AZStd::pair<AZ::u32, AZ::u32>>& gaps = {},
            const AZStd::vector<AZStd::pair<AZ::u32, AZ::u32>>& oppositeDiagonals = {})
        {
            GridMesh mesh;
            mesh.m_positions.reserve(size_t(width) * height);
            for (AZ::u32 y = 0; y < height; ++y)
            {
                for (AZ::u32 x = 0; x < width; ++x)
                {
                    mesh.m_positions.emplace_back(float(x), float(y), float(x * y));
                }
            }
            for (AZ::u32 y = 0; y + 1 < height; ++y)
            {
                for (AZ::u32 x = 0; x + 1 < width; ++x)
                {
                    if (HasCell(gaps, x, y))
                    {
                        continue;
                    }
                    const AZ::u32 bottomLeft = y * width + x;
                    const AZ::u32 bottomRight = bottomLeft + 1;
                    const AZ::u32 topLeft = bottomLeft + width;
                    const AZ::u32 topRight = topLeft + 1;
                    if (HasCell(oppositeDiagonals, x, y))
                    {
                        mesh.m_indices.insert(mesh.m_indices.end(), { bottomLeft, bottomRight, topLeft, bottomRight, topRight, topLeft });
                    }
                    else
                    {
                        mesh.m_indices.insert(mesh.m_indices.end(), { bottomLeft, bottomRight, topRight, bottomLeft, topRight, topLeft });
                    }
                }
            }
            return mesh;
        }

        TerrainMeshHeightDataPtr MakeMeshHeightGrid()
        {
            auto data = AZStd::make_shared<TerrainMeshHeightData>();
            EXPECT_EQ(BuildTerrainMeshHeightData(GridPositions, DiagonalBottomLeftTopRight, *data), TerrainMeshHeightValidation::Valid);
            return data;
        }

        PreparedTerrainMeshHeightStamp MakePreparedMeshHeight(const AZ::Transform& transform = AZ::Transform::CreateIdentity())
        {
            TerrainMeshHeightStampRegistrationData registration;
            registration.m_stampEntityId = AZ::EntityId(1001);
            registration.m_configuration.m_orderingId = AZ::Uuid::CreateRandom();
            registration.m_configuration.m_featherWidth = 0.0f;
            registration.m_configuration.m_relativeEdgeBlend = false;
            registration.m_mesh.m_status = TerrainMeshHeightDataStatus::Ready;
            registration.m_mesh.m_data = MakeMeshHeightGrid();
            registration.m_worldTransform = transform;
            PreparedTerrainMeshHeightStamp prepared;
            EXPECT_EQ(PrepareTerrainMeshHeightStamp(registration, false, prepared), TerrainMeshHeightStampPlacementValidation::Valid);
            return prepared;
        }

        PreparedTerrainMeshHeightStamp MakePreparedMeshHeight(
            TerrainMeshHeightDataPtr data, TerrainMeshHeightUncoveredAreaPolicy policy, float edgeInset = 0.0f)
        {
            TerrainMeshHeightStampRegistrationData registration;
            registration.m_stampEntityId = AZ::EntityId(1001);
            registration.m_configuration.m_orderingId = AZ::Uuid::CreateRandom();
            registration.m_configuration.m_relativeEdgeBlend = false;
            registration.m_configuration.m_uncoveredAreaPolicy = policy;
            registration.m_configuration.m_edgeInset = edgeInset;
            registration.m_mesh.m_status = TerrainMeshHeightDataStatus::Ready;
            registration.m_mesh.m_data = AZStd::move(data);
            registration.m_worldTransform = AZ::Transform::CreateIdentity();
            PreparedTerrainMeshHeightStamp prepared;
            EXPECT_EQ(PrepareTerrainMeshHeightStamp(registration, false, prepared), TerrainMeshHeightStampPlacementValidation::Valid);
            return prepared;
        }
    } // namespace

    TEST(TerrainMeshHeightConfigurationTests, DefaultsToPreservingLowerTerrain)
    {
        const TerrainMeshHeightStampConfig configuration;
        EXPECT_EQ(configuration.m_uncoveredAreaPolicy, TerrainMeshHeightUncoveredAreaPolicy::PreserveLowerTerrain);
        EXPECT_TRUE(configuration.m_affectTerrainRendering);
        EXPECT_TRUE(configuration.m_affectTerrainCollisionQueries);
    }

    TEST(TerrainMeshHeightPreparationTests, RejectsOlderTicketsWithinTheSameLifecycleAndEveryPriorLifecycle)
    {
        EXPECT_TRUE(IsTerrainMeshHeightPreparationCurrent(4, 12, 4, 12));
        EXPECT_FALSE(IsTerrainMeshHeightPreparationCurrent(4, 11, 4, 12));
        EXPECT_FALSE(IsTerrainMeshHeightPreparationCurrent(3, 12, 4, 12));
        EXPECT_FALSE(IsTerrainMeshHeightPreparationCurrent(3, 11, 4, 12));
    }

    TEST(TerrainMeshHeightConfigurationTests, LegacyVersionsLoadWithPreservePolicyAndRemoveTheOldVisibilityField)
    {
        AZ::SerializeContext currentContext;
        TerrainMeshHeightStampConfig::Reflect(&currentContext);
        const auto* classData = currentContext.FindClassData(azrtti_typeid<TerrainMeshHeightStampConfig>());
        ASSERT_NE(classData, nullptr);
        EXPECT_EQ(classData->m_version, 3);

        for (const unsigned int version : { 1u, 2u })
        {
            AZ::SerializeContext legacyContext;
            auto classBuilder = legacyContext.Class<TerrainMeshHeightStampConfigV2>();
            classBuilder.Version(version)->Field("Strength", &TerrainMeshHeightStampConfigV2::m_strength);
            if (version < 2)
            {
                classBuilder.Field("ShowSourceMeshInEditor", &TerrainMeshHeightStampConfigV2::m_showSourceMeshInEditor);
            }
            TerrainMeshHeightStampConfigV2 legacy;
            AZStd::vector<char> buffer;
            AZ::IO::ByteContainerStream<AZStd::vector<char>> stream(&buffer);
            ASSERT_TRUE(AZ::Utils::SaveObjectToStream(stream, AZ::ObjectStream::ST_XML, &legacy, &legacyContext));
            AZStd::unique_ptr<TerrainMeshHeightStampConfig> loaded(
                AZ::Utils::LoadObjectFromBuffer<TerrainMeshHeightStampConfig>(buffer.data(), buffer.size(), &currentContext));
            ASSERT_NE(loaded, nullptr);
            EXPECT_FLOAT_EQ(loaded->m_strength, 0.375f);
            EXPECT_EQ(loaded->m_uncoveredAreaPolicy, TerrainMeshHeightUncoveredAreaPolicy::PreserveLowerTerrain);
            EXPECT_TRUE(loaded->m_affectTerrainRendering);
            EXPECT_TRUE(loaded->m_affectTerrainCollisionQueries);
        }

        currentContext.EnableRemoveReflection();
        TerrainMeshHeightStampConfig::Reflect(&currentContext);
        currentContext.DisableRemoveReflection();
    }

    TEST(TerrainMeshHeightDataTests, PreservesNonCenteredBoundsAndImportedTrianglePlane)
    {
        const auto data = MakeMeshHeightGrid();
        ASSERT_TRUE(data);
        EXPECT_EQ(data->m_width, 2);
        EXPECT_EQ(data->m_height, 2);
        EXPECT_EQ(data->m_localOrigin, AZ::Vector2(10.0f, -5.0f));
        EXPECT_EQ(data->m_gridSpacing, AZ::Vector2(2.0f, 3.0f));
        EXPECT_EQ(data->m_localBounds.GetMin(), AZ::Vector3(10.0f, -5.0f, 0.0f));
        EXPECT_EQ(data->m_localBounds.GetMax(), AZ::Vector3(12.0f, -2.0f, 1.0f));
        double height = 0.0;
        ASSERT_TRUE(SampleTerrainMeshLocalHeight(*data, 11.5, -4.25, height));
        EXPECT_DOUBLE_EQ(height,
                         0.25); // Bilinear would be 0.1875 and would not match the FBX triangles.
        ASSERT_TRUE(SampleTerrainMeshLocalHeight(*data, 10.5, -2.75, height));
        EXPECT_DOUBLE_EQ(height, 0.25);
        EXPECT_FALSE(SampleTerrainMeshLocalHeight(*data, 9.99, -4.0, height));
    }

    TEST(TerrainMeshHeightDataTests, PreservesTheOppositeImportedDiagonal)
    {
        const AZStd::vector<AZ::u32> oppositeDiagonal{ 0, 1, 2, 1, 3, 2 };
        TerrainMeshHeightData data;
        ASSERT_EQ(BuildTerrainMeshHeightData(GridPositions, oppositeDiagonal, data), TerrainMeshHeightValidation::Valid);
        double height = 0.0;
        ASSERT_TRUE(SampleTerrainMeshLocalHeight(data, 11.5, -3.5, height));
        EXPECT_DOUBLE_EQ(height, 0.25); // Bilinear would be 0.375 and would not match
                                        // the imported triangles.
    }

    TEST(TerrainMeshHeightDataTests, AcceptsInteriorAdjacentDisjointAndBoundaryGaps)
    {
        const AZStd::vector<AZStd::pair<AZ::u32, AZ::u32>> gaps{ { 1, 1 }, { 2, 1 }, { 0, 0 }, { 3, 2 } };
        const auto mesh = MakeGrid(5, 4, gaps, { { 1, 0 }, { 2, 2 } });
        TerrainMeshHeightData data;
        ASSERT_EQ(BuildTerrainMeshHeightData(mesh.m_positions, mesh.m_indices, data), TerrainMeshHeightValidation::Valid);

        EXPECT_EQ(data.m_width, 5);
        EXPECT_EQ(data.m_height, 4);
        EXPECT_EQ(data.m_coveredCellCount, 8);
        EXPECT_EQ(data.m_uncoveredCellCount, 4);
        EXPECT_EQ(data.m_uncoveredCellBits.size(), 1);
        EXPECT_EQ(data.m_cellDiagonalBits.size(), 1);
        for (const auto& [x, y] : gaps)
        {
            EXPECT_TRUE(IsTerrainMeshHeightCellUncovered(data, size_t(y) * 4 + x));
            double height = 123.0;
            EXPECT_FALSE(SampleTerrainMeshLocalHeight(data, x + 0.5, y + 0.5, height));
            EXPECT_DOUBLE_EQ(height, 0.0);
        }

        double height = 0.0;
        ASSERT_TRUE(SampleTerrainMeshLocalHeight(data, 1.75, 0.5, height));
        EXPECT_DOUBLE_EQ(height, 0.75); // Opposite diagonal for this non-planar cell.
        ASSERT_TRUE(SampleTerrainMeshLocalHeight(data, 0.5, 1.5, height));
        EXPECT_DOUBLE_EQ(height, 1.0);
    }

    TEST(TerrainMeshHeightDataTests, PacksCoverageAcrossWordBoundaries)
    {
        const AZStd::vector<AZStd::pair<AZ::u32, AZ::u32>> gaps{ { 1, 3 }, { 2, 3 }, { 5, 3 } };
        const auto mesh = MakeGrid(10, 5, gaps);
        TerrainMeshHeightData data;
        ASSERT_EQ(BuildTerrainMeshHeightData(mesh.m_positions, mesh.m_indices, data), TerrainMeshHeightValidation::Valid);

        ASSERT_EQ(GetTerrainMeshHeightCellCount(data), 36);
        ASSERT_EQ(data.m_uncoveredCellBits.size(), 2);
        EXPECT_TRUE(IsTerrainMeshHeightCellUncovered(data, 28));
        EXPECT_TRUE(IsTerrainMeshHeightCellUncovered(data, 29));
        EXPECT_TRUE(IsTerrainMeshHeightCellUncovered(data, 32));
        EXPECT_FALSE(IsTerrainMeshHeightCellUncovered(data, 31));
        EXPECT_FALSE(IsTerrainMeshHeightCellUncovered(data, 36));
    }

    TEST(TerrainMeshHeightDataTests, BuildsDeterministicGapTileSummariesAndTightBounds)
    {
        AZStd::vector<AZStd::pair<AZ::u32, AZ::u32>> gaps;
        for (AZ::u32 y = 0; y < TerrainMeshHeightGapTileSize; ++y)
        {
            for (AZ::u32 x = 0; x < TerrainMeshHeightGapTileSize; ++x)
            {
                gaps.emplace_back(x, y);
            }
        }
        gaps.emplace_back(TerrainMeshHeightGapTileSize, 0);
        const auto mesh = MakeGrid(18, 10, gaps);
        TerrainMeshHeightData data;
        ASSERT_EQ(BuildTerrainMeshHeightData(mesh.m_positions, mesh.m_indices, data), TerrainMeshHeightValidation::Valid);
        ASSERT_EQ(data.m_gapTileWidth, 3);
        ASSERT_EQ(data.m_gapTileHeight, 2);
        ASSERT_EQ(data.m_gapTileOccupancy.size(), 6);
        EXPECT_EQ(GetTerrainMeshHeightGapTileOccupancy(data, 0, 0), TerrainMeshHeightGapTileOccupancy::Uncovered);
        EXPECT_EQ(GetTerrainMeshHeightGapTileOccupancy(data, TerrainMeshHeightGapTileSize, 0), TerrainMeshHeightGapTileOccupancy::Mixed);
        EXPECT_EQ(GetTerrainMeshHeightGapTileOccupancy(data, 16, 0), TerrainMeshHeightGapTileOccupancy::Covered);
        EXPECT_EQ(GetTerrainMeshHeightGapTileOccupancy(data, 0, 8), TerrainMeshHeightGapTileOccupancy::Covered);
        ASSERT_TRUE(data.m_localUncoveredBounds.IsValid());
        EXPECT_TRUE(data.m_localUncoveredBounds.GetMin().IsClose(AZ::Vector3(0.0f, 0.0f, 0.0f)));
        EXPECT_TRUE(data.m_localUncoveredBounds.GetMax().IsClose(AZ::Vector3(9.0f, 8.0f, 0.0f)));
    }

    TEST(TerrainMeshHeightDataTests, UsesDeterministicCellBoundaryOwnership)
    {
        const auto mesh = MakeGrid(3, 3, { { 1, 0 } });
        TerrainMeshHeightData data;
        ASSERT_EQ(BuildTerrainMeshHeightData(mesh.m_positions, mesh.m_indices, data), TerrainMeshHeightValidation::Valid);

        TerrainMeshHeightCellAddress address;
        ASSERT_TRUE(ResolveTerrainMeshHeightCell(data, 0.0, 0.0, address));
        EXPECT_EQ(address.m_x, 0);
        EXPECT_EQ(address.m_y, 0);
        ASSERT_TRUE(ResolveTerrainMeshHeightCell(data, 1.0, 0.5, address));
        EXPECT_EQ(address.m_x,
                  1); // Interior boundaries belong to the positive-axis cell.
        EXPECT_EQ(address.m_y, 0);
        ASSERT_TRUE(ResolveTerrainMeshHeightCell(data, 0.5, 1.0, address));
        EXPECT_EQ(address.m_x, 0);
        EXPECT_EQ(address.m_y, 1);
        ASSERT_TRUE(ResolveTerrainMeshHeightCell(data, 1.0, 1.0, address));
        EXPECT_EQ(address.m_x, 1);
        EXPECT_EQ(address.m_y, 1);
        ASSERT_TRUE(ResolveTerrainMeshHeightCell(data, 2.0, 2.0, address));
        EXPECT_EQ(address.m_x, 1); // Exact maximum edges clamp to the final cell.
        EXPECT_EQ(address.m_y, 1);
        EXPECT_FALSE(ResolveTerrainMeshHeightCell(data, -0.0001, 1.0, address));
        EXPECT_FALSE(ResolveTerrainMeshHeightCell(data, 2.0001, 1.0, address));

        double height = 0.0;
        EXPECT_TRUE(SampleTerrainMeshLocalHeight(data, 0.9999, 0.5, height));
        EXPECT_FALSE(SampleTerrainMeshLocalHeight(data, 1.0, 0.5, height));
        EXPECT_FALSE(SampleTerrainMeshLocalHeight(data, 2.0, 0.5, height));
    }

    TEST(TerrainMeshHeightDataTests, WeldsRenderSplitVerticesWithoutChangingHeights)
    {
        const AZStd::vector<AZ::Vector3> split{ GridPositions[0], GridPositions[1], GridPositions[3],
                                                GridPositions[0], GridPositions[3], GridPositions[2] };
        const AZStd::vector<AZ::u32> indices{ 0, 1, 2, 3, 4, 5 };
        TerrainMeshHeightData data;
        ASSERT_EQ(BuildTerrainMeshHeightData(split, indices, data), TerrainMeshHeightValidation::Valid);
        EXPECT_EQ(data.m_localHeights, (AZStd::vector<float>{ 0.0f, 0.0f, 0.0f, 1.0f }));
    }

    TEST(TerrainMeshHeightDataTests, ResourceBudgetSupportsA1024SquareGridWithSplitRenderVertices)
    {
        constexpr size_t Dimension = 1024;
        constexpr size_t GridSamples = Dimension * Dimension;
        constexpr size_t TriangleIndices = 6 * (Dimension - 1) * (Dimension - 1);

        EXPECT_GE(TerrainMeshHeightMaximumGridSamples, GridSamples);
        EXPECT_GE(TerrainModelGeometryMaximumIndices, TriangleIndices);
        // In the worst common FBX layout, every triangle index can address a split
        // render vertex.
        EXPECT_GE(TerrainModelGeometryMaximumVertices, TriangleIndices);
    }

    TEST(TerrainMeshHeightDataTests, RejectsMissingConflictingDegenerateAndOverlappingGeometry)
    {
        TerrainMeshHeightData data;
        auto missing = GridPositions;
        missing.push_back({ 14.0f, -5.0f, 0.0f });
        EXPECT_EQ(BuildTerrainMeshHeightData(missing, DiagonalBottomLeftTopRight, data), TerrainMeshHeightValidation::MissingGridPoint);

        auto conflicting = GridPositions;
        conflicting.push_back({ 10.0f, -5.0f, 2.0f });
        EXPECT_EQ(
            BuildTerrainMeshHeightData(conflicting, DiagonalBottomLeftTopRight, data), TerrainMeshHeightValidation::ConflictingHeight);

        const AZStd::vector<AZ::u32> degenerate{ 0, 1, 1, 0, 3, 2 };
        EXPECT_EQ(BuildTerrainMeshHeightData(GridPositions, degenerate, data), TerrainMeshHeightValidation::DegenerateTriangle);

        const AZStd::vector<AZ::u32> overlapping{ 0, 1, 3, 0, 1, 3, 0, 3, 2 };
        EXPECT_EQ(BuildTerrainMeshHeightData(GridPositions, overlapping, data), TerrainMeshHeightValidation::OverlappingTriangles);
    }

    TEST(TerrainMeshHeightDataTests, RejectsHalfCellsAndRetainsBoundedDiagnosticsWithoutPartialData)
    {
        TerrainMeshHeightData data;
        data.m_width = 99;
        data.m_localHeights = { 1.0f };
        TerrainMeshHeightBuildDiagnostics diagnostics;
        const AZStd::vector<AZ::u32> halfCell{ 0, 1, 3 };
        EXPECT_EQ(BuildTerrainMeshHeightData(GridPositions, halfCell, data, &diagnostics), TerrainMeshHeightValidation::HalfCell);
        EXPECT_EQ(data.m_width, 0);
        EXPECT_TRUE(data.m_localHeights.empty());
        ASSERT_EQ(diagnostics.m_totalOffenseCount, 1);
        ASSERT_EQ(diagnostics.m_details.size(), 1);
        EXPECT_EQ(diagnostics.m_details[0].m_validation, TerrainMeshHeightValidation::HalfCell);
        EXPECT_EQ(diagnostics.m_details[0].m_gridX, 0);
        EXPECT_EQ(diagnostics.m_details[0].m_gridY, 0);
        EXPECT_EQ(diagnostics.m_details[0].m_triangleIndex, 0);
        EXPECT_EQ(diagnostics.m_gridWidth, 2);
        EXPECT_EQ(diagnostics.m_gridHeight, 2);
        EXPECT_EQ(diagnostics.m_localOrigin, AZ::Vector2(10.0f, -5.0f));
        EXPECT_EQ(diagnostics.m_gridSpacing, AZ::Vector2(2.0f, 3.0f));
        EXPECT_EQ(diagnostics.m_localBounds, AZ::Aabb::CreateFromMinMaxValues(10.0f, -5.0f, 0.0f, 12.0f, -2.0f, 1.0f));

        AZStd::vector<AZ::Vector3> sparse;
        for (AZ::u32 coordinate = 0; coordinate < 12; ++coordinate)
        {
            sparse.emplace_back(float(coordinate), 0.0f, 0.0f);
            if (coordinate != 0)
            {
                sparse.emplace_back(0.0f, float(coordinate), 0.0f);
            }
        }
        const AZStd::vector<AZ::u32> oneTriangle{ 0, 1, 12 };
        EXPECT_EQ(BuildTerrainMeshHeightData(sparse, oneTriangle, data, &diagnostics), TerrainMeshHeightValidation::MissingGridPoint);
        EXPECT_GT(diagnostics.m_totalOffenseCount, TerrainMeshHeightMaximumDiagnosticDetails);
        EXPECT_EQ(diagnostics.m_details.size(), TerrainMeshHeightMaximumDiagnosticDetails);
        EXPECT_EQ(diagnostics.m_gridWidth, 12);
        EXPECT_EQ(diagnostics.m_gridHeight, 12);
    }

    TEST(TerrainMeshHeightDataTests, DiagnosticDrawingStrideIsBoundedAndKeepsModestGridsExact)
    {
        EXPECT_EQ(CalculateTerrainMeshHeightDiagnosticStride(8, 8, 4096), 1);
        const size_t stride = CalculateTerrainMeshHeightDiagnosticStride(2999, 999, 4096);
        ASSERT_GT(stride, 1);
        const size_t displayed = ((2999 + stride - 1) / stride) * ((999 + stride - 1) / stride);
        EXPECT_LE(displayed, 4096);
        EXPECT_EQ(CalculateTerrainMeshHeightDiagnosticStride(8, 8, 0), 0);
    }

    TEST(TerrainMeshHeightDataTests, RejectsMalformedSourceGeometryWithSpecificReasons)
    {
        TerrainMeshHeightData data;
        auto mesh = MakeGrid(3, 2);

        const AZStd::vector<AZ::u32> invalidIndex{ 0, 1, 99 };
        EXPECT_EQ(BuildTerrainMeshHeightData(mesh.m_positions, invalidIndex, data), TerrainMeshHeightValidation::IndexOutOfRange);

        const AZStd::vector<AZ::u32> nonAdjacent{ 0, 2, 5 };
        EXPECT_EQ(BuildTerrainMeshHeightData(mesh.m_positions, nonAdjacent, data), TerrainMeshHeightValidation::NonAdjacentTriangle);

        mesh.m_positions[2].SetZ(1.0f);
        const AZStd::vector<AZ::u32> vertical{ 0, 1, 2 };
        EXPECT_EQ(BuildTerrainMeshHeightData(mesh.m_positions, vertical, data), TerrainMeshHeightValidation::VerticalTriangle);

        auto nonFinite = GridPositions;
        nonFinite[0].SetZ(std::numeric_limits<float>::quiet_NaN());
        EXPECT_EQ(BuildTerrainMeshHeightData(nonFinite, DiagonalBottomLeftTopRight, data), TerrainMeshHeightValidation::NonFinitePosition);

        auto irregular = MakeGrid(3, 2);
        irregular.m_positions[2].SetX(3.0f);
        irregular.m_positions[5].SetX(3.0f);
        EXPECT_EQ(
            BuildTerrainMeshHeightData(irregular.m_positions, irregular.m_indices, data),
            TerrainMeshHeightValidation::InconsistentGridSpacing);

        const AZStd::vector<AZ::u32> nonComplementary{ 0, 1, 3, 0, 1, 2 };
        EXPECT_EQ(BuildTerrainMeshHeightData(GridPositions, nonComplementary, data), TerrainMeshHeightValidation::OverlappingTriangles);
    }

    TEST(TerrainMeshHeightSamplingTests, UncoveredCellsNeverContributeHeightUnderEitherPolicy)
    {
        const auto mesh = MakeGrid(4, 4, { { 1, 1 } });
        auto data = AZStd::make_shared<TerrainMeshHeightData>();
        ASSERT_EQ(BuildTerrainMeshHeightData(mesh.m_positions, mesh.m_indices, *data), TerrainMeshHeightValidation::Valid);

        for (const auto policy :
             { TerrainMeshHeightUncoveredAreaPolicy::PreserveLowerTerrain, TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain })
        {
            const auto prepared = MakePreparedMeshHeight(data, policy, 0.25f);
            EXPECT_EQ(prepared.m_uncoveredAreaPolicy, policy);
            EXPECT_TRUE(IsTerrainMeshHeightCellUncovered(*prepared.m_data, 4));
            TerrainMeshHeightContribution contribution;
            EXPECT_FALSE(SampleTerrainMeshHeightStamp(AZ::Vector3(1.5f, 1.5f, 0.0f), prepared, contribution));
            EXPECT_FALSE(SampleTerrainMeshHeightStamp(AZ::Vector3(0.1f, 1.5f, 0.0f), prepared, contribution));
            EXPECT_TRUE(SampleTerrainMeshHeightStamp(AZ::Vector3(0.5f, 1.5f, 0.0f), prepared, contribution));
        }
    }

    TEST(TerrainMeshHeightSamplingTests, GapPreparationIsStrengthAndEdgeInsetIndependentAndRetainsTheMeshRevision)
    {
        const auto mesh = MakeGrid(3, 3, { { 1, 1 } });
        auto data = AZStd::make_shared<TerrainMeshHeightData>();
        ASSERT_EQ(BuildTerrainMeshHeightData(mesh.m_positions, mesh.m_indices, *data), TerrainMeshHeightValidation::Valid);
        data->m_revision = 73;
        auto prepared = MakePreparedMeshHeight(data, TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain, 0.25f);
        prepared.m_strength = 0.0;

        PreparedTerrainMeshHeightGap gap;
        ASSERT_TRUE(PrepareTerrainMeshHeightGap(prepared, 0.5f, gap));
        EXPECT_EQ(gap.m_data, prepared.m_data);
        EXPECT_EQ(gap.m_data->m_revision, 73);
        EXPECT_TRUE(SampleTerrainMeshHeightGap(AZ::Vector3(1.5f, 1.5f, 100.0f), gap, TerrainMeshHeightGapConsumer::Queries));
        EXPECT_FALSE(SampleTerrainMeshHeightGap(AZ::Vector3(0.5f, 1.5f, 100.0f), gap, TerrainMeshHeightGapConsumer::Queries));
        EXPECT_FALSE(SampleTerrainMeshHeightGap(AZ::Vector3(2.2f, 1.5f, 100.0f), gap, TerrainMeshHeightGapConsumer::Queries));
        EXPECT_FALSE(SampleTerrainMeshHeightGap(AZ::Vector3(2.2f, 1.5f, 100.0f), gap, TerrainMeshHeightGapConsumer::Collision));

        auto queryOnly = gap;
        queryOnly.m_affectTerrainRendering = false;
        EXPECT_FALSE(SampleTerrainMeshHeightGap(AZ::Vector3(1.5f, 1.5f, 0.0f), queryOnly, TerrainMeshHeightGapConsumer::Rendering));
        EXPECT_TRUE(SampleTerrainMeshHeightGap(AZ::Vector3(1.5f, 1.5f, 0.0f), queryOnly, TerrainMeshHeightGapConsumer::Queries));
        queryOnly.m_affectTerrainCollisionQueries = false;
        EXPECT_FALSE(SampleTerrainMeshHeightGap(AZ::Vector3(1.5f, 1.5f, 0.0f), queryOnly, TerrainMeshHeightGapConsumer::Queries));

        prepared.m_uncoveredAreaPolicy = TerrainMeshHeightUncoveredAreaPolicy::PreserveLowerTerrain;
        EXPECT_FALSE(PrepareTerrainMeshHeightGap(prepared, 0.5f, gap));
    }

    TEST(TerrainMeshHeightSamplingTests, GapUsesTheSameTranslatedYawedAndUniformlyScaledPlacementAsHeight)
    {
        const auto mesh = MakeGrid(3, 3, { { 1, 1 } });
        auto data = AZStd::make_shared<TerrainMeshHeightData>();
        ASSERT_EQ(BuildTerrainMeshHeightData(mesh.m_positions, mesh.m_indices, *data), TerrainMeshHeightValidation::Valid);
        TerrainMeshHeightStampRegistrationData registration;
        registration.m_stampEntityId = AZ::EntityId(1002);
        registration.m_configuration.m_orderingId = AZ::Uuid::CreateRandom();
        registration.m_configuration.m_uncoveredAreaPolicy = TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain;
        registration.m_mesh.m_status = TerrainMeshHeightDataStatus::Ready;
        registration.m_mesh.m_data = data;
        registration.m_worldTransform = AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateRotationZ(AZ::DegToRad(90.0f)), AZ::Vector3(10.0f, 20.0f, 4.0f));
        registration.m_worldTransform.MultiplyByUniformScale(2.0f);
        PreparedTerrainMeshHeightStamp prepared;
        ASSERT_EQ(PrepareTerrainMeshHeightStamp(registration, false, prepared), TerrainMeshHeightStampPlacementValidation::Valid);
        PreparedTerrainMeshHeightGap gap;
        ASSERT_TRUE(PrepareTerrainMeshHeightGap(prepared, 0.0f, gap));
        EXPECT_EQ(gap.m_data, prepared.m_data);
        EXPECT_TRUE(gap.m_worldBounds.IsValid());
        EXPECT_TRUE(SampleTerrainMeshHeightGap(
            registration.m_worldTransform.TransformPoint(AZ::Vector3(1.5f, 1.5f, 0.0f)), gap, TerrainMeshHeightGapConsumer::Queries));
        EXPECT_FALSE(SampleTerrainMeshHeightGap(
            registration.m_worldTransform.TransformPoint(AZ::Vector3(0.5f, 1.5f, 0.0f)), gap, TerrainMeshHeightGapConsumer::Queries));
    }

    TEST(TerrainMeshHeightSamplingTests, PreparesSubGridCollisionCellsWithoutExpandingLogicalQueries)
    {
        const auto mesh = MakeGrid(3, 3, { { 1, 1 } });
        auto data = AZStd::make_shared<TerrainMeshHeightData>();
        ASSERT_EQ(BuildTerrainMeshHeightData(mesh.m_positions, mesh.m_indices, *data), TerrainMeshHeightValidation::Valid);
        TerrainMeshHeightStampRegistrationData registration;
        registration.m_stampEntityId = AZ::EntityId(1010);
        registration.m_configuration.m_orderingId = AZ::Uuid::CreateRandom();
        registration.m_configuration.m_uncoveredAreaPolicy = TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain;
        registration.m_mesh.m_status = TerrainMeshHeightDataStatus::Ready;
        registration.m_mesh.m_data = data;
        registration.m_worldTransform = AZ::Transform::CreateTranslation(AZ::Vector3(0.25f, 0.25f, 0.0f));
        registration.m_worldTransform.MultiplyByUniformScale(0.5f);
        PreparedTerrainMeshHeightStamp preparedHeight;
        ASSERT_EQ(
            PrepareTerrainMeshHeightStamp(registration, false, preparedHeight),
            TerrainMeshHeightStampPlacementValidation::Valid);

        PreparedTerrainMeshHeightGap gap;
        const AZ::Aabb region = AZ::Aabb::CreateFromMinMaxValues(0.0f, 0.0f, -10.0f, 4.0f, 4.0f, 10.0f);
        ASSERT_TRUE(PrepareTerrainMeshHeightGap(preparedHeight, 2.0f, region, gap));
        ASSERT_TRUE(gap.m_collisionCells);
        ASSERT_EQ(gap.m_collisionCells->m_cells.size(), 1);
        EXPECT_EQ(gap.m_collisionCells->m_cells.front(), (TerrainHeightfieldCellAddress{ 0, 0 }));
        EXPECT_EQ(gap.m_data->m_uncoveredCellCount, 1);
        EXPECT_FALSE(SampleTerrainMeshHeightGap(AZ::Vector3(0.1f, 0.1f, 0.0f), gap, TerrainMeshHeightGapConsumer::Queries));
        EXPECT_TRUE(SampleTerrainMeshHeightGap(AZ::Vector3(0.1f, 0.1f, 0.0f), gap, TerrainMeshHeightGapConsumer::Collision));
        EXPECT_TRUE(IsTerrainMeshHeightGapCollisionCell(AZ::Vector2(0.0f), AZ::Vector2(2.0f), gap));
        EXPECT_FALSE(IsTerrainMeshHeightGapCollisionCell(AZ::Vector2(2.0f, 0.0f), AZ::Vector2(2.0f), gap));

        PreparedTerrainMeshHeightGap invalidGrid;
        EXPECT_FALSE(PrepareTerrainMeshHeightGap(preparedHeight, 0.0f, region, invalidGrid));
    }

    TEST(TerrainMeshHeightSamplingTests, RotatedUnequalGridPreparesEveryIntersectingCollisionCell)
    {
        GridMesh mesh = MakeGrid(3, 3, { { 1, 1 } });
        for (AZ::Vector3& point : mesh.m_positions)
        {
            point.SetX(point.GetX() * 2.0f);
            point.SetY(point.GetY() * 0.5f);
        }
        auto data = AZStd::make_shared<TerrainMeshHeightData>();
        ASSERT_EQ(BuildTerrainMeshHeightData(mesh.m_positions, mesh.m_indices, *data), TerrainMeshHeightValidation::Valid);
        TerrainMeshHeightStampRegistrationData registration;
        registration.m_stampEntityId = AZ::EntityId(1011);
        registration.m_configuration.m_orderingId = AZ::Uuid::CreateRandom();
        registration.m_configuration.m_uncoveredAreaPolicy = TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain;
        registration.m_mesh.m_status = TerrainMeshHeightDataStatus::Ready;
        registration.m_mesh.m_data = data;
        registration.m_worldTransform = AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateRotationZ(AZ::DegToRad(45.0f)), AZ::Vector3(5.0f, 3.0f, 0.0f));
        PreparedTerrainMeshHeightStamp preparedHeight;
        ASSERT_EQ(
            PrepareTerrainMeshHeightStamp(registration, false, preparedHeight),
            TerrainMeshHeightStampPlacementValidation::Valid);
        PreparedTerrainMeshHeightGap gap;
        const AZ::Aabb region = AZ::Aabb::CreateFromMinMaxValues(-20.0f, -20.0f, -10.0f, 20.0f, 20.0f, 10.0f);
        ASSERT_TRUE(PrepareTerrainMeshHeightGap(preparedHeight, 1.0f, region, gap));
        ASSERT_TRUE(gap.m_collisionCells);
        EXPECT_GT(gap.m_collisionCells->m_cells.size(), 1);
        EXPECT_TRUE(gap.m_collisionWorldBounds.IsValid());
        EXPECT_TRUE(gap.m_collisionWorldBounds.Contains(gap.m_worldBounds));

        PreparedTerrainMeshHeightGap finer;
        ASSERT_TRUE(PrepareTerrainMeshHeightGap(preparedHeight, 0.5f, region, finer));
        ASSERT_TRUE(finer.m_collisionCells);
        EXPECT_GT(finer.m_collisionCells->m_cells.size(), gap.m_collisionCells->m_cells.size());
        EXPECT_FLOAT_EQ(finer.m_collisionCells->m_gridSpacing, 0.5f);
        EXPECT_FALSE(IsTerrainMeshHeightGapCollisionCell(
            AZ::Vector2::CreateZero(), AZ::Vector2(0.5f), gap));
    }

    TEST(TerrainMeshHeightSamplingTests, UsesMeshPivotYawAndUniformScale)
    {
        AZ::Transform transform = AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateRotationZ(AZ::DegToRad(90.0f)), AZ::Vector3(50.0f, 20.0f, 7.0f));
        transform.MultiplyByUniformScale(2.0f);
        const auto prepared = MakePreparedMeshHeight(transform);
        TerrainMeshHeightContribution sample;
        const AZ::Vector3 world = transform.TransformPoint(AZ::Vector3(11.5f, -4.25f, 0.0f));
        ASSERT_TRUE(SampleTerrainMeshHeightStamp(world, prepared, sample));
        EXPECT_NEAR(sample.m_targetHeight, 7.5, 1.0e-6);
        EXPECT_NEAR(sample.m_displacement, 0.5, 1.0e-6);
        EXPECT_NEAR(prepared.m_worldBounds.GetMin().GetX(), 54.0f, 1.0e-4f);
        EXPECT_NEAR(prepared.m_worldBounds.GetMin().GetY(), 40.0f, 1.0e-4f);
        EXPECT_NEAR(prepared.m_worldBounds.GetMax().GetX(), 60.0f, 1.0e-4f);
        EXPECT_NEAR(prepared.m_worldBounds.GetMax().GetY(), 44.0f, 1.0e-4f);
    }

    TEST(TerrainMeshHeightSamplingTests, RejectsPitchAndNonUniformScale)
    {
        TerrainMeshHeightStampRegistrationData registration;
        registration.m_stampEntityId = AZ::EntityId(1001);
        registration.m_configuration.m_orderingId = AZ::Uuid::CreateRandom();
        registration.m_mesh.m_status = TerrainMeshHeightDataStatus::Ready;
        registration.m_mesh.m_data = MakeMeshHeightGrid();
        registration.m_worldTransform = AZ::Transform::CreateFromQuaternion(AZ::Quaternion::CreateRotationX(AZ::DegToRad(2.0f)));
        PreparedTerrainMeshHeightStamp prepared;
        EXPECT_EQ(PrepareTerrainMeshHeightStamp(registration, false, prepared), TerrainMeshHeightStampPlacementValidation::Rotation);
        registration.m_worldTransform = AZ::Transform::CreateIdentity();
        EXPECT_EQ(PrepareTerrainMeshHeightStamp(registration, true, prepared), TerrainMeshHeightStampPlacementValidation::NonUniformScale);
    }

    TEST(TerrainMeshHeightSamplingTests, ImageAndMeshUseOneOrderedReplaceStream)
    {
        PreparedHeightContributor image;
        image.m_type = PreparedHeightContributor::Type::Image;
        image.m_image.m_placement.m_worldBounds =
            AZ::Aabb::CreateFromMinMax(AZ::Vector3(9.0f, -6.0f, 0.0f), AZ::Vector3(13.0f, -1.0f, 0.0f));
        image.m_image.m_placement.m_centerX = 11.0;
        image.m_image.m_placement.m_centerY = -3.5;
        image.m_image.m_placement.m_halfWidth = 2.0;
        image.m_image.m_placement.m_halfDepth = 2.5;
        image.m_image.m_heightOrigin = 20.0;
        image.m_image.m_strength = 1.0;
        image.m_image.m_relativeEdgeBlend = false;
        auto pixels = AZStd::make_shared<HeightmapData>();
        pixels->m_width = 1;
        pixels->m_height = 1;
        pixels->m_samples = { 0.0f };
        image.m_image.m_image = pixels;

        PreparedHeightContributor mesh;
        mesh.m_type = PreparedHeightContributor::Type::Mesh;
        mesh.m_mesh = MakePreparedMeshHeight();
        const AZ::Vector3 position(11.5f, -4.25f, 0.0f);
        const PreparedHeightContributor meshLast[]{ image, mesh };
        EXPECT_FLOAT_EQ(ComposeHeightContributors(position, 0.0f, { 0.0, 100.0 }, meshLast), 0.0025f);
        const PreparedHeightContributor imageLast[]{ mesh, image };
        EXPECT_FLOAT_EQ(ComposeHeightContributors(position, 0.0f, { 0.0, 100.0 }, imageLast), 0.2f);
    }
} // namespace TerrainCompositor
