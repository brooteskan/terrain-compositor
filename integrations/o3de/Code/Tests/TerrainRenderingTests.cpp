#include <AzTest/AzTest.h>
#include <TerrainRenderer/TerrainMeshManager.h>
#include <Tests/Mocks/Terrain/MockTerrainDataRequestBus.h>
#include "TerrainTestFixtures.h"
#include <cmath>
#include <set>

namespace Terrain
{
    // Friend of the renderer: exercise the actual gather, packing, CLOD and RT
    // conversion rather than a second implementation of terrain normal math.
    class TerrainRenderingTests : public ::testing::Test
    {
    protected:
        using Manager = TerrainMeshManager;
        using Vertex = Manager::HeightNormalVertex;

        void CheckFlatGroundEligibility()
        {
            auto settings = Settings();
            settings->m_gridSize = 8;
            settings->m_gridVerts1D = 9;
            settings->m_gridVerts2D = 81;
            settings->m_clodEnabled = true;
            Manager::PreparedSectorResult result;
            result.m_request.m_data.m_settings = settings;
            result.m_status = Manager::SectorPreparationStatus::Ready;
            result.m_hasData = true;
            const auto reset = [&]()
            {
                result.m_heights.assign(81, Vertex{0.5f, {0, 0}});
                result.m_lodHeights = result.m_heights;
            };
            reset();
            EXPECT_TRUE(Manager::IsFlatGround(result));
            // Inspect every vertex, including interior relief and edge/halo normals.
            for (size_t index = 0; index < 81; ++index)
            {
                reset(); result.m_heights[index].m_height = std::nextafter(0.5f, 1.0f);
                EXPECT_FALSE(Manager::IsFlatGround(result));
                reset(); result.m_heights[index].m_height = Manager::NoTerrainVertexHeight;
                EXPECT_FALSE(Manager::IsFlatGround(result));
                reset(); result.m_heights[index].m_normal.first = 1;
                EXPECT_FALSE(Manager::IsFlatGround(result));
                reset(); result.m_lodHeights[index].m_height = 0.51f;
                EXPECT_FALSE(Manager::IsFlatGround(result));
                reset(); result.m_lodHeights[index].m_normal.second = 1;
                EXPECT_FALSE(Manager::IsFlatGround(result));
            }
            for (float invalid : {Manager::NoTerrainVertexHeight, -0.1f, 1.1f,
                AZStd::numeric_limits<float>::infinity(), AZStd::numeric_limits<float>::quiet_NaN()})
            {
                reset();
                for (auto& vertex : result.m_heights) vertex.m_height = invalid;
                for (auto& vertex : result.m_lodHeights) vertex.m_height = invalid;
                EXPECT_FALSE(Manager::IsFlatGround(result));
            }
            for (float height : {0.0f, 1.0f})
            {
                for (auto& vertex : result.m_heights) vertex.m_height = height;
                for (auto& vertex : result.m_lodHeights) vertex.m_height = height;
                EXPECT_TRUE(Manager::IsFlatGround(result));
            }
            reset(); result.m_lodHeights.pop_back();
            EXPECT_FALSE(Manager::IsFlatGround(result));
            settings->m_clodEnabled = false;
            EXPECT_TRUE(Manager::IsFlatGround(result));
            settings->m_simplifyFlatGround = false;
            EXPECT_FALSE(Manager::IsFlatGround(result));
            settings->m_simplifyFlatGround = true;
            result.m_hasData = false;
            EXPECT_FALSE(Manager::IsFlatGround(result));
        }

