#include <AzCore/std/algorithm.h>
#include <AzCore/Jobs/JobContext.h>
#include <AzCore/Jobs/JobManager.h>
#include <AzCore/Jobs/JobManagerBus.h>
#include "TerrainTestFixtures.h"
#include <AzTest/AzTest.h>
#include <Atom/RHI/RHISystem.h>
#include <TerrainCompositor/Components/ProceduralGroundGradientComponent.h>
#include <TerrainCompositor/Components/TerrainCompositionGradientComponent.h>
#include <TerrainCompositor/Components/TerrainMeshCutoutConfig.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>
#include <TerrainCompositor/TerrainMeshCutoutData.h>
#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>
#include <TerrainCompositor/TerrainMeshCutoutSampling.h>
#include <TerrainRenderer/TerrainMeshManager.h>
#include <TerrainRaycast/TerrainRaycastExistence.h>
#include <TerrainSystem/TerrainSystem.h>
#include <Components/TerrainPhysicsColliderComponent.h>
#include <Terrain/MockTerrain.h>
#include <Terrain/MockTerrainAreaSurfaceRequestBus.h>
#include <LmbrCentral/Shape/MockShapes.h>
#include <Mocks/Terrain/MockTerrainDataRequestBus.h>
#include <chrono>
#include <cstdio>

