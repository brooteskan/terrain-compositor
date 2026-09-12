#include <AzTest/AzTest.h>
#include <TerrainRenderer/TerrainMeshManager.h>
#include <Tests/Mocks/Terrain/MockTerrainDataRequestBus.h>
#include "TerrainTestFixtures.h"
#include <cmath>

namespace Terrain
{
    // Friend of the renderer: exercise the actual gather, packing, CLOD and RT
    // conversion rather than a second implementation of terrain normal math.
    class TerrainRenderingTests : public ::testing::Test
    {
    protected:
        using Manager = TerrainMeshManager;
        using Vertex = Manager::HeightNormalVertex;

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
    TEST_F(TerrainRenderingTests, RayTracingNormalsRemainUnitLengthAtPackedDiskBoundary)
    { CheckRayTracingNormalBoundary(); }
}