        void CheckFlatGroundTopology()
        {
            for (uint32_t grid : {8u, 16u, 32u, 64u, 128u})
                for (bool remapped : {false, true})
                {
                    SCOPED_TRACE(::testing::Message() << grid << " remapped " << remapped);
                    const uint32_t vertices = (grid + 1) * (grid + 1), side = grid / 2;
                    AZStd::vector<uint16_t> order(vertices);
                    for (uint32_t i = 0; i < vertices; ++i) order[i] = uint16_t(remapped ? vertices - 1 - i : i);
                    const auto indices = Manager::CreateFlatGroundIndices(grid, order);
                    ASSERT_EQ(indices.size(), grid * 24);
                    const auto coordinate = [&](uint16_t index)
                    {
                        const uint32_t linear = remapped ? vertices - 1 - index : index;
                        return AZStd::pair<int, int>{int(linear % (grid + 1)), int(linear / (grid + 1))};
                    };
                    for (uint32_t quadrant = 0; quadrant < 4; ++quadrant)
                    {
                        const int x = int(quadrant & 1) * side, y = int(quadrant >> 1) * side;
                        const int maxX = x + side, maxY = y + side;
                        int twiceArea = 0;
                        std::set<AZStd::array<int, 4>> boundary;
                        const size_t begin = quadrant * indices.size() / 4, end = begin + indices.size() / 4;
                        for (size_t i = begin; i < end; i += 3)
                        {
                            const auto a = coordinate(indices[i]), b = coordinate(indices[i + 1]), c = coordinate(indices[i + 2]);
                            const int cross = (b.first - a.first) * (c.second - a.second) - (b.second - a.second) * (c.first - a.first);
                            EXPECT_GT(cross, 0); // Same upward winding as the dense grid.
                            twiceArea += cross;
                            const AZStd::array<AZStd::pair<int, int>, 3> points{a, b, c};
                            for (size_t edge = 0; edge < 3; ++edge)
                            {
                                const auto p = points[edge], q = points[(edge + 1) % 3];
                                ASSERT_GE(p.first, x); ASSERT_LE(p.first, maxX);
                                ASSERT_GE(p.second, y); ASSERT_LE(p.second, maxY);
                                const bool perimeter = (p.first == q.first && (p.first == x || p.first == maxX)) ||
                                    (p.second == q.second && (p.second == y || p.second == maxY));
                                if (perimeter)
                                {
                                    EXPECT_EQ(abs(q.first - p.first) + abs(q.second - p.second), 1);
                                    EXPECT_TRUE(boundary.insert({p.first, p.second, q.first, q.second}).second);
                                }
                            }
                        }
                        EXPECT_EQ(twiceArea, 2 * int(side * side));
                        EXPECT_EQ(boundary.size(), 4 * side); // Every original unit boundary segment exactly once.
                    }
                }
        }

        void CheckFlatGroundRenderedPrecision()
        {
            auto settings = Settings();
            settings->m_gridSize = 8; settings->m_gridVerts1D = 9; settings->m_gridVerts2D = 81;
            settings->m_clodEnabled = true;
            Manager::PreparedSectorResult result;
            result.m_request.m_data.m_settings = settings;
            result.m_status = Manager::SectorPreparationStatus::Ready;
            result.m_hasData = true;
            result.m_heights.assign(81, Vertex{0.5f, {0, 0}});
            result.m_lodHeights = result.m_heights;
            // Different query floats, but exactly one height in the active GPU format.
            result.m_heights[40].m_height = std::nextafter(0.5f, 1.0f);
            result.m_lodHeights[17].m_height = std::nextafter(0.5f, 0.0f);
            EXPECT_TRUE(Manager::IsFlatGround(result, true));
            EXPECT_FALSE(Manager::IsFlatGround(result, false)); // Retain full-float raster/RT geometry.
            result.m_lodHeights[17].m_height = 0.50048828125f; // Adjacent representable half height.
            EXPECT_FALSE(Manager::IsFlatGround(result, true));
            result.m_lodHeights[17].m_height = Manager::NoTerrainVertexHeight;
            EXPECT_FALSE(Manager::IsFlatGround(result, true));
            result.m_lodHeights[17].m_height = 0.5f;
            result.m_heights[40].m_normal.first = 1;
            EXPECT_FALSE(Manager::IsFlatGround(result, true));
        }