namespace Terrain
{
    class TerrainSectorPreparationTests : public ::testing::Test
    {
    protected:
        using Vertex = TerrainMeshManager::HeightNormalVertex;
        static void CheckRenderQueryPreparation()
        {
            using namespace TerrainCompositor;
            using Requests = AzFramework::Terrain::TerrainDataRequests;
            ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
            auto snapshot = std::make_shared<TerrainMeshCutoutRenderSnapshot>();
            TerrainRenderGeometryQuery query;
            query.m_regionBounds = AZ::Aabb::CreateFromMinMax({ 0, 0, -1 }, { 4, 4, 1 });
            query.m_getHeight = [](const AZ::Vector3& p) { return 30.0f * p.GetX() - 20.0f * p.GetY(); };
            query.m_getTerrainExists = [](const AZ::Vector3& p) { return p.GetZ() < 0 && !(p.GetX() == 1 && p.GetY() == 1); };
            query.m_getGeometry = [query](auto points, auto heights, auto exists)
            {
                for (size_t i = 0; i < points.size(); ++i)
                {
                    exists[i] = query.m_getTerrainExists(points[i]);
                    heights[i] = query.m_getHeight(points[i]);
                }
            };
            snapshot->m_renderGeometryQueries.push_back(query);
            bool reference = false;
            size_t ordinaryCalls = 0;
            Requests::Sampler requestedSampler = Requests::Sampler::EXACT;
            ON_CALL(terrain, QueryRegion).WillByDefault([&](const auto& region, auto mask, auto callback, auto sampler)
            {
                ++ordinaryCalls;
                EXPECT_EQ(mask, Requests::TerrainDataMask::Heights);
                EXPECT_EQ(sampler, requestedSampler);
                const size_t count = region.m_numPointsX * region.m_numPointsY;
                AZStd::vector<AZ::Vector3> points(count);
                AZStd::vector<float> heights(count);
                auto exists = std::make_unique<bool[]>(count);
                for (size_t y = 0; y < region.m_numPointsY; ++y)
                    for (size_t x = 0; x < region.m_numPointsX; ++x)
                    {
                        size_t i = y * region.m_numPointsX + x;
                        points[i] = region.m_startPoint + AZ::Vector3(float(x) * region.m_stepSize.GetX(), float(y) * region.m_stepSize.GetY(), 0);
                        points[i].SetZ(x % 3 == 0 ? 1000.0f : -1000.0f);
                        heights[i] = points[i].GetZ();
                        exists[i] = false; // Ordinary collision fallback must be replaced only within XY ownership.
                    }
                if (reference) ApplyTerrainRenderGeometry(*snapshot, points, heights, { exists.get(), count });
                for (size_t y = 0; y < region.m_numPointsY; ++y)
                    for (size_t x = 0; x < region.m_numPointsX; ++x)
                    {
                        size_t i = y * region.m_numPointsX + x;
                        AzFramework::SurfaceData::SurfacePoint surface;
                        surface.m_position = points[i];
                        surface.m_position.SetZ(heights[i]);
                        callback(x, y, surface, exists[i]);
                    }
            });
            TerrainMeshManager manager;
            manager.m_worldHeightBounds = { -50, 50 };
            manager.m_gridSize = 4;
            manager.m_gridVerts1D = 5;
            manager.m_gridVerts2D = 25;
            for (uint16_t i = 0; i < 25; ++i) manager.m_vertexOrder.push_back(24 - i);
            for (auto sampler : { Requests::Sampler::EXACT, Requests::Sampler::CLAMP, Requests::Sampler::BILINEAR })
            {
                requestedSampler = sampler;
                AZStd::vector<Vertex> expected[2], actual[2];
                for (int lod = 0; lod < 2; ++lod)
                {
                    TerrainMeshManager::SectorDataRequest request;
                    request.m_settings = manager.CapturePreparationSettings();
                    request.m_worldStartPosition = AZ::Vector2::CreateZero();
                    request.m_vertexSpacing = lod ? 2.0f : 1.0f;
                    request.m_samplesX = request.m_samplesY = lod ? 3 : 5;
                    request.m_samplerType = sampler;
                    request.m_useVertexOrderRemap = lod == 0;
                    AZ::Aabb expectedBounds = AZ::Aabb::CreateNull(), actualBounds = AZ::Aabb::CreateNull();
                    bool expectedExists = false, actualExists = false;
                    // Initialize hole normals: GatherMeshData deliberately writes only their sentinel height.
                    expected[lod].resize(request.m_samplesX * request.m_samplesY, Vertex{});
                    actual[lod] = expected[lod];
                    reference = true;
                    manager.GatherMeshData(request, expected[lod], expectedBounds, expectedExists);
                    reference = false;
                    request.m_renderSnapshot = snapshot;
                    TerrainRenderQueryStatistics statistics;
                    request.m_queryStatistics = &statistics;
                    manager.GatherMeshData(request, actual[lod], actualBounds, actualExists);
                    EXPECT_EQ(actualExists, expectedExists);
                    EXPECT_EQ(actualBounds, expectedBounds);
                    ASSERT_EQ(actual[lod].size(), expected[lod].size());
                    for (size_t i = 0; i < actual[lod].size(); ++i)
                    {
                        EXPECT_EQ(actual[lod][i].m_height, expected[lod][i].m_height);
                        EXPECT_EQ(actual[lod][i].m_normal, expected[lod][i].m_normal);
                    }
                    EXPECT_EQ(statistics.m_ordinarySamples, size_t((request.m_samplesX + 2) * (request.m_samplesY + 2)));
                    EXPECT_GT(statistics.m_retainedSamples, 0);
                }
                AZStd::vector<Vertex> expectedClod, actualClod;
                manager.PrepareSectorLodData(*manager.CapturePreparationSettings(), expected[0], expected[1], expectedClod);
                manager.PrepareSectorLodData(*manager.CapturePreparationSettings(), actual[0], actual[1], actualClod);
                ASSERT_EQ(actualClod.size(), expectedClod.size());
                for (size_t i = 0; i < actualClod.size(); ++i)
                {
                    EXPECT_EQ(actualClod[i].m_height, expectedClod[i].m_height);
                    EXPECT_EQ(actualClod[i].m_normal, expectedClod[i].m_normal);
                }
            }
            EXPECT_EQ(ordinaryCalls, 12); // One unchanged QueryRegion per reference/candidate and LOD.
        }
        static void CheckSectorGridCrossing(bool crossX, bool crossY)
        {
            // GeometryView enumerates devices even when no GPU resources are
            // created. Supply an empty RHI for these CPU-only sector objects.
            ASSERT_EQ(AZ::RHI::RHISystemInterface::Get(), nullptr);
            struct ScopedEmptyRhi
            {
                AZ::RHI::RHISystem m_system;
                ScopedEmptyRhi() { AZ::Interface<AZ::RHI::RHISystemInterface>::Register(&m_system); }
                ~ScopedEmptyRhi() { AZ::Interface<AZ::RHI::RHISystemInterface>::Unregister(&m_system); }
            } rhi;
            TerrainMeshManager manager;
            manager.m_gridSize = 128;
            // These movement tests also prepare complete empty requests. Supply
            // the production grid dimensions so admission validates both gathers.
            manager.m_gridVerts1D = 129;
            manager.m_gridVerts2D = 129 * 129;
            manager.m_sampleSpacing = 0.5f;
            manager.m_config.m_firstLodDistance = 256.0f;
            manager.m_1dSectorCount = 10; // Non-power-of-two grid from DefaultLevel.
            manager.m_sectorLods.resize(1);
            auto& grid = manager.m_sectorLods.front();
            grid.m_startCoord = { 100, 100 };
            grid.m_sectors.resize(100);
            for (auto& sector : grid.m_sectors)
            {
                sector.m_requestedWorldCoord = { 100, 100 };
            }
            auto updates = manager.CollectUpdatedSectors(AZ::Vector3(192.0f, 192.0f, 0.0f));
            ASSERT_EQ(updates[0].size(), 100);
            ASSERT_EQ(grid.m_startCoord, Vector2i(-1, -1));

            // A one-column/row shift must preserve every overlapping sector's
            // buffer slot, including crossings of zero in either direction.
            const AZ::Vector3 positivePosition(crossX ? 320.0f : 192.0f, crossY ? 320.0f : 192.0f, 0.0f);
            const size_t expectedUpdates = crossX && crossY ? 19 : 10;
            for (const auto& position : { positivePosition, AZ::Vector3(192.0f, 192.0f, 0.0f), positivePosition })
            {
                AZStd::vector<Vector2i> oldCoordinates;
                AZStd::vector<Vector2i> committedCoordinates;
                AZStd::vector<std::shared_ptr<TerrainMeshManager::PreparedSectorResult>> delayed;
                const auto settings = manager.CapturePreparationSettings();
                for (size_t slot = 0; slot < grid.m_sectors.size(); ++slot)
                    delayed.push_back(std::make_shared<TerrainMeshManager::PreparedSectorResult>(
                        manager.PrepareSector(manager.CaptureSectorRequest(0, slot, settings, {}, false, {}))));
                for (const auto& sector : grid.m_sectors)
                {
                    oldCoordinates.push_back(sector.m_requestedWorldCoord);
                    committedCoordinates.push_back(sector.m_committed.m_worldCoord);
                }
                updates = manager.CollectUpdatedSectors(position);
                EXPECT_EQ(updates[0].size(), expectedUpdates);
                for (size_t slot = 0; slot < grid.m_sectors.size(); ++slot)
                    EXPECT_EQ(grid.m_sectors[slot].m_committed.m_worldCoord, committedCoordinates[slot]);
                size_t commits = 0;
                for (const auto& result : delayed)
                {
                    const bool stillAssigned = result->m_request.m_worldCoord == grid.m_sectors[result->m_request.m_slot].m_requestedWorldCoord;
                    EXPECT_EQ(manager.AcceptPreparedSectors({ &result, 1 },
                        [&commits](auto& sector, const auto& prepared)
                        {
                            EXPECT_EQ(sector.m_committed.m_worldCoord, prepared.m_request.m_worldCoord);
                            EXPECT_EQ(sector.m_state, TerrainMeshManager::SectorState::Ready);
                            ++commits;
                        }), stillAssigned);
                }
                EXPECT_EQ(commits, 100 - expectedUpdates);
                for (size_t oldIndex = 0; oldIndex < oldCoordinates.size(); ++oldIndex)
                {
                    for (size_t newIndex = 0; newIndex < grid.m_sectors.size(); ++newIndex)
                    {
                        if (oldCoordinates[oldIndex] == grid.m_sectors[newIndex].m_requestedWorldCoord)
                        {
                            EXPECT_EQ(oldIndex, newIndex) << "An overlapping sector changed buffer slots";
                        }
                    }
                }
                for (int32_t y = 0; y < 10; ++y)
                {
                    for (int32_t x = 0; x < 10; ++x)
                    {
                        const Vector2i expected = grid.m_startCoord + Vector2i(x, y);
                        EXPECT_EQ(AZStd::count_if(grid.m_sectors.begin(), grid.m_sectors.end(),
                            [&expected](const auto& sector) { return sector.m_requestedWorldCoord == expected; }), 1);
                    }
                }
                EXPECT_TRUE(manager.CollectUpdatedSectors(position)[0].empty());
            }
            // A teleport replaces the whole requested grid but cannot relabel any
            // of the committed slots, including delayed empty results.
            AZStd::vector<Vector2i> displayed;
            AZStd::vector<std::shared_ptr<TerrainMeshManager::PreparedSectorResult>> delayed;
            for (size_t slot = 0; slot < grid.m_sectors.size(); ++slot)
            {
                displayed.push_back(grid.m_sectors[slot].m_committed.m_worldCoord);
                delayed.push_back(std::make_shared<TerrainMeshManager::PreparedSectorResult>(
                    manager.PrepareSector(manager.CaptureSectorRequest(0, slot, manager.CapturePreparationSettings(), {}, false, {}))));
            }
            EXPECT_EQ(manager.CollectUpdatedSectors(AZ::Vector3(-100000.0f,100000.0f,0))[0].size(),100);
            for (size_t slot = 0; slot < grid.m_sectors.size(); ++slot)
            {
                EXPECT_EQ(grid.m_sectors[slot].m_committed.m_worldCoord, displayed[slot]);
                EXPECT_FALSE(manager.AcceptPreparedSectors({&delayed[slot],1}, [](auto&, const auto&)
                    { ADD_FAILURE() << "A pre-teleport result reached resource commit"; }));
            }
        }
        static void CheckMissingTerrainProvider()
        {
            ASSERT_FALSE(AzFramework::Terrain::TerrainDataRequestBus::HasHandlers());
            TerrainMeshManager manager;
            auto snapshot = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderSnapshot>();
            TerrainCompositor::TerrainRenderGeometryQuery query;
            query.m_regionBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-10.0f), AZ::Vector3(10.0f));
            size_t calls = 0;
            query.m_getHeight = [&calls](const AZ::Vector3&) { ++calls; return 1.0f; };
            query.m_getTerrainExists = [&calls](const AZ::Vector3&) { ++calls; return true; };
            snapshot->m_renderGeometryQueries.push_back(query);
            TerrainMeshManager::SectorDataRequest request;
            request.m_settings = manager.CapturePreparationSettings();
            request.m_renderSnapshot = snapshot;
            request.m_worldStartPosition = AZ::Vector2::CreateZero();
            request.m_vertexSpacing = 1.0f;
            request.m_samplesX = request.m_samplesY = 3;
            AZStd::vector<Vertex> vertices;
            AZ::Aabb bounds = AZ::Aabb::CreateNull();
            bool exists = false;
            manager.GatherMeshData(request, vertices, bounds, exists);
            EXPECT_FALSE(exists);
            EXPECT_FALSE(bounds.IsValid());
            EXPECT_EQ(calls, 0);
        }
        static void CheckClodPreparation()
        {
            TerrainMeshManager manager;
            manager.m_gridSize = 2;
            manager.m_gridVerts1D = 3;
            manager.m_gridVerts2D = 9;
            manager.m_vertexOrder = { 8, 7, 6, 5, 4, 3, 2, 1, 0 }; // Exercise remapped output.
            AZStd::vector<Vertex> original(9, Vertex{ 42, { -8, 6 } });
            AZStd::vector<Vertex> lod{ { 10, { -11, 5 } }, { 20, { 4, -2 } }, { 30, { -6, -7 } }, { 40, { 8, 9 } } };
            AZStd::vector<Vertex> prepared;
            manager.PrepareSectorLodData(*manager.CapturePreparationSettings(), original, lod, prepared);
            const uint16_t expected[] = { 10, 15, 20, 20, 25, 30, 30, 35, 40 };
            ASSERT_EQ(prepared.size(), 9);
            for (size_t i = 0; i < 9; ++i)
                EXPECT_EQ(prepared[8 - i].m_height, expected[i]);
            EXPECT_EQ(prepared[7].m_normal.first, -3); // Signed average truncates toward zero.
            EXPECT_EQ(prepared[7].m_normal.second, 1);
            lod[1].m_height = TerrainMeshManager::NoTerrainVertexHeight;
            manager.PrepareSectorLodData(*manager.CapturePreparationSettings(), original, lod, prepared);
            EXPECT_EQ(prepared[7].m_height, 42);
            EXPECT_EQ(prepared[4].m_height, 42);
            EXPECT_EQ(prepared[7].m_normal, original[7].m_normal);
            EXPECT_EQ(prepared[8].m_height, 10); // Unaffected parent stays valid.
        }
        static void CheckRayTracingPreparation()
        {
            TerrainMeshManager manager;
            manager.m_gridSize = 2;
            manager.m_gridVerts2D = 3;
            manager.m_xyPositions = { { 0, 0 }, { 1, 2 }, { 2, 1 } };
            AZStd::vector<Vertex> source{ { 0, { 0, 0 } },
                                          { 32768, { 127, 0 } },
                                          { TerrainMeshManager::NoTerrainVertexHeight, { 0, -127 } } };
            AZStd::vector<TerrainMeshManager::RtVertex> positions, normals;
            manager.PrepareSectorRayTracingData(*manager.CapturePreparationSettings(), source, positions, normals);
            ASSERT_EQ(positions.size(), 3);
            ASSERT_EQ(normals.size(), 3);
            EXPECT_EQ(sizeof(TerrainMeshManager::RtVertex), 3 * sizeof(float));
            EXPECT_FLOAT_EQ(positions[1].x, 0.5f);
            EXPECT_FLOAT_EQ(positions[1].y, 1.0f);
            EXPECT_FLOAT_EQ(positions[1].z, 32768.0f / 65535.0f);
            EXPECT_FLOAT_EQ(positions[2].z, 0.0f);
            EXPECT_FLOAT_EQ(normals[0].z, 1.0f);
            EXPECT_FLOAT_EQ(normals[1].x, 1.0f);
            EXPECT_FLOAT_EQ(normals[1].z, 0.0f);
            EXPECT_FLOAT_EQ(normals[2].y, -1.0f);
            EXPECT_FLOAT_EQ(normals[2].z, 0.0f);
        }

    private:
        TerrainCompositor::TestSupport::ScopedNameDictionary m_names;
    };
    TEST_F(TerrainSectorPreparationTests, ClodPreservesInterpolationRemappingAndHoleFallback)
    {
        CheckClodPreparation();
    }
    TEST_F(TerrainSectorPreparationTests, RenderOwnershipPreservesClampedHeightsNormalsClodAndPackedVertices)
    {
        CheckRenderQueryPreparation();
    }
    TEST_F(TerrainSectorPreparationTests, CrossingZeroXPreservesOverlappingSectorBuffers)
    {
        CheckSectorGridCrossing(true, false);
    }
    TEST_F(TerrainSectorPreparationTests, CrossingZeroYPreservesOverlappingSectorBuffers)
    {
        CheckSectorGridCrossing(false, true);
    }
    TEST_F(TerrainSectorPreparationTests, CrossingZeroDiagonallyUpdatesOnlyEnteringRowAndColumn)
    {
        CheckSectorGridCrossing(true, true);
    }
    TEST_F(TerrainSectorPreparationTests, MissingTerrainProviderDoesNotSampleUninitializedPositions)
    {
        CheckMissingTerrainProvider();
    }
    TEST_F(TerrainSectorPreparationTests, RayTracingPreservesPackedPositionAndNormalDecoding)
    {
        CheckRayTracingPreparation();
    }

    TEST(TerrainRaycastExistenceTests, RejectsRemovedCandidateAndKeepsValidSecondTriangle)
    {
        const TerrainRaycastCandidate first{ AZ::Vector3::CreateAxisZ(), 0.25f, true };
        const TerrainRaycastCandidate second{ AZ::Vector3::CreateAxisY(), 0.75f, true };
        AZStd::vector<float> queried;
        const auto* selected = SelectNearestExistingTerrainCandidate(
            first, second,
            [&queried](float distance)
            {
                queried.push_back(distance);
                return distance > 0.5f;
            });
        ASSERT_EQ(selected, &second);
        EXPECT_EQ(queried, (AZStd::vector<float>{ 0.25f, 0.75f }));
    }

    TEST(TerrainRaycastExistenceTests, SelectsNearestExistingCandidateAndContinuesAfterEmptyCell)
    {
        const TerrainRaycastCandidate first{ AZ::Vector3::CreateAxisZ(), 0.2f, true };
        const TerrainRaycastCandidate second{ AZ::Vector3::CreateAxisZ(), 0.8f, true };
        EXPECT_EQ(SelectNearestExistingTerrainCandidate(first, second, [](float) { return true; }), &first);
        EXPECT_EQ(SelectNearestExistingTerrainCandidate(first, second, [](float) { return false; }), nullptr);
        const TerrainRaycastCandidate missed{ AZ::Vector3::CreateZero(), 0.0f, false };
        EXPECT_EQ(SelectNearestExistingTerrainCandidate(missed, second, [](float) { return true; }), &second);
    }

    class TerrainPublicExistenceTests : public ::testing::Test
    {
    protected:
        class JobManagerProvider : public AZ::JobManagerBus::Handler
        {
        public:
            JobManagerProvider()
            {
                AZ::JobManagerDesc descriptor;
                m_manager = AZStd::make_unique<AZ::JobManager>(descriptor);
                m_context = AZStd::make_unique<AZ::JobContext>(*m_manager);
                AZ::JobManagerBus::Handler::BusConnect();
            }

            ~JobManagerProvider() override
            {
                AZ::JobManagerBus::Handler::BusDisconnect();
            }

            AZ::JobManager* GetManager() override { return m_manager.get(); }
            AZ::JobContext* GetGlobalContext() override { return m_context.get(); }

        private:
            AZStd::unique_ptr<AZ::JobManager> m_manager;
            AZStd::unique_ptr<AZ::JobContext> m_context;
        };

        void SetUp() override
        {
            m_jobs = AZStd::make_unique<JobManagerProvider>();
            m_shape = AZStd::make_unique<::testing::NiceMock<UnitTest::MockShapeComponentRequests>>(m_regionId);
            m_spawner = AZStd::make_unique<::testing::NiceMock<UnitTest::MockTerrainSpawnerRequests>>(m_regionId);
            m_heights = AZStd::make_unique<::testing::NiceMock<UnitTest::MockTerrainAreaHeightRequests>>(m_regionId);
            m_surfaces = AZStd::make_unique<::testing::NiceMock<UnitTest::MockTerrainAreaSurfaceRequestBus>>(m_regionId);
            ON_CALL(*m_shape, GetEncompassingAabb)
                .WillByDefault(::testing::Return(
                    AZ::Aabb::CreateFromMinMaxValues(-2.0f, -2.0f, -2.0f, 4.0f, 4.0f, 2.0f)));
            ON_CALL(*m_spawner, GetUseGroundPlane).WillByDefault(::testing::Return(false));
            ON_CALL(*m_spawner, GetPriority)
                .WillByDefault([](uint32_t& layer, int32_t& priority) { layer = 0; priority = 0; });
            const auto sample = [](AZ::Vector3& position, bool& terrainExists)
            {
                terrainExists = !(position.GetX() >= 0.25f && position.GetX() <= 0.75f &&
                    position.GetY() >= 0.25f && position.GetY() <= 0.75f);
                position.SetZ(0.0f);
            };
            ON_CALL(*m_heights, GetHeight)
                .WillByDefault([sample](const AZ::Vector3& input, AZ::Vector3& output, bool& terrainExists)
                {
                    output = input;
                    sample(output, terrainExists);
                });
            ON_CALL(*m_heights, GetHeights)
                .WillByDefault([sample](AZStd::span<AZ::Vector3> positions, AZStd::span<bool> terrainExists)
                {
                    for (size_t index = 0; index < positions.size(); ++index)
                    {
                        sample(positions[index], terrainExists[index]);
                    }
                });
            const auto sampleSurface = [](const AZ::Vector3& position, AzFramework::SurfaceData::SurfaceTagWeightList& weights)
            {
                weights.clear();
                if (!(position.GetX() >= 0.25f && position.GetX() <= 0.75f &&
                      position.GetY() >= 0.25f && position.GetY() <= 0.75f))
                {
                    weights.push_back({ AZ::Crc32("phase4_surface"), 1.0f });
                }
            };
            ON_CALL(*m_surfaces, GetSurfaceWeights).WillByDefault(sampleSurface);
            ON_CALL(*m_surfaces, GetSurfaceWeightsFromList)
                .WillByDefault([sampleSurface](
                    AZStd::span<const AZ::Vector3> positions,
                    AZStd::span<AzFramework::SurfaceData::SurfaceTagWeightList> weights)
                {
                    for (size_t index = 0; index < positions.size(); ++index)
                    {
                        sampleSurface(positions[index], weights[index]);
                    }
                });

            m_terrain = AZStd::make_unique<TerrainSystem>();
            m_terrain->SetTerrainHeightBounds({ -10.0f, 10.0f });
            m_terrain->SetTerrainHeightQueryResolution(1.0f);
            m_terrain->SetTerrainSurfaceDataQueryResolution(1.0f);
            m_terrain->Activate();
            AZ::TickBus::Broadcast(&AZ::TickEvents::OnTick, 0.0f, AZ::ScriptTimePoint{});
        }

        void TearDown() override
        {
            m_terrain.reset();
            m_surfaces.reset();
            m_heights.reset();
            m_spawner.reset();
            m_shape.reset();
            m_jobs.reset();
        }

        AZ::EntityId m_regionId{ 71'001 };
        AZStd::unique_ptr<JobManagerProvider> m_jobs;
        AZStd::unique_ptr<UnitTest::MockShapeComponentRequests> m_shape;
        AZStd::unique_ptr<UnitTest::MockTerrainSpawnerRequests> m_spawner;
        AZStd::unique_ptr<UnitTest::MockTerrainAreaHeightRequests> m_heights;
        AZStd::unique_ptr<UnitTest::MockTerrainAreaSurfaceRequestBus> m_surfaces;
        AZStd::unique_ptr<TerrainSystem> m_terrain;
    };

    TEST_F(TerrainPublicExistenceTests, ScalarBatchAndTerrainRaycastAgreeOnSubGridHole)
    {
        bool exists = true;
        EXPECT_FLOAT_EQ(m_terrain->GetHeight(
            AZ::Vector3(0.5f, 0.5f, 1.0f), AzFramework::Terrain::TerrainDataRequests::Sampler::EXACT, &exists), -10.0f);
        EXPECT_FALSE(exists);

        const AZStd::array<AZ::Vector3, 2> positions{
            AZ::Vector3(0.5f, 0.5f, 1.0f), AZ::Vector3(1.5f, 0.5f, 1.0f)
        };
        AZStd::array<bool, 2> batchExists{ true, false };
        AZStd::array<size_t, 2> batchSurfaceCounts{};
        size_t resultIndex = 0;
        m_terrain->QueryList(
            positions,
            static_cast<AzFramework::Terrain::TerrainDataRequests::TerrainDataMask>(
                AzFramework::Terrain::TerrainDataRequests::TerrainDataMask::Heights |
                AzFramework::Terrain::TerrainDataRequests::TerrainDataMask::SurfaceData),
            [&batchExists, &batchSurfaceCounts, &resultIndex](
                const AzFramework::SurfaceData::SurfacePoint& point, bool terrainExists)
            {
                batchExists[resultIndex] = terrainExists;
                batchSurfaceCounts[resultIndex] = point.m_surfaceTags.size();
                ++resultIndex;
            },
            AzFramework::Terrain::TerrainDataRequests::Sampler::EXACT);
        EXPECT_EQ(resultIndex, positions.size());
        EXPECT_FALSE(batchExists[0]);
        EXPECT_TRUE(batchExists[1]);
        EXPECT_EQ(batchSurfaceCounts[0], 0);
        EXPECT_EQ(batchSurfaceCounts[1], 1);

        AzFramework::SurfaceData::SurfacePoint holePoint;
        exists = true;
        m_terrain->GetSurfacePoint(
            positions[0], holePoint, AzFramework::Terrain::TerrainDataRequests::Sampler::EXACT, &exists);
        EXPECT_FALSE(exists);
        EXPECT_TRUE(holePoint.m_surfaceTags.empty());
        AzFramework::SurfaceData::SurfaceTagWeightList weights;
        m_terrain->GetSurfaceWeights(
            positions[0], weights, AzFramework::Terrain::TerrainDataRequests::Sampler::EXACT, &exists);
        EXPECT_FALSE(exists);
        EXPECT_TRUE(weights.empty());
        m_terrain->GetSurfaceWeights(
            positions[1], weights, AzFramework::Terrain::TerrainDataRequests::Sampler::EXACT, &exists);
        EXPECT_TRUE(exists);
        EXPECT_EQ(weights.size(), 1);

        AzFramework::RenderGeometry::RayRequest ray;
        ray.m_startWorldPosition = AZ::Vector3(0.5f, 0.5f, 1.0f);
        ray.m_endWorldPosition = AZ::Vector3(0.5f, 0.5f, -1.0f);
        EXPECT_FALSE(m_terrain->GetClosestIntersection(ray));
        ray.m_startWorldPosition.SetX(1.5f);
        ray.m_endWorldPosition.SetX(1.5f);
        const auto hit = m_terrain->GetClosestIntersection(ray);
        ASSERT_TRUE(hit);
        EXPECT_NEAR(hit.m_worldPosition.GetZ(), 0.0f, 0.0001f);

        // A grid-boundary hit must remain valid.
        ray.m_startWorldPosition = AZ::Vector3(1.0f, 0.5f, 1.0f);
        ray.m_endWorldPosition = AZ::Vector3(1.0f, 0.5f, -1.0f);
        EXPECT_TRUE(m_terrain->GetClosestIntersection(ray));
    }

    TEST_F(TerrainPublicExistenceTests, ObliqueRayRejectsGapAndContinuesToLaterExistingTerrain)
    {
        AzFramework::RenderGeometry::RayRequest ray;
        ray.m_startWorldPosition = AZ::Vector3(0.4f, 0.4f, 1.0f);
        ray.m_endWorldPosition = AZ::Vector3(0.6f, 0.6f, -1.0f);
        EXPECT_FALSE(m_terrain->GetClosestIntersection(ray));

        const auto varyingSample = [](const AZ::Vector3& input, AZ::Vector3& output, bool& terrainExists)
        {
            output = input;
            output.SetZ(-std::cos(AZ::Constants::Pi * input.GetX()));
            terrainExists = !(input.GetX() >= 0.25f && input.GetX() <= 0.75f &&
                input.GetY() >= 0.25f && input.GetY() <= 0.75f);
        };
        ON_CALL(*m_heights, GetHeight).WillByDefault(varyingSample);
        ON_CALL(*m_heights, GetHeights)
            .WillByDefault([varyingSample](AZStd::span<AZ::Vector3> positions, AZStd::span<bool> terrainExists)
            {
                for (size_t index = 0; index < positions.size(); ++index)
                {
                    const AZ::Vector3 input = positions[index];
                    varyingSample(input, positions[index], terrainExists[index]);
                }
            });

        // This shallow descending ray intersects the alternating terrain
        // profile three times. The first front-facing hit is inside the
        // opening, so traversal must continue until a later existing candidate.
        ray.m_startWorldPosition = AZ::Vector3(0.0f, 0.5f, 0.2f);
        ray.m_endWorldPosition = AZ::Vector3(3.0f, 0.5f, -0.1f);
        const auto hit = m_terrain->GetClosestIntersection(ray);
        ASSERT_TRUE(hit);
        EXPECT_GT(hit.m_worldPosition.GetX(), 0.75f);
        EXPECT_NEAR(hit.m_worldPosition.GetZ(), 0.2f - 0.1f * hit.m_worldPosition.GetX(), 0.0001f);
        bool exists = false;
        m_terrain->GetHeight(
            hit.m_worldPosition, AzFramework::Terrain::TerrainDataRequests::Sampler::EXACT, &exists);
        EXPECT_TRUE(exists);
    }

    class TerrainPhysicsColliderGapTests : public ::testing::Test
    {
    protected:
        void CheckPreparedCellBecomesHeightfieldHole()
        {
            AZ::Entity entity;
            auto* collider = entity.CreateComponent<TerrainPhysicsColliderComponent>();
            entity.Init();
            ::testing::NiceMock<UnitTest::MockShapeComponentRequests> shape(entity.GetId());
            ON_CALL(shape, GetEncompassingAabb)
                .WillByDefault(::testing::Return(
                    AZ::Aabb::CreateFromMinMaxValues(0.0f, 0.0f, -10.0f, 4.0f, 4.0f, 10.0f)));
            ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrainData;
            ON_CALL(terrainData, GetTerrainHeightQueryResolution).WillByDefault(::testing::Return(2.0f));
            ON_CALL(terrainData, QueryRegionAsync)
                .WillByDefault([](
                    const AzFramework::Terrain::TerrainQueryRegion& region,
                    AzFramework::Terrain::TerrainDataRequests::TerrainDataMask,
                    AzFramework::Terrain::SurfacePointRegionFillCallback callback,
                    AzFramework::Terrain::TerrainDataRequests::Sampler,
                    AZStd::shared_ptr<AzFramework::Terrain::QueryAsyncParams>)
                {
                    AzFramework::SurfaceData::SurfacePoint point;
                    for (size_t y = 0; y < region.m_numPointsY; ++y)
                    {
                        for (size_t x = 0; x < region.m_numPointsX; ++x)
                        {
                            point.m_position = region.m_startPoint + AZ::Vector3(
                                float(x) * region.m_stepSize.GetX(), float(y) * region.m_stepSize.GetY(), 10.0f);
                            callback(x, y, point, true);
                        }
                    }
                    return AZStd::shared_ptr<AzFramework::Terrain::TerrainJobContext>{};
                });

            auto cells = std::make_shared<TerrainCompositor::PreparedTerrainHeightfieldCellMask>();
            cells->m_gridSpacing = 2.0f;
            cells->m_cells = { { 0, 0 } };
            TerrainCompositor::PreparedTerrainMeshHeightGap collisionOnly;
            collisionOnly.m_affectTerrainRendering = false;
            collisionOnly.m_affectTerrainCollisionQueries = true;
            collisionOnly.m_collisionCells = cells;
            auto snapshot = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderSnapshot>();
            snapshot->m_revision = 17;
            snapshot->m_meshHeightGaps = { collisionOnly };
            collider->m_gapChannel = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderChannel>();
            collider->m_gapChannel->m_snapshot.store(snapshot, std::memory_order_release);
            collider->m_terrainDataActive = true;
            collider->m_heightfieldRegion = AzFramework::Terrain::TerrainQueryRegion(
                AZ::Vector2::CreateZero(), 3, 3, AZ::Vector2(2.0f));

            AZStd::array<Physics::HeightMaterialPoint, 9> samples;
            bool complete = false;
            collider->UpdateHeightsAndMaterialsAsync(
                [&samples](size_t column, size_t row, const Physics::HeightMaterialPoint& point)
                {
                    samples[row * 3 + column] = point;
                },
                [&complete]() { complete = true; }, 0, 0, 3, 3);
            EXPECT_TRUE(complete);
            EXPECT_EQ(samples[0].m_quadMeshType, Physics::QuadMeshType::Hole);
            EXPECT_EQ(samples[1].m_quadMeshType, Physics::QuadMeshType::SubdivideUpperLeftToBottomRight);
            EXPECT_EQ(samples[3].m_quadMeshType, Physics::QuadMeshType::SubdivideUpperLeftToBottomRight);
        }
    };

    TEST_F(TerrainPhysicsColliderGapTests, PreparedCellBecomesHeightfieldHole)
    {
        CheckPreparedCellBecomesHeightfieldHole();
    }
} // namespace Terrain

namespace TerrainCompositor
{
    class TerrainRenderGeometryBatchTests : public ::testing::Test
    {
    protected:
        static void CheckImmediateSourceInvalidation()
        {
            TerrainCompositionGradientComponent component;
            component.m_address.second = AZ::EntityId(99001);
            component.m_configuration.m_proceduralSourceEntityId = AZ::EntityId(99002);
            component.ConnectDependencies();
            auto state = std::make_shared<TerrainCompositionGradientComponent::QueryState>();
            state->m_preparationDependency = component.m_sourceChanges->m_preparationDependency;
            const auto query = TerrainCompositionGradientComponent::CreateRenderGeometryQuery(state);
            const auto dependency = query.m_preparationDependency;
            ASSERT_NE(dependency, nullptr);
            const auto before = dependency->Capture();
            LmbrCentral::DependencyNotificationBus::Event(AZ::EntityId(99002),
                &LmbrCentral::DependencyNotifications::OnCompositionChanged);
            EXPECT_GT(dependency->Capture(), before); // No tick or terrain refresh was needed.
            EXPECT_TRUE(component.m_sourceChanges->m_wholeRegion);
            component.m_sourceChanges.reset();
            TerrainPreparationAdmission admission({ { dependency, dependency->Capture() } });
            EXPECT_FALSE(admission.IsValid());
        }
        static TerrainRenderGeometryQuery MakeQuery(
            AZ::EntityId source,
            AZStd::vector<PreparedTerrainExistenceContributor> existence = {},
            AZStd::vector<PreparedHeightContributor> heights = {},
            double minZ = -100.0)
        {
            auto state = std::make_shared<TerrainCompositionGradientComponent::QueryState>();
            state->m_ownerEntityId = AZ::EntityId(98'001);
            state->m_regionEntityId = AZ::EntityId(98'002);
            state->m_sourceEntityId = source;
            state->m_regionBounds =
                AZ::Aabb::CreateFromMinMax(AZ::Vector3(-10000.0f, -10000.0f, -100.0f), AZ::Vector3(10000.0f, 10000.0f, 100.0f));
            state->m_regionMapping = { minZ, 200.0 };
            state->m_heightContributors = AZStd::move(heights);
            state->m_existenceContributors = AZStd::move(existence);
            return TerrainCompositionGradientComponent::CreateRenderGeometryQuery(state);
        }
    };

    TEST_F(TerrainRenderGeometryBatchTests, SourceMailboxInvalidatesRetainedPreparationBeforeDeferredRefresh)
    {
        CheckImmediateSourceInvalidation();
    }

    namespace
    {
        class CountingRenderSource final
            : public GradientSignal::GradientRequestBus::Handler
            , public TerrainExistenceSourceRequestBus::Handler
        {
        public:
            explicit CountingRenderSource(AZ::EntityId id)
            {
                GradientSignal::GradientRequestBus::Handler::BusConnect(id);
                TerrainExistenceSourceRequestBus::Handler::BusConnect(id);
            }
            ~CountingRenderSource() override
            {
                TerrainExistenceSourceRequestBus::Handler::BusDisconnect();
                GradientSignal::GradientRequestBus::Handler::BusDisconnect();
            }
            static float Height(const AZ::Vector3& p)
            {
                return 0.5f + p.GetX() * 0.01f;
            }
            static bool Exists(const AZ::Vector3& p)
            {
                return p.GetZ() < 0.0f;
            }
            float GetValue(const GradientSignal::GradientSampleParams& p) const override
            {
                ++m_scalarHeights;
                return Height(p.m_position);
            }
            void GetValues(AZStd::span<const AZ::Vector3> positions, AZStd::span<float> out) const override
            {
                ++m_batchHeights;
                for (size_t i = 0; i < positions.size(); ++i)
                    out[i] = Height(positions[i]);
                if (m_onHeights) m_onHeights();
            }
            bool GetTerrainExists(const AZ::Vector3& p) const override
            {
                ++m_scalarExists;
                return Exists(p);
            }
            void GetTerrainExistsFromList(AZStd::span<const AZ::Vector3> positions, AZStd::span<bool> out) const override
            {
                ++m_batchExists;
                for (size_t i = 0; i < positions.size(); ++i)
                    out[i] = Exists(positions[i]);
            }
            bool IsEntityInHierarchy(const AZ::EntityId&) const override
            {
                return m_cyclic;
            }
            bool m_cyclic = false;
            AZStd::function<void()> m_onHeights;
            mutable size_t m_scalarHeights = 0, m_scalarExists = 0, m_batchHeights = 0, m_batchExists = 0;
        };
        const AZStd::vector<AZ::Vector3> CubePositions{ { -1.0f, -1.0f, -1.0f }, { 1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, -1.0f },
                                                        { -1.0f, 1.0f, -1.0f },  { -1.0f, -1.0f, 1.0f }, { 1.0f, -1.0f, 1.0f },
                                                        { 1.0f, 1.0f, 1.0f },    { -1.0f, 1.0f, 1.0f } };
        const AZStd::vector<AZ::u32> CubeIndices{ 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                                                  3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5 };

        TerrainMeshCutoutDataPtr MakeCube()
        {
            auto data = AZStd::make_shared<TerrainMeshCutoutData>();
            EXPECT_EQ(BuildTerrainMeshCutoutData(CubePositions, CubeIndices, *data), TerrainMeshCutoutValidation::Valid);
            return data;
        }

        PreparedTerrainMeshCutout MakePrepared(
            TerrainExistenceOperation operation = TerrainExistenceOperation::RemoveTerrain,
            float margin = 0.0f,
            const AZ::Transform& transform = AZ::Transform::CreateIdentity())
        {
            PreparedTerrainMeshCutout result;
            result.m_data = MakeCube();
            result.m_worldFromLocal = transform;
            result.m_localFromWorld = transform.GetInverse();
            result.m_renderWorldBounds = result.m_data->m_localBounds.GetTransformedAabb(transform);
            result.m_renderWorldBounds.Expand(AZ::Vector3(margin));
            result.m_collisionWorldBounds = result.m_renderWorldBounds;
            result.m_renderLocalMargin = margin / transform.GetUniformScale();
            result.m_collisionLocalMargin = result.m_renderLocalMargin;
            result.m_operation = operation;
            result.m_stableOrderKey = "uuid:00000000-0000-0000-0000-000000000001";
            return result;
        }

        PreparedTerrainExistenceStamp MakeActiveImageMask(TerrainExistenceOperation operation)
        {
            return TestSupport::MakePreparedMask(1.0f, operation, 2.0f);
        }

        PreparedTerrainMeshHeightGap MakePreparedGap()
        {
            const AZStd::vector<AZ::Vector3> positions{ { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f },
                                                        { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, { 2.0f, 1.0f, 0.0f } };
            const AZStd::vector<AZ::u32> indices{ 0, 1, 4, 0, 4, 3 };
            auto data = AZStd::make_shared<TerrainMeshHeightData>();
            EXPECT_EQ(BuildTerrainMeshHeightData(positions, indices, *data), TerrainMeshHeightValidation::Valid);
            data->m_revision = 91;
            TerrainMeshHeightStampRegistrationData registration;
            registration.m_stampEntityId = AZ::EntityId(9001);
            registration.m_configuration.m_orderingId = AZ::Uuid::CreateRandom();
            registration.m_configuration.m_strength = 0.0f;
            registration.m_configuration.m_uncoveredAreaPolicy = TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain;
            registration.m_mesh.m_status = TerrainMeshHeightDataStatus::Ready;
            registration.m_mesh.m_revision = data->m_revision;
            registration.m_mesh.m_data = data;
            PreparedTerrainMeshHeightStamp prepared;
            EXPECT_EQ(PrepareTerrainMeshHeightStamp(registration, false, prepared), TerrainMeshHeightStampPlacementValidation::Valid);
            PreparedTerrainMeshHeightGap gap;
            EXPECT_TRUE(PrepareTerrainMeshHeightGap(
                prepared,
                1.0f,
                AZ::Aabb::CreateFromMinMaxValues(-10.0f, -10.0f, -10.0f, 10.0f, 10.0f, 10.0f),
                gap));
            return gap;
        }
    } // namespace

    TEST(TerrainMeshCutoutDataTests, ClosedCubeBuildsDeterministicBvh)
    {
        auto first = MakeCube();
        auto second = MakeCube();
        ASSERT_FALSE(first->m_bvh.empty());
        ASSERT_EQ(first->m_triangles.size(), 12);
        ASSERT_EQ(first->m_vertices.size(), 8);
        ASSERT_EQ(first->m_indices.size(), 36);
        ASSERT_EQ(first->m_bvh.size(), second->m_bvh.size());
        for (size_t index = 0; index < first->m_bvh.size(); ++index)
        {
            EXPECT_EQ(first->m_bvh[index].m_escapeIndex, second->m_bvh[index].m_escapeIndex);
            EXPECT_EQ(first->m_bvh[index].m_firstTriangle, second->m_bvh[index].m_firstTriangle);
            EXPECT_EQ(first->m_bvh[index].m_triangleCount, second->m_bvh[index].m_triangleCount);
        }
    }

    TEST(TerrainMeshCutoutDataTests, GloballyReversedInputIsNormalizedForGpuWinding)
    {
        auto reversed = CubeIndices;
        for (size_t index = 0; index < reversed.size(); index += 3)
        {
            AZStd::swap(reversed[index + 1], reversed[index + 2]);
        }
        TerrainMeshCutoutData data;
        ASSERT_EQ(BuildTerrainMeshCutoutData(CubePositions, reversed, data), TerrainMeshCutoutValidation::Valid);
        ASSERT_EQ(data.m_indices.size(), CubeIndices.size());
        double signedVolumeTimesSix = 0.0;
        for (size_t index = 0; index < data.m_indices.size(); index += 3)
        {
            const auto& a = data.m_vertices[data.m_indices[index]];
            const auto& b = data.m_vertices[data.m_indices[index + 1]];
            const auto& c = data.m_vertices[data.m_indices[index + 2]];
            signedVolumeTimesSix += double(a.Dot(b.Cross(c)));
        }
        EXPECT_GT(signedVolumeTimesSix, 0.0);
    }

    TEST(TerrainMeshCutoutDataTests, RejectsOpenDegenerateInconsistentAndSelfIntersectingGeometry)
    {
        TerrainMeshCutoutData data;
        auto open = CubeIndices;
        open.resize(open.size() - 6);
        EXPECT_EQ(BuildTerrainMeshCutoutData(CubePositions, open, data), TerrainMeshCutoutValidation::OpenOrNonManifold);

        auto degenerate = CubeIndices;
        degenerate[2] = degenerate[1];
        EXPECT_EQ(BuildTerrainMeshCutoutData(CubePositions, degenerate, data), TerrainMeshCutoutValidation::DegenerateTriangle);

        auto inconsistent = CubeIndices;
        AZStd::swap(inconsistent[1], inconsistent[2]);
        EXPECT_EQ(BuildTerrainMeshCutoutData(CubePositions, inconsistent, data), TerrainMeshCutoutValidation::InconsistentWinding);

        auto intersectingPositions = CubePositions;
        for (const auto& point : CubePositions)
        {
            intersectingPositions.push_back(point + AZ::Vector3(0.5f, 0.5f, 0.5f));
        }
        auto intersectingIndices = CubeIndices;
        for (const AZ::u32 index : CubeIndices)
            intersectingIndices.push_back(index + 8);
        EXPECT_EQ(
            BuildTerrainMeshCutoutData(intersectingPositions, intersectingIndices, data), TerrainMeshCutoutValidation::SelfIntersection);
    }

    TEST(TerrainMeshCutoutSamplingTests, InsideBoundaryOutsideMarginAndFinalHeightAreExact)
    {
        const auto cube = MakePrepared();
        EXPECT_TRUE(SampleTerrainMeshCutout(AZ::Vector3::CreateZero(), cube, TerrainMeshCutoutConsumer::CollisionQueries));
        EXPECT_TRUE(SampleTerrainMeshCutout(AZ::Vector3(1.0f, 0.0f, 0.0f), cube, TerrainMeshCutoutConsumer::CollisionQueries));
        EXPECT_FALSE(SampleTerrainMeshCutout(AZ::Vector3(1.01f, 0.0f, 0.0f), cube, TerrainMeshCutoutConsumer::CollisionQueries));
        EXPECT_TRUE(SampleTerrainMeshCutout(AZ::Vector3(0.0f, 0.0f, 0.9f), cube, TerrainMeshCutoutConsumer::CollisionQueries));
        EXPECT_FALSE(SampleTerrainMeshCutout(AZ::Vector3(0.0f, 0.0f, 1.1f), cube, TerrainMeshCutoutConsumer::CollisionQueries));

        const auto dilated = MakePrepared(TerrainExistenceOperation::RemoveTerrain, 0.2f);
        EXPECT_TRUE(SampleTerrainMeshCutout(AZ::Vector3(1.1f, 0.0f, 0.0f), dilated, TerrainMeshCutoutConsumer::CollisionQueries));
        EXPECT_FALSE(SampleTerrainMeshCutout(AZ::Vector3(1.3f, 0.0f, 0.0f), dilated, TerrainMeshCutoutConsumer::CollisionQueries));
    }

    TEST(TerrainMeshCutoutSamplingTests, ArbitraryRotationTranslationAndUniformScaleUseWorldSurfacePoint)
    {
        AZ::Transform transform = AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateRotationX(AZ::Constants::QuarterPi), AZ::Vector3(5.0f, -3.0f, 2.0f));
        transform.MultiplyByUniformScale(2.0f);
        const auto cutout = MakePrepared(TerrainExistenceOperation::RemoveTerrain, 0.0f, transform);
        EXPECT_TRUE(SampleTerrainMeshCutout(
            transform.TransformPoint(AZ::Vector3(0.2f, -0.4f, 0.5f)), cutout, TerrainMeshCutoutConsumer::CollisionQueries));
        EXPECT_FALSE(SampleTerrainMeshCutout(
            transform.TransformPoint(AZ::Vector3(1.2f, 0.0f, 0.0f)), cutout, TerrainMeshCutoutConsumer::CollisionQueries));
    }

    TEST(TerrainMeshCutoutSamplingTests, ComponentPlacementUsesWorldTransformAndScale)
    {
        TerrainMeshCutoutConfig configuration;
        const AZ::EntityId componentEntity(1001);
        configuration.m_orderingId = AZ::Uuid::CreateRandom();
        configuration.m_renderMargin = 0.0f;
        configuration.m_collisionMargin = 0.0f;

        TerrainMeshCutoutRegistrationData registration;
        registration.m_cutoutEntityId = componentEntity;
        registration.m_configuration = configuration;
        registration.m_mesh.m_status = TerrainMeshCutoutDataStatus::Ready;
        registration.m_mesh.m_data = MakeCube();
        registration.m_transformAvailable = true;
        registration.m_worldTransform = AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateRotationZ(AZ::DegToRad(55.0f)), AZ::Vector3(80.7f, -9.7f, 5.1f));
        registration.m_worldTransform.MultiplyByUniformScale(100.0f);

        PreparedTerrainMeshCutout prepared;
        ASSERT_EQ(PrepareTerrainMeshCutout(registration, false, prepared), TerrainMeshCutoutPlacementValidation::Valid);
        EXPECT_TRUE(SampleTerrainMeshCutout(
            registration.m_worldTransform.TransformPoint(AZ::Vector3(0.25f, -0.5f, 0.75f)),
            prepared,
            TerrainMeshCutoutConsumer::CollisionQueries));
        EXPECT_FALSE(SampleTerrainMeshCutout(
            registration.m_worldTransform.TransformPoint(AZ::Vector3(1.2f, 0.0f, 0.0f)),
            prepared,
            TerrainMeshCutoutConsumer::CollisionQueries));
    }