        std::shared_ptr<Manager::SectorPreparationSettings> Settings()
        {
            auto settings = std::make_shared<Manager::SectorPreparationSettings>();
            settings->m_worldHeightBounds = { -1000.0f, 1000.0f };
            settings->m_gridSize = 2;
            settings->m_gridVerts1D = 3;
            settings->m_gridVerts2D = 9;
            settings->m_vertexOrder = { 8, 7, 6, 5, 4, 3, 2, 1, 0 };
            settings->m_xyPositions = { {2,2}, {1,2}, {0,2}, {2,1}, {1,1}, {0,1}, {2,0}, {1,0}, {0,0} };
            settings->m_batchQueries = false;
            return settings;
        }

        Manager::SectorDataRequest Request(float spacing, bool remap)
        {
            Manager::SectorDataRequest request;
            request.m_settings = Settings();
            request.m_worldStartPosition = AZ::Vector2(-4.0f, 3.0f);
            request.m_vertexSpacing = spacing;
            request.m_samplesX = request.m_samplesY = 3;
            request.m_useVertexOrderRemap = remap;
            return request;
        }

        void CheckPlaneNormals()
        {
            ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
            for (const AZ::Vector2 rise : { AZ::Vector2(0, 0), AZ::Vector2(1, 0), AZ::Vector2(-1, 0),
                     AZ::Vector2(0, 1), AZ::Vector2(0.5f, 0.5f), AZ::Vector2(1, 1), AZ::Vector2(2, 0.25f),
                     AZ::Vector2(-2, 2), AZ::Vector2(4, -3), AZ::Vector2(16, 16), AZ::Vector2(-16, 16) })
            {
                SCOPED_TRACE(::testing::Message() << "rise " << rise.GetX() << ", " << rise.GetY());
                ON_CALL(terrain, QueryRegion).WillByDefault([rise](const auto& region, auto, auto callback, auto)
                {
                    for (size_t y = 0; y < region.m_numPointsY; ++y)
                        for (size_t x = 0; x < region.m_numPointsX; ++x)
                        {
                            const float worldX = region.m_startPoint.GetX() + x * region.m_stepSize.GetX();
                            const float worldY = region.m_startPoint.GetY() + y * region.m_stepSize.GetY();
                            AzFramework::SurfaceData::SurfacePoint point;
                            point.m_position = AZ::Vector3(worldX, worldY, 23.0f + rise.GetX() * worldX + rise.GetY() * worldY);
                            callback(x, y, point, true);
                        }
                });
                // Independent geometric oracle: cross two tangents of the plane.
                const AZ::Vector3 expected = AZ::Vector3(1, 0, rise.GetX()).Cross(
                    AZ::Vector3(0, 1, rise.GetY())).GetNormalized();
                // XY packing is 1/127; Z becomes less precise near the horizon.
                const float tolerance = expected.GetZ() < 0.2f ? 0.09f : 0.025f;
                for (float spacing : { 0.5f, 2.0f })
                    for (bool remap : { false, true })
                    {
                        auto request = Request(spacing, remap);
                        AZStd::vector<Vertex> regular, coarse, clod;
                        AZ::Aabb bounds;
                        bool exists = false;
                        ASSERT_TRUE(Manager::GatherMeshData(request, regular, bounds, exists));
                        ASSERT_TRUE(exists);
                        auto coarseRequest = request;
                        coarseRequest.m_samplesX = coarseRequest.m_samplesY = 2;
                        coarseRequest.m_vertexSpacing *= 2;
                        coarseRequest.m_useVertexOrderRemap = false;
                        ASSERT_TRUE(Manager::GatherMeshData(coarseRequest, coarse, bounds, exists));
                        Manager::PrepareSectorLodData(*request.m_settings, regular, coarse, clod);
                        for (const auto* packed : { &regular, &clod })
                        {
                            AZStd::vector<Manager::RtVertex> positions, normals;
                            Manager::PrepareSectorRayTracingData(*request.m_settings, *packed, positions, normals);
                            ASSERT_EQ(normals.size(), 9);
                            for (size_t i = 0; i < normals.size(); ++i)
                            {
                                EXPECT_NEAR((*packed)[i].m_normal.first / 127.0f, expected.GetX(), 0.5f / 127.0f + 1e-6f);
                                EXPECT_NEAR((*packed)[i].m_normal.second / 127.0f, expected.GetY(), 0.5f / 127.0f + 1e-6f);
                                const AZ::Vector3 actual(normals[i].x, normals[i].y, normals[i].z);
                                EXPECT_TRUE(actual.IsFinite());
                                EXPECT_NEAR(actual.GetLength(), 1.0f, 1e-6f);
                                EXPECT_TRUE(actual.IsClose(expected, tolerance));
                            }
                        }
                    }
            }
        }