    TEST(TerrainMeshCutoutSamplingTests, RenderingAndCollisionUseIndependentFlagsAndMargins)
    {
        TerrainMeshCutoutRegistrationData registration;
        registration.m_cutoutEntityId = AZ::EntityId(1001);
        registration.m_configuration.m_orderingId = AZ::Uuid::CreateRandom();
        registration.m_configuration.m_affectTerrainRendering = false;
        registration.m_configuration.m_affectTerrainCollisionQueries = true;
        registration.m_configuration.m_renderMargin = 0.0f;
        registration.m_configuration.m_collisionMargin = 0.3f;
        registration.m_mesh.m_status = TerrainMeshCutoutDataStatus::Ready;
        registration.m_mesh.m_data = MakeCube();
        registration.m_transformAvailable = true;

        PreparedTerrainMeshCutout prepared;
        ASSERT_EQ(PrepareTerrainMeshCutout(registration, false, prepared), TerrainMeshCutoutPlacementValidation::Valid);
        const AZ::Vector3 point(1.2f, 0.0f, 0.0f);
        EXPECT_FALSE(SampleTerrainMeshCutout(point, prepared, TerrainMeshCutoutConsumer::Rendering));
        EXPECT_TRUE(SampleTerrainMeshCutout(point, prepared, TerrainMeshCutoutConsumer::CollisionQueries));
    }

    TEST(TerrainMeshCutoutSamplingTests, HeightfieldCellPaddingIsConservativeAndCollisionOnly)
    {
        auto prepared = MakePrepared();
        ApplyTerrainMeshCutoutCollisionCellPadding(prepared, 1.0f);
        const AZ::Vector3 nearCellCorner(2.3f, 0.0f, 0.0f);
        EXPECT_TRUE(SampleTerrainMeshCutout(nearCellCorner, prepared, TerrainMeshCutoutConsumer::CollisionQueries));
        EXPECT_FALSE(SampleTerrainMeshCutout(nearCellCorner, prepared, TerrainMeshCutoutConsumer::Rendering));
    }

    TEST(TerrainMeshCutoutSamplingTests, UnifiedOrderingLetsImageMasksAndMeshesOverrideEachOther)
    {
        PreparedTerrainExistenceContributor image;
        image.m_type = PreparedTerrainExistenceContributor::Type::ImageMask;
        image.m_imageMask = MakeActiveImageMask(TerrainExistenceOperation::RestoreTerrain);
        PreparedTerrainExistenceContributor mesh;
        mesh.m_type = PreparedTerrainExistenceContributor::Type::MeshCutout;
        mesh.m_meshCutout = MakePrepared(TerrainExistenceOperation::RemoveTerrain);

        const AZ::Vector3 finalSurfacePoint(0.0f, 0.0f, 0.25f);
        const PreparedTerrainExistenceContributor meshLast[]{ image, mesh };
        EXPECT_FALSE(ComposeTerrainExists(finalSurfacePoint, true, meshLast));
        const PreparedTerrainExistenceContributor imageLast[]{ mesh, image };
        EXPECT_TRUE(ComposeTerrainExists(finalSurfacePoint, true, imageLast));

        EXPECT_TRUE(ComposeTerrainExists(AZ::Vector3(0.0f, 0.0f, 2.0f), true, meshLast));
    }

    TEST(TerrainMeshCutoutSamplingTests, RenderGeometryIgnoresMeshCutoutsButPreservesImageMasks)
    {
        PreparedTerrainExistenceContributor image;
        image.m_type = PreparedTerrainExistenceContributor::Type::ImageMask;
        image.m_imageMask = MakeActiveImageMask(TerrainExistenceOperation::RemoveTerrain);
        PreparedTerrainExistenceContributor mesh;
        mesh.m_type = PreparedTerrainExistenceContributor::Type::MeshCutout;
        mesh.m_meshCutout = MakePrepared(TerrainExistenceOperation::RemoveTerrain);

        const AZ::Vector3 surfacePoint(0.0f, 0.0f, 0.25f);
        const PreparedTerrainExistenceContributor meshOnly[]{ mesh };
        EXPECT_TRUE(ComposeTerrainRenderGeometryExists(surfacePoint, true, meshOnly));

        const PreparedTerrainExistenceContributor imageAndMesh[]{ image, mesh };
        EXPECT_FALSE(ComposeTerrainRenderGeometryExists(surfacePoint, true, imageAndMesh));
    }

    TEST(TerrainMeshCutoutSamplingTests, MeshHeightGapsAreARemoveOnlyUnionAppliedAfterOrderedRestore)
    {
        PreparedTerrainExistenceContributor imageRestore;
        imageRestore.m_type = PreparedTerrainExistenceContributor::Type::ImageMask;
        imageRestore.m_imageMask = MakeActiveImageMask(TerrainExistenceOperation::RestoreTerrain);
        PreparedTerrainExistenceContributor closedRestore;
        closedRestore.m_type = PreparedTerrainExistenceContributor::Type::MeshCutout;
        closedRestore.m_meshCutout = MakePrepared(TerrainExistenceOperation::RestoreTerrain);
        PreparedTerrainExistenceContributor gap;
        gap.m_type = PreparedTerrainExistenceContributor::Type::MeshHeightGap;
        gap.m_meshHeightGap = MakePreparedGap();

        const AZ::Vector3 removedPoint(1.5f, 0.5f, 0.25f);
        const PreparedTerrainExistenceContributor imageThenGap[]{ imageRestore, gap };
        EXPECT_FALSE(ComposeTerrainExists(removedPoint, false, imageThenGap));
        const PreparedTerrainExistenceContributor gapThenClosedRestore[]{ gap, closedRestore };
        EXPECT_FALSE(ComposeTerrainExists(removedPoint, false, gapThenClosedRestore));

        const PreparedTerrainExistenceContributor twoGaps[]{ gap, gap };
        EXPECT_FALSE(ComposeTerrainExists(removedPoint, true, twoGaps));
        const PreparedTerrainExistenceContributor oneGap[]{ gap };
        EXPECT_FALSE(ComposeTerrainExists(removedPoint, true, oneGap));
        EXPECT_TRUE(ComposeTerrainExists(AZ::Vector3(0.5f, 0.5f, 0.25f), true, oneGap));
        EXPECT_TRUE(ComposeTerrainRenderGeometryExists(removedPoint, true, oneGap));
    }