        void CheckHeightPrecision()
        {
            ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
            // Millimeter-scale relief must survive a 2048 m world range. Include
            // both range endpoints: normalized height 1 is valid terrain, not a hole.
            for (float base : { -1024.0f, 0.0f, 1023.99f })
                for (bool remap : { false, true })
                {
                    SCOPED_TRACE(::testing::Message() << "base " << base << ", remap " << remap);
                    auto request = Request(0.5f, remap);
                    auto settings = Settings();
                    settings->m_worldHeightBounds = { -1024.0f, 1024.0f };
                    request.m_settings = settings;
                    const auto heightAt = [base](float x, float y)
                    {
                        return AZ::GetClamp(base + (x + 4.0f) * 0.006f + (y - 3.0f) * 0.004f, -1024.0f, 1024.0f);
                    };
                    ON_CALL(terrain, QueryRegion).WillByDefault([&](const auto& region, auto, auto callback, auto)
                    {
                        for (size_t y = 0; y < region.m_numPointsY; ++y)
                            for (size_t x = 0; x < region.m_numPointsX; ++x)
                            {
                                const float wx = region.m_startPoint.GetX() + x * region.m_stepSize.GetX();
                                const float wy = region.m_startPoint.GetY() + y * region.m_stepSize.GetY();
                                AzFramework::SurfaceData::SurfacePoint point;
                                point.m_position = AZ::Vector3(wx, wy, heightAt(wx, wy));
                                callback(x, y, point, true);
                            }
                    });
                    AZStd::vector<Vertex> regular, coarse, clod;
                    AZ::Aabb bounds;
                    bool exists = false;
                    ASSERT_TRUE(Manager::GatherMeshData(request, regular, bounds, exists));
                    ASSERT_TRUE(exists);
                    auto coarseRequest = request;
                    coarseRequest.m_samplesX = coarseRequest.m_samplesY = 2;
                    coarseRequest.m_vertexSpacing = 1.0f;
                    coarseRequest.m_useVertexOrderRemap = false;
                    ASSERT_TRUE(Manager::GatherMeshData(coarseRequest, coarse, bounds, exists));
                    // The production LOD writer always emits remapped vertices.
                    Manager::PrepareSectorLodData(*settings, regular, coarse, clod);
                    for (bool useClod : { false, true })
                    {
                        const auto& vertices = useClod ? clod : regular;
                        AZStd::vector<Manager::RtVertex> positions, normals;
                        Manager::PrepareSectorRayTracingData(*settings, vertices, positions, normals);
                        for (size_t y = 0; y < 3; ++y)
                            for (size_t x = 0; x < 3; ++x)
                            {
                                const size_t linear = y * 3 + x;
                                const size_t index = useClod || remap ? settings->m_vertexOrder[linear] : linear;
                                const float expected = heightAt(-4.0f + x * 0.5f, 3.0f + y * 0.5f);
                                ASSERT_NE(vertices[index].m_height, Manager::NoTerrainVertexHeight);
                                const float rendered = -1024.0f + vertices[index].m_height * 2048.0f;
                                EXPECT_NEAR(rendered, expected, 0.00025f);
                                EXPECT_NEAR(-1024.0f + positions[index].z * 2048.0f, expected, 0.00025f);
                            }
                    }
                }
        }