    TEST(TerrainMeshCutoutRenderRegistryTests, CorrelatesGapOwnershipAndRejectsStaleCompositionGenerations)
    {
        if (AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get())
        {
            GTEST_SKIP() << "The application already owns the render registry.";
        }
        TerrainMeshCutoutRenderRegistry registry;
        const void* scene = reinterpret_cast<const void*>(uintptr_t(3));
        const auto channel = registry.AcquireSceneChannel(scene);
        const AZ::Uuid session = AZ::Uuid::CreateRandom();
        const auto gap = MakePreparedGap();
        ASSERT_TRUE(registry.Publish(scene, session, {}, {}, 5, { gap }));
        const auto generationFive = channel->m_snapshot.load(std::memory_order_acquire);
        ASSERT_EQ(generationFive->m_meshHeightGaps.size(), 1);
        EXPECT_EQ(generationFive->m_meshHeightGaps[0].m_data, gap.m_data);
        ASSERT_EQ(generationFive->m_compositionGenerations.size(), 1);
        EXPECT_EQ(generationFive->m_compositionGenerations[0].first, session);
        EXPECT_EQ(generationFive->m_compositionGenerations[0].second, 5);

        EXPECT_FALSE(registry.Publish(scene, session, {}, {}, 4, {}));
        EXPECT_EQ(channel->m_snapshot.load(std::memory_order_acquire), generationFive);
        EXPECT_TRUE(registry.Publish(scene, session, {}, {}, 5, {}));
        EXPECT_EQ(channel->m_snapshot.load(std::memory_order_acquire), generationFive);
    }

    TEST(TerrainMeshCutoutRenderRegistryTests, CollisionCellsRespectAdmissionAndCollisionOnlyGaps)
    {
        auto coupled = MakePreparedGap();
        TerrainMeshCutoutRenderSnapshot snapshot;
        snapshot.m_revision = 11;
        snapshot.m_meshHeightGaps = { coupled };
        auto admitted = std::make_shared<TerrainMeshHeightGapActivation>();
        admitted->m_revision = snapshot.m_revision;
        admitted->m_gaps = { coupled };
        EXPECT_TRUE(IsTerrainHeightfieldCellRemoved(snapshot, admitted, AZ::Vector2(1.0f, 0.0f), AZ::Vector2(1.0f)));

        auto stale = std::make_shared<TerrainMeshHeightGapActivation>(*admitted);
        stale->m_revision = snapshot.m_revision - 1;
        EXPECT_FALSE(IsTerrainHeightfieldCellRemoved(snapshot, stale, AZ::Vector2(1.0f, 0.0f), AZ::Vector2(1.0f)));

        auto collisionOnly = coupled;
        collisionOnly.m_affectTerrainRendering = false;
        snapshot.m_meshHeightGaps = { collisionOnly };
        EXPECT_TRUE(IsTerrainHeightfieldCellRemoved(snapshot, {}, AZ::Vector2(1.0f, 0.0f), AZ::Vector2(1.0f)));
        EXPECT_FALSE(IsTerrainHeightfieldCellRemoved(snapshot, {}, AZ::Vector2(0.0f, 0.0f), AZ::Vector2(1.0f)));

        auto overlapping = collisionOnly;
        overlapping.m_entityId = AZ::EntityId(9002);
        snapshot.m_meshHeightGaps = { collisionOnly, overlapping };
        EXPECT_TRUE(IsTerrainHeightfieldCellRemoved(snapshot, {}, AZ::Vector2(1.0f, 0.0f), AZ::Vector2(1.0f)));
        snapshot.m_meshHeightGaps.erase(snapshot.m_meshHeightGaps.begin());
        EXPECT_TRUE(IsTerrainHeightfieldCellRemoved(snapshot, {}, AZ::Vector2(1.0f, 0.0f), AZ::Vector2(1.0f)));
        snapshot.m_meshHeightGaps.clear();
        EXPECT_FALSE(IsTerrainHeightfieldCellRemoved(snapshot, {}, AZ::Vector2(1.0f, 0.0f), AZ::Vector2(1.0f)));
    }

    TEST(TerrainMeshCutoutRenderRegistryTests, RenderTopologyQueryUsesTerrainRegionXYWhenCollisionHoleHasOutOfRangeHeight)
    {
        TerrainMeshCutoutRenderSnapshot snapshot;
        TerrainRenderGeometryQuery query;
        query.m_regionBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-10.0f, -10.0f, -1.0f), AZ::Vector3(10.0f, 10.0f, 1.0f));
        query.m_getTerrainExists = [](const AZ::Vector3& position)
        {
            return position.GetX() >= 0.0f;
        };
        snapshot.m_renderGeometryQueries.push_back(AZStd::move(query));

        bool renderTerrainExists = false;
        EXPECT_TRUE(TryGetTerrainRenderGeometryExists(snapshot, AZ::Vector3(2.0f, 3.0f, 1000.0f), renderTerrainExists));
        EXPECT_TRUE(renderTerrainExists);

        renderTerrainExists = false;
        EXPECT_FALSE(TryGetTerrainRenderGeometryExists(snapshot, AZ::Vector3(20.0f, 3.0f, 0.0f), renderTerrainExists));
        EXPECT_FALSE(renderTerrainExists);
    }

    TEST(TerrainMeshCutoutRenderRegistryTests, PublishesImmutableScenePartitionedSnapshots)
    {
        if (AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get())
        {
            GTEST_SKIP() << "The application already owns the render registry.";
        }
        TerrainMeshCutoutRenderRegistry registry;
        const void* sceneA = reinterpret_cast<const void*>(uintptr_t(1));
        const void* sceneB = reinterpret_cast<const void*>(uintptr_t(2));
        const auto channelA = registry.AcquireSceneChannel(sceneA);
        const auto channelB = registry.AcquireSceneChannel(sceneB);
        const auto originalA = channelA->m_snapshot.load(std::memory_order_acquire);

        auto later = MakePrepared();
        later.m_priority = 20;
        later.m_stableOrderKey = "uuid:00000000-0000-0000-0000-000000000020";
        auto earlier = MakePrepared();
        earlier.m_priority = 10;
        earlier.m_stableOrderKey = "uuid:00000000-0000-0000-0000-000000000010";
        registry.Publish(sceneA, AZ::Uuid::CreateRandom(), { later, earlier });

        const auto publishedA = channelA->m_snapshot.load(std::memory_order_acquire);
        const auto publishedB = channelB->m_snapshot.load(std::memory_order_acquire);
        ASSERT_NE(originalA, publishedA);
        ASSERT_EQ(publishedA->m_cutouts.size(), 2);
        EXPECT_EQ(publishedA->m_cutouts[0].m_priority, 10);
        EXPECT_TRUE(publishedB->m_cutouts.empty());
        EXPECT_TRUE(originalA->m_cutouts.empty());
    }

    TEST_F(TerrainRenderGeometryBatchTests, MatchesScalarComposedHeightAndImageHolesWithoutCollisionCuts)
    {
        const AZ::EntityId sourceId(98'003);
        CountingRenderSource source(sourceId);
        PreparedTerrainExistenceContributor image, cutout, gap;
        image.m_type = PreparedTerrainExistenceContributor::Type::ImageMask;
        image.m_imageMask = MakeActiveImageMask(TerrainExistenceOperation::RemoveTerrain);
        cutout.m_type = PreparedTerrainExistenceContributor::Type::MeshCutout;
        cutout.m_meshCutout = MakePrepared();
        gap.m_type = PreparedTerrainExistenceContributor::Type::MeshHeightGap;
        gap.m_meshHeightGap = MakePreparedGap();
        PreparedHeightContributor height;
        height.m_image.m_placement = image.m_imageMask.m_placement;
        height.m_image.m_image = image.m_imageMask.m_mask;
        height.m_image.m_heightOrigin = 30.0;
        height.m_image.m_heightRange = 10.0;
        height.m_image.m_strength = 1.0;
        height.m_image.m_relativeEdgeBlend = false;
        TerrainMeshCutoutRenderSnapshot snapshot;
        snapshot.m_renderGeometryQueries.push_back(MakeQuery(sourceId, { image, cutout, gap }, { height }));
        AZ::Vector3 points[] = { { 0, 0, -1000 }, { 1.5f, 0.5f, -1000 }, { 3, 0, -1000 }, { 3, 0, 1000 } };
        float heights[4]{};
        bool exists[4]{};
        ApplyTerrainRenderGeometry(snapshot, points, heights, exists);
        EXPECT_EQ(source.m_batchHeights, 1);
        EXPECT_EQ(source.m_batchExists, 1);
        EXPECT_EQ(source.m_scalarHeights, 0);
        EXPECT_EQ(source.m_scalarExists, 0);
        EXPECT_FLOAT_EQ(heights[0], 40.0f);
        EXPECT_FALSE(exists[0]);
        EXPECT_TRUE(exists[2]);
        EXPECT_FALSE(exists[3]); // The batch must preserve input surface Z.
        for (size_t i = 0; i < 4; ++i)
        {
            EXPECT_FLOAT_EQ(heights[i], snapshot.m_renderGeometryQueries[0].m_getHeight(points[i]));
            EXPECT_EQ(exists[i], snapshot.m_renderGeometryQueries[0].m_getTerrainExists(points[i]));
        }
        snapshot.m_renderGeometryQueries[0] = MakeQuery(sourceId, { cutout, gap });
        ApplyTerrainRenderGeometry(snapshot, points, heights, exists);
        EXPECT_TRUE(exists[0]);
        EXPECT_TRUE(exists[1]); // A mesh-height gap cannot become a coarse topology hole.
    }

    TEST_F(TerrainRenderGeometryBatchTests, MissingAndCyclicSourcesPreserveScalarFallbacks)
    {
        const AZ::EntityId sourceId(98'004);
        auto query = MakeQuery(sourceId);
        AZ::Vector3 points[] = { { 0, 0, -1000 } };
        float heights[1]{};
        bool exists[1]{};
        query.m_getGeometry(points, heights, exists);
        EXPECT_FLOAT_EQ(heights[0], -100.0f);
        EXPECT_TRUE(exists[0]);
        CountingRenderSource source(sourceId);
        source.m_cyclic = true;
        query.m_getGeometry(points, heights, exists);
        EXPECT_FLOAT_EQ(heights[0], query.m_getHeight(points[0]));
        EXPECT_EQ(exists[0], query.m_getTerrainExists(points[0]));
        EXPECT_EQ(source.m_batchHeights, 0);
        EXPECT_EQ(source.m_batchExists, 0);
    }

    TEST_F(TerrainRenderGeometryBatchTests, RoutingPreservesOverlapsBoundariesIndependentOwnersAndFallback)
    {
        const AZ::EntityId sourceId(98'005);
        CountingRenderSource source(sourceId);
        TerrainMeshCutoutRenderSnapshot snapshot;
        auto left = MakeQuery(sourceId);
        left.m_regionBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-2, -1, -1), AZ::Vector3(1, 1, 1));
        auto right = MakeQuery(sourceId, {}, {}, 200.0);
        right.m_regionBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(0, -1, -1), AZ::Vector3(3, 1, 1));
        auto heightOnly = left;
        heightOnly.m_getTerrainExists = {};
        heightOnly.m_getHeight = [](const AZ::Vector3&)
        {
            return 42.0f;
        };
        snapshot.m_renderGeometryQueries = { left, right };
        AZ::Vector3 points[] = { { -3, 0, -1000 }, { -2, 0, -1000 }, { 0, 0, -1000 }, { 1, 0, -1000 },
                                 { 2, 0, 1000 },   { 3, 0, -1000 },  { 4, 0, -1000 } };
        for (int pass = 0; pass < 3; ++pass)
        {
            float heights[7] = { 17, 17, 17, 17, 17, 17, 17 };
            bool exists[7]{};
            ApplyTerrainRenderGeometry(snapshot, points, heights, exists);
            for (size_t i = 0; i < 7; ++i)
            {
                float expectedHeight = 17;
                bool expectedExists = false;
                TryGetTerrainRenderGeometryHeight(snapshot, points[i], expectedHeight);
                TryGetTerrainRenderGeometryExists(snapshot, points[i], expectedExists);
                EXPECT_FLOAT_EQ(heights[i], expectedHeight);
                EXPECT_EQ(exists[i], expectedExists);
            }
            if (pass == 0)
                snapshot.m_renderGeometryQueries.insert(snapshot.m_renderGeometryQueries.begin(), heightOnly);
            else
                snapshot.m_renderGeometryQueries.clear();
        }
        ApplyTerrainRenderGeometry(snapshot, {}, {}, {});
    }

    TEST_F(TerrainRenderGeometryBatchTests, RetainedBatchSurvivesReplacementAndRemoval)
    {
        ASSERT_EQ(AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get(), nullptr);
        TerrainMeshCutoutRenderRegistry registry;
        const void* scene = reinterpret_cast<const void*>(uintptr_t(71));
        const auto session = AZ::Uuid::CreateRandom();
        const auto channel = registry.AcquireSceneChannel(scene);
        ASSERT_TRUE(registry.Publish(scene, session, {}, MakeQuery(AZ::EntityId(0), {}, {}, -100.0), 1));
        const auto retained = channel->m_snapshot.load();
        ASSERT_TRUE(registry.Publish(scene, session, {}, MakeQuery(AZ::EntityId(0), {}, {}, 200.0), 2));
        registry.Remove(session);
        AZ::Vector3 points[] = { { 0, 0, -1000 } };
        float heights[1]{};
        bool exists[1]{};
        ApplyTerrainRenderGeometry(*retained, points, heights, exists);
        EXPECT_FLOAT_EQ(heights[0], -100.0f);
        EXPECT_TRUE(exists[0]);
        EXPECT_TRUE(channel->m_snapshot.load()->m_renderGeometryQueries.empty());
    }

    TEST_F(TerrainRenderGeometryBatchTests, OwnershipPlanMatchesLegacyOverlayAtBatchAndInclusiveRegionBoundaries)
    {
        for (size_t count : { 0, 1, 255, 256, 257 })
        {
            for (int routing = 0; routing < 5; ++routing)
            {
                SCOPED_TRACE(::testing::Message() << count << " samples, routing " << routing);
                auto publication = std::make_shared<TerrainMeshCutoutRenderSnapshot>();
                AZStd::vector<int> calls;
                const auto makeOwner = [&calls](int id, float minX, float maxX)
                {
                    TerrainRenderGeometryQuery query;
                    query.m_regionBounds = AZ::Aabb::CreateFromMinMax({ minX, -1, -1 }, { maxX, 1, 1 });
                    query.m_getHeight = [&calls, id](const AZ::Vector3& p) { calls.push_back(id * 10 + 1); return p.GetX() + id; };
                    query.m_getTerrainExists = [&calls, id](const AZ::Vector3& p) { calls.push_back(id * 10 + 2); return p.GetZ() < 0; };
                    query.m_getGeometry = [&calls, id](auto points, auto heights, auto exists)
                    {
                        calls.push_back(id * 1000 + static_cast<int>(points.size()));
                        for (size_t i = 0; i < points.size(); ++i) { heights[i] = points[i].GetX() + id; exists[i] = points[i].GetZ() < 0; }
                    };
                    return query;
                };
                publication->m_renderGeometryQueries = { makeOwner(1, -2, 1), makeOwner(2, 0, 3) };
                if (routing == 1) publication->m_renderGeometryQueries[0].m_getGeometry = {}; // Scalar-only owner.
                if (routing == 2) publication->m_renderGeometryQueries[0].m_getTerrainExists = {}; // Split and partially owned.
                if (routing == 3) publication->m_renderGeometryQueries.clear();
                AZStd::vector<AZ::Vector3> points(count);
                AZStd::vector<float> expected(count, 17), actual(count, 17);
                auto expectedExists = std::make_unique<bool[]>(count), actualExists = std::make_unique<bool[]>(count);
                for (size_t i = 0; i < count; ++i)
                    points[i] = AZ::Vector3(float(int(i % 8) - 3), i % 2 ? 1.0f : -1.0f, i % 3 ? -1000.0f : 1000.0f);
                if (routing == 4)
                    for (size_t i = 0; i < count; ++i)
                    {
                        TryGetTerrainRenderGeometryExists(*publication, points[i], expectedExists[i]);
                        TryGetTerrainRenderGeometryHeight(*publication, points[i], expected[i]);
                    }
                else ApplyTerrainRenderGeometry(*publication, points, expected, { expectedExists.get(), count });
                const auto expectedCalls = calls;
                calls.clear();
                TerrainRenderQueryStatistics statistics;
                TerrainRenderQueryRequest request;
                request.m_positions = points;
                request.m_allowBatch = routing != 4;
                const auto plan = ResolveTerrainRenderQuery(publication, request, &statistics);
                EXPECT_TRUE(calls.empty()); // Resolution must never probe live sources.
                EXPECT_EQ(plan.m_publication, publication);
                EXPECT_TRUE(plan.RequiresOrdinaryResults());
                ExecuteTerrainRenderQuery(plan, actual, { actualExists.get(), count }, &statistics);
                EXPECT_EQ(actual, expected);
                EXPECT_EQ(calls, expectedCalls);
                for (size_t i = 0; i < count; ++i) EXPECT_EQ(actualExists[i], expectedExists[i]);
                EXPECT_EQ(statistics.m_independentSamples, 0);
                EXPECT_EQ(statistics.m_fallbackSamples[static_cast<size_t>(TerrainRenderFallback::PreservedPolicy)], count);
            }
        }
    }

    TEST_F(TerrainRenderGeometryBatchTests, CompositionPlanDeclaresLiveOrdinaryZDependenciesAndCountsActualSourceCalls)
    {
        const AZ::EntityId sourceId(98'006);
        CountingRenderSource source(sourceId);
        for (size_t count : { 0, 1, 255, 256, 257 })
        {
            auto publication = std::make_shared<TerrainMeshCutoutRenderSnapshot>();
            publication->m_renderGeometryQueries.push_back(MakeQuery(sourceId));
            AZStd::vector<AZ::Vector3> points(count, AZ::Vector3(0, 0, -1000));
            AZStd::vector<float> heights(count);
            auto exists = std::make_unique<bool[]>(count);
            TerrainRenderQueryRequest request;
            request.m_positions = points;
            TerrainRenderQueryStatistics statistics;
            const auto plan = ResolveTerrainRenderQuery(publication, request, &statistics);
            ExecuteTerrainRenderQuery(plan, heights, { exists.get(), count }, &statistics);
            EXPECT_EQ(statistics.m_heightOwned, count);
            EXPECT_EQ(statistics.m_existenceOwned, count);
            EXPECT_EQ(statistics.m_batchSamples, count);
            EXPECT_EQ(statistics.m_batchCallbacks, count ? 1 : 0);
            EXPECT_EQ(statistics.m_heightSourceCalls, count ? 1 : 0);
            EXPECT_EQ(statistics.m_existenceSourceCalls, count ? 1 : 0);
            EXPECT_EQ(statistics.m_hierarchySourceCalls, count ? 2 : 0);
            EXPECT_EQ(statistics.m_independentSamples, 0);
            for (auto reason : { TerrainRenderFallback::LiveSource, TerrainRenderFallback::InputZ, TerrainRenderFallback::OrdinaryDependency })
                EXPECT_EQ(statistics.m_fallbackSamples[static_cast<size_t>(reason)], count);
            for (size_t i = 0; i < count; ++i) { EXPECT_FLOAT_EQ(heights[i], 0); EXPECT_TRUE(exists[i]); }
        }
    }

    TEST_F(TerrainRenderGeometryBatchTests, UnsupportedCapabilitiesKeepOrdinaryOverlayAndMismatchDoesNotCallSources)
    {
        const AZ::EntityId sourceId(98'007);
        CountingRenderSource source(sourceId);
        auto publication = std::make_shared<TerrainMeshCutoutRenderSnapshot>();
        publication->m_renderGeometryQueries.push_back(MakeQuery(sourceId));
        AZ::Vector3 points[] = { { 0, 0, 1000 }, { 0, 0, -1000 } };
        float heights[] = { 17, 17 };
        bool exists[] = { true, false };
        for (int unsupported = 0; unsupported < 5; ++unsupported)
        {
            auto& capability = publication->m_renderGeometryQueries[0].m_capability;
            capability.m_maxSamples = unsupported == 0 ? 1 : 2;
            TerrainRenderQueryRequest request;
            request.m_positions = points;
            if (unsupported == 1) request.m_coordinates = TerrainRenderCoordinates::Unknown;
            if (unsupported == 2) request.m_sampler = static_cast<AzFramework::Terrain::TerrainDataRequests::Sampler>(99);
            if (unsupported == 3) request.m_grid = TerrainRenderGrid::Regular; // Missing dimensions/spacing.
            if (unsupported == 4)
            {
                request.m_grid = static_cast<TerrainRenderGrid>(99);
                request.m_gridWidth = 2; request.m_gridHeight = 1; request.m_gridSpacing = AZ::Vector2(1.0f);
            }
            const auto plan = ResolveTerrainRenderQuery(publication, request);
            EXPECT_NE(plan.m_firstRun.m_fallbackReasons & TerrainRenderFallbackBit(TerrainRenderFallback::UnsupportedRequest), 0);
            ExecuteTerrainRenderQuery(plan, heights, exists);
            EXPECT_FALSE(exists[0]); EXPECT_TRUE(exists[1]);
        }
        TerrainRenderQueryRequest request;
        request.m_positions = points;
        const auto plan = ResolveTerrainRenderQuery(publication, request);
        const auto calls = source.m_batchHeights;
        AZ_TEST_START_TRACE_SUPPRESSION;
        ExecuteTerrainRenderQuery(plan, AZStd::span<float>(heights, 1), exists);
        ExecuteTerrainRenderQuery(plan, heights, AZStd::span<bool>(exists, 1));
        AZ_TEST_STOP_TRACE_SUPPRESSION(2);
        EXPECT_EQ(source.m_batchHeights, calls);
    }

    TEST_F(TerrainRenderGeometryBatchTests, IndependenceRequiresEveryExplicitGuaranteeEvenForOneBulkOwner)
    {
        auto publication = std::make_shared<TerrainMeshCutoutRenderSnapshot>();
        auto query = MakeQuery(AZ::EntityId(0));
        auto& capability = query.m_capability;
        capability.m_height = capability.m_existence = { TerrainRenderSource::RetainedAvailable, TerrainRenderInputZ::Independent, false };
        publication->m_renderGeometryQueries.push_back(query);
        AZ::Vector3 point(0, 0, -1000);
        TerrainRenderQueryRequest request;
        request.m_positions = { &point, 1 };
        for (auto source : { TerrainRenderSource::Unknown, TerrainRenderSource::Live, TerrainRenderSource::Unavailable, TerrainRenderSource::RetainedAvailable })
        {
            publication->m_renderGeometryQueries[0].m_capability.m_existence.m_source = source;
            TerrainRenderQueryStatistics statistics;
            const auto plan = ResolveTerrainRenderQuery(publication, request, &statistics);
            EXPECT_TRUE(plan.RequiresOrdinaryResults()); // No query elimination in this architectural slice.
            EXPECT_TRUE(plan.m_firstRun.m_batch);
            EXPECT_EQ(statistics.m_independentSamples, source == TerrainRenderSource::RetainedAvailable ? 1 : 0);
        }
        publication->m_renderGeometryQueries[0].m_capability.m_declared = false;
        const auto plan = ResolveTerrainRenderQuery(publication, request);
        EXPECT_NE(plan.m_firstRun.m_fallbackReasons & TerrainRenderFallbackBit(TerrainRenderFallback::LegacyContract), 0);
    }

    TEST_F(TerrainRenderGeometryBatchTests, RetainedPlanSurvivesReentrantPublicationReplacementAndRemoval)
    {
        TerrainMeshCutoutRenderRegistry registry;
        const void* scene = reinterpret_cast<const void*>(uintptr_t(72));
        const auto session = AZ::Uuid::CreateRandom();
        const auto channel = registry.AcquireSceneChannel(scene);
        auto query = MakeQuery(AZ::EntityId(0), {}, {}, -100.0);
        const auto original = query.m_getGeometry;
        query.m_getGeometry = [&](auto positions, auto heights, auto exists)
        {
            registry.Publish(scene, session, {}, MakeQuery(AZ::EntityId(0), {}, {}, 200.0), 2);
            registry.Remove(session);
            original(positions, heights, exists);
        };
        registry.Publish(scene, session, {}, query, 1);
        AZ::Vector3 point(0, 0, -1000);
        TerrainRenderQueryRequest request;
        request.m_positions = { &point, 1 };
        const auto plan = ResolveTerrainRenderQuery(channel->m_snapshot.load(), request);
        EXPECT_EQ(plan.m_firstRun.m_height->m_compositionSession, session);
        EXPECT_EQ(plan.m_firstRun.m_existence->m_compositionRevision, 1);
        float height = 17; bool exists = false;
        ExecuteTerrainRenderQuery(plan, { &height, 1 }, { &exists, 1 });
        EXPECT_FLOAT_EQ(height, -100);
        EXPECT_TRUE(exists);
        EXPECT_TRUE(channel->m_snapshot.load()->m_renderGeometryQueries.empty());
        EXPECT_EQ(plan.m_publication->m_renderGeometryQueries.size(), 1);
    }

    TEST_F(TerrainRenderGeometryBatchTests, LiveSourceDisappearanceAndReentrancyPreserveFallbackAndDiagnosticScope)
    {
        const AZ::EntityId sourceId(98'008);
        CountingRenderSource source(sourceId);
        auto publication = std::make_shared<TerrainMeshCutoutRenderSnapshot>();
        publication->m_renderGeometryQueries.push_back(MakeQuery(sourceId));
        AZ::Vector3 point(0, 0, -1000);
        TerrainRenderQueryRequest request;
        request.m_positions = { &point, 1 };
        const auto plan = ResolveTerrainRenderQuery(publication, request);
        float height = 17; bool exists = false;
        TerrainRenderQueryStatistics outer, nested;
        AZStd::vector<float> nestedHeights;
        source.m_onHeights = [&]
        {
            float nestedHeight = 17; bool nestedExists = false;
            ExecuteTerrainRenderQuery(plan, { &nestedHeight, 1 }, { &nestedExists, 1 }, &nested);
            nestedHeights.push_back(nestedHeight);
            source.TerrainExistenceSourceRequestBus::Handler::BusDisconnect();
        };
        ExecuteTerrainRenderQuery(plan, { &height, 1 }, { &exists, 1 }, &outer);
        EXPECT_FLOAT_EQ(height, 0); EXPECT_TRUE(exists);
        EXPECT_EQ(outer.m_heightSourceCalls, 1);
        EXPECT_EQ(outer.m_existenceSourceCalls, 0); // Source disappeared after the height callback.
        // EBus marks use reentrant after the second dispatch enters the bus.
        // The deepest query fails closed for height; its caller keeps its sampled height.
        EXPECT_EQ(nestedHeights, (AZStd::vector<float>{ -100.0f, 0.0f }));
        EXPECT_EQ(nested.m_heightSourceCalls, 1);
        EXPECT_EQ(nested.m_heightSourceFallbackSamples, 1);
        source.GradientSignal::GradientRequestBus::Handler::BusDisconnect();
        outer = {};
        ExecuteTerrainRenderQuery(plan, { &height, 1 }, { &exists, 1 }, &outer);
        EXPECT_FLOAT_EQ(height, -100); EXPECT_TRUE(exists);
        EXPECT_EQ(outer.m_heightSourceCalls, 0);
        EXPECT_EQ(outer.m_existenceSourceCalls, 0);
    }

    TEST_F(TerrainRenderGeometryBatchTests, DISABLED_ProfileOwnershipPlanAgainstLegacyOverlay)
    {
        AZStd::unique_ptr<AZ::ComponentDescriptor> descriptor(ProceduralGroundGradientComponent::CreateDescriptor());
        AZ::Entity entity("Ownership plan benchmark");
        entity.CreateComponent<ProceduralGroundGradientComponent>();
        entity.Init(); entity.Activate();
        auto publication = std::make_shared<TerrainMeshCutoutRenderSnapshot>();
        publication->m_renderGeometryQueries.push_back(MakeQuery(entity.GetId()));
        AZStd::vector<AZ::Vector3> points;
        for (int size : { 131, 67 })
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    points.emplace_back(float(x - 1) * (size == 131 ? 0.5f : 1.0f), float(y - 1) * (size == 131 ? 0.5f : 1.0f), -1000.0f);
        AZStd::vector<float> heights(points.size());
        auto exists = std::make_unique<bool[]>(points.size());
        for (int sectors : { 10, 20, 30 })
        {
            AZStd::vector<double> legacyTimes, planTimes;
            for (int iteration = 0; iteration < 11; ++iteration)
                for (int phase = 0; phase < 2; ++phase)
                {
                    const bool planned = (iteration + phase) % 2 == 0;
                    auto start = std::chrono::steady_clock::now();
                    for (int sector = 0; sector < sectors; ++sector)
                        for (auto range : { AZStd::pair<size_t, size_t>{ 0, 131 * 131 }, { 131 * 131, 67 * 67 } })
                        {
                            TerrainRenderQueryRequest request;
                            request.m_positions = AZStd::span<const AZ::Vector3>(points).subspan(range.first, range.second);
                            auto output = AZStd::span<float>(heights).subspan(range.first, range.second);
                            AZStd::span<bool> existence(exists.get() + range.first, range.second);
                            if (planned) ExecuteTerrainRenderQuery(ResolveTerrainRenderQuery(publication, request), output, existence);
                            else ApplyTerrainRenderGeometry(*publication, request.m_positions, output, existence);
                        }
                    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                    if (iteration > 1) (planned ? planTimes : legacyTimes).push_back(ms);
                }
            AZStd::sort(legacyTimes.begin(), legacyTimes.end()); AZStd::sort(planTimes.begin(), planTimes.end());
            std::printf("%d sectors (regular+CLOD): legacy %.3f ms; ownership plan %.3f ms; ratio %.3f\n",
                sectors, legacyTimes[4], planTimes[4], planTimes[4] / legacyTimes[4]);
        }
        entity.Deactivate();
    }

    TEST_F(TerrainRenderGeometryBatchTests, DISABLED_ProfileRetainedSectorQueries)
    {
        // Opt-in microbenchmark, not a frame-time assertion. Use the actual
        // procedural component and retained query factory, with regular + CLOD halos.
        AZStd::unique_ptr<AZ::ComponentDescriptor> descriptor(ProceduralGroundGradientComponent::CreateDescriptor());
        AZ::Entity entity("Terrain sampling benchmark");
        entity.CreateComponent<ProceduralGroundGradientComponent>();
        entity.Init();
        entity.Activate();
        TerrainMeshCutoutRenderSnapshot snapshot;
        snapshot.m_renderGeometryQueries.push_back(MakeQuery(entity.GetId()));
        AZStd::vector<AZ::Vector3> positions;
        for (int size : { 131, 67 })
        {
            const float spacing = size == 131 ? 0.5f : 1.0f;
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    positions.emplace_back(float(x - 1) * spacing, float(y - 1) * spacing, -1000.0f);
        }
        AZStd::vector<float> heights(positions.size()), reference(positions.size());
        auto exists = std::make_unique<bool[]>(positions.size());
        AZStd::vector<double> scalarTimes, batchTimes;
        for (int iteration = 0; iteration < 11; ++iteration)
        {
            // Alternate execution order to reduce warm-cache bias.
            for (int phase = 0; phase < 2; ++phase)
            {
                const bool batch = (iteration + phase) % 2 == 0;
                const auto start = std::chrono::steady_clock::now();
                if (batch)
                {
                    constexpr size_t RegularCount = 131 * 131;
                    ApplyTerrainRenderGeometry(
                        snapshot,
                        AZStd::span<const AZ::Vector3>(positions).first(RegularCount),
                        AZStd::span<float>(heights).first(RegularCount),
                        AZStd::span<bool>(exists.get(), RegularCount));
                    ApplyTerrainRenderGeometry(
                        snapshot,
                        AZStd::span<const AZ::Vector3>(positions).subspan(RegularCount),
                        AZStd::span<float>(heights).subspan(RegularCount),
                        AZStd::span<bool>(exists.get() + RegularCount, positions.size() - RegularCount));
                }
                else
                {
                    for (size_t i = 0; i < positions.size(); ++i)
                    {
                        TryGetTerrainRenderGeometryExists(snapshot, positions[i], exists[i]);
                        TryGetTerrainRenderGeometryHeight(snapshot, positions[i], reference[i]);
                    }
                }
                const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                if (iteration > 1)
                    (batch ? batchTimes : scalarTimes).push_back(elapsed);
            }
        }
        EXPECT_EQ(heights, reference);
        AZStd::sort(scalarTimes.begin(), scalarTimes.end());
        AZStd::sort(batchTimes.begin(), batchTimes.end());
        std::printf(
            "%zu samples: scalar median %.3f ms; batch median %.3f ms; %.2fx speedup (CPU retained queries only)\n",
            positions.size(),
            scalarTimes[4],
            batchTimes[4],
            scalarTimes[4] / batchTimes[4]);
        entity.Deactivate();
    }
} // namespace TerrainCompositor