        void CheckHeightStreamLayout()
        {
            Manager manager;
            const auto layout = manager.CreateTerrainInputStreamLayout();
            ASSERT_TRUE(layout.IsFinalized());
            const auto buffers = layout.GetStreamBuffers();
            const auto channels = layout.GetStreamChannels();
            ASSERT_EQ(buffers.size(), 5);
            ASSERT_EQ(channels.size(), 5);
            for (size_t stream = 1; stream < 5; ++stream)
            {
                EXPECT_EQ(buffers[stream].m_byteStride, sizeof(Vertex));
                EXPECT_EQ(channels[stream].m_bufferIndex, stream);
                EXPECT_EQ(channels[stream].m_byteOffset, 0); // Buffer views provide the normal offset.
                EXPECT_EQ(channels[stream].m_format,
                    stream % 2 ? AZ::RHI::Format::R32_FLOAT : AZ::RHI::Format::R8G8_SNORM);
            }
            EXPECT_EQ(offsetof(Vertex, m_normal), sizeof(float));
            EXPECT_LT(Manager::NoTerrainVertexHeight, 0.0f);
        }

        void CheckHalfHeightStreamLayout()
        {
            Manager manager;
            manager.m_heightFp16 = true;
            const auto layout = manager.CreateTerrainInputStreamLayout();
            ASSERT_TRUE(layout.IsFinalized());
            const auto buffers = layout.GetStreamBuffers();
            const auto channels = layout.GetStreamChannels();
            for (size_t stream = 1; stream < 5; ++stream)
            {
                EXPECT_EQ(buffers[stream].m_byteStride, 4);
                EXPECT_EQ(channels[stream].m_format,
                    stream % 2 ? AZ::RHI::Format::R16_FLOAT : AZ::RHI::Format::R8G8_SNORM);
            }
            AZStd::vector<Vertex> vertices{{0.0f, {-127,127}}, {1.0f, {0,0}}, {-1.0f, {1,-1}}};
            const auto packed = manager.PackHeightVertices(vertices);
            ASSERT_EQ(packed.size(), vertices.size());
            EXPECT_EQ(packed[0], 0x7f810000u);
            EXPECT_EQ(packed[1], 0x00003c00u);
            EXPECT_EQ(packed[2], 0xff01bc00u);
            EXPECT_FLOAT_EQ(vertices[2].m_height, -1.0f); // master records remain untouched
        }

        void CheckHeightBounds()
        {
            ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
            // -1 denotes a missing sample; all other values are relative to the
            // world minimum, exactly as in the production gather.
            const AZStd::vector<AZStd::vector<float>> cases{
                { 10, 9, 8, 7, 6, 5, 4, 3, 2 },
                { 10, 5, 7, 8, 6, 4, 2, 3, 1 },
                { 1, 2, 3, 4, 5, 6, 7, 8, 9 },
                { 5, 5, 5, 5, 5, 5, 5, 5, 5 },
                { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
                { 5, -1, -1, -1, -1, -1, -1, -1, -1 },
                { -1, -1, -1, -1, 5, -1, -1, -1, -1 },
                { -1, -1, -1, -1, -1, -1, -1, -1, -1 }
            };
            for (size_t caseIndex = 0; caseIndex < cases.size(); ++caseIndex)
                for (bool remap : { false, true })
                {
                    SCOPED_TRACE(::testing::Message() << "case " << caseIndex << ", remap " << remap);
                    auto request = Request(0.5f, remap);
                    const auto& heights = cases[caseIndex];
                    const float worldMin = request.m_settings->m_worldHeightBounds.m_min;
                    ON_CALL(terrain, QueryRegion).WillByDefault([&](const auto& region, auto, auto callback, auto)
                    {
                        for (size_t y = 0; y < region.m_numPointsY; ++y)
                            for (size_t x = 0; x < region.m_numPointsX; ++x)
                            {
                                // Keep the normal halo missing, including for the all-missing case.
                                const float height = x > 0 && x < 4 && y > 0 && y < 4 ? heights[(y - 1) * 3 + x - 1] : -1;
                                AzFramework::SurfaceData::SurfacePoint point;
                                point.m_position = AZ::Vector3(region.m_startPoint.GetX() + x * region.m_stepSize.GetX(),
                                    region.m_startPoint.GetY() + y * region.m_stepSize.GetY(), worldMin + height);
                                callback(x, y, point, height >= 0);
                            }
                    });
                    AZStd::vector<Vertex> packed;
                    AZ::Aabb actual;
                    bool exists = false;
                    ASSERT_TRUE(Manager::GatherMeshData(request, packed, actual, exists));
                    AZ::Aabb expected = AZ::Aabb::CreateNull();
                    for (size_t i = 0; i < heights.size(); ++i)
                        if (heights[i] >= 0)
                        {
                            // Sector XY covers the complete patch; Z encloses only valid samples.
                            expected.AddPoint(AZ::Vector3(-4, 3, worldMin + heights[i]));
                            expected.AddPoint(AZ::Vector3(-3, 4, worldMin + heights[i]));
                            EXPECT_TRUE(actual.Contains(AZ::Vector3(-4 + (i % 3) * 0.5f, 3 + (i / 3) * 0.5f,
                                worldMin + heights[i])));
                        }
                    EXPECT_EQ(exists, expected.IsValid());
                    EXPECT_EQ(actual, expected);
                }
        }

        void CheckRayTracingNormalBoundary()
        {
            const auto settings = Settings();
            AZStd::vector<Vertex> packed(9);
            // 90/127 on both axes lies outside the unit disk. Include signs,
            // exact horizon normals, and a regular upward normal.
            const AZStd::array<AZStd::pair<int8_t, int8_t>, 9> xy{
                { {90,90}, {-90,90}, {90,-90}, {-90,-90}, {127,0}, {-127,0}, {0,127}, {0,-127}, {0,0} }
            };
            for (size_t i = 0; i < packed.size(); ++i) packed[i].m_normal = xy[i];
            AZStd::vector<Manager::RtVertex> positions, normals;
            Manager::PrepareSectorRayTracingData(*settings, packed, positions, normals);
            for (size_t i = 0; i < normals.size(); ++i)
            {
                const AZ::Vector3 normal(normals[i].x, normals[i].y, normals[i].z);
                EXPECT_TRUE(normal.IsFinite());
                EXPECT_NEAR(normal.GetLength(), 1.0f, 1e-6f);
                EXPECT_FLOAT_EQ(normal.GetZ(), i == 8 ? 1.0f : 0.0f);
            }
        }

        TerrainCompositor::TestSupport::ScopedNameDictionary m_names;
    };

    TEST_F(TerrainRenderingTests, PackedPlaneNormalsMatchGeometricOracleIncludingClodAndRayTracing)
    { CheckPlaneNormals(); }
    TEST_F(TerrainRenderingTests, HeightBoundsEncloseAllValidSamplesRegardlessOfTraversalOrder)
    { CheckHeightBounds(); }
    TEST_F(TerrainRenderingTests, MillimeterReliefSurvivesWideHeightRangeThroughGatherClodAndRayTracing)
    { CheckHeightPrecision(); }
    TEST_F(TerrainRenderingTests, HeightAndNormalStreamsUseTheInterleavedFloatRecordStride)
    { CheckHeightStreamLayout(); }
    TEST_F(TerrainRenderingTests, HalfHeightAndNormalStreamsPackRegularAndClodRecordsInFourBytes)
    { CheckHalfHeightStreamLayout(); }
    TEST_F(TerrainRenderingTests, RayTracingNormalsRemainUnitLengthAtPackedDiskBoundary)
    { CheckRayTracingNormalBoundary(); }
    TEST_F(TerrainRenderingTests, FlatGroundRequiresExactRegularClodHeightAndUpwardNormalsWithoutHoles)
    { CheckFlatGroundEligibility(); }
    TEST_F(TerrainRenderingTests, FlatGroundFansPreserveAllQuadrantBoundariesWindingAndArea)
    { CheckFlatGroundTopology(); }
    TEST_F(TerrainRenderingTests, FlatGroundUsesExactSelectedGpuPrecisionAndPreservesFullFloatRayTracing)
    { CheckFlatGroundRenderedPrecision(); }
}
