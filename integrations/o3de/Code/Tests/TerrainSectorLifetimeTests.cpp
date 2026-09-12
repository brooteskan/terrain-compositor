#include <AzTest/AzTest.h>
#include <Atom/RHI/RHISystem.h>
#include <Atom/RPI.Public/Shader/ShaderSystemInterface.h>
#include <TerrainRenderer/TerrainMeshManager.h>
#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>
#include <Tests/Mocks/Terrain/MockTerrainDataRequestBus.h>
#include "TerrainTestFixtures.h"
#include "ProceduralSnapshotTestSupport.h"
#include <thread>

namespace Terrain
{
    class TerrainSectorLifetimeTests : public ::testing::Test
    {
    protected:
        void CheckReorderedIndependentCompletionsCommitOnceIncludingEmpty();
        void CheckSupersededRequestCannotCommitAtSameCoordinate();
        void CheckCoordinateRoundTripAcrossZeroRejectsPreviousLease();
        void CheckRecreatedSlotAtSameCoordinateAndSerialRejectsOldIdentity();
        void CheckOwnedPreparationOutlivesManagerWithClodRayTracingAndRetainedQueries();
        void CheckPopulatedAndEmptyResultsShareAllOrNothingAcceptance();
        void CheckFailedOrCancelledQueriesNeverReachCommit();
        void CheckPublicationReplacementSceneRemovalAndRegistryShutdownRejectRetainedWork();
        void CheckImmediateSourceInvalidationRejectsUnchangedPublication();
        void CheckTerrainSettingsInvalidationAndResetRejectPendingWork();
        void CheckConfigurationChangeAndMalformedDataRejectBeforeCommit();
        void CheckPublicationLockCoversEveryCommitInBatch();
        void CheckClodEmptyQueryFallsBackButIncompleteQueryFails();
        void CheckSourceChangeDuringPreparationRejectsOwnedOutput();
        void CheckSceneReactivationPreservesIndependentlyOwnedRegistrations();
        void CheckProceduralSnapshotMatchesPackedClodHaloAndRtOutput();
        void CheckProceduralSnapshotChangeRemovalAndReconnectionRejectDelayedResults();
        void CheckProceduralSnapshotChangeDuringOrdinarySamplingRejectsResults();
        void CheckProceduralSnapshotPublicationReplacementRejectsResults();
        void CheckAcquisitionCannotRefreshAnOldSourceGenerationTicket();

        using Manager = TerrainMeshManager;
        using Request = Manager::SectorPreparationRequest;
        using Result = Manager::PreparedSectorResult;
        using Status = Manager::SectorPreparationStatus;
        using ResultPtr = std::shared_ptr<Result>;
        using Requests = AzFramework::Terrain::TerrainDataRequests;

        // Explicit completion order, no sleeps and no worker references to the fixture.
        struct Executor
        {
            AZStd::vector<Request> m_queue;
            void Submit(Request request) { m_queue.push_back(AZStd::move(request)); }
            ResultPtr Complete(size_t index)
            {
                return std::make_shared<Result>(Manager::PrepareSector(AZStd::move(m_queue.at(index))));
            }
        };

        void SetUp() override
        {
            AZ::Interface<AZ::RHI::RHISystemInterface>::Register(&m_rhi);
            m_manager = std::make_unique<Manager>();
            Configure();
        }
        void TearDown() override
        {
            m_manager.reset();
            AZ::Interface<AZ::RHI::RHISystemInterface>::Unregister(&m_rhi);
        }
        void Configure()
        {
            m_manager->m_gridSize = 2;
            m_manager->m_gridVerts1D = 3;
            m_manager->m_gridVerts2D = 9;
            m_manager->m_worldHeightBounds = { -10.0f, 10.0f };
            m_manager->m_config.m_clodEnabled = true;
            m_manager->m_vertexOrder = { 8, 7, 6, 5, 4, 3, 2, 1, 0 };
            m_manager->m_xyPositions = { {2,2}, {1,2}, {0,2}, {2,1}, {1,1}, {0,1}, {2,0}, {1,0}, {0,0} };
            m_manager->m_sectorLods.resize(1);
            m_manager->m_sectorLods[0].m_sectors.resize(2);
            m_manager->m_sectorLods[0].m_sectors[0].m_worldCoord = { -1, 0 };
            m_manager->m_sectorLods[0].m_sectors[1].m_worldCoord = { 0, 0 };
        }
        Request Capture(size_t slot = 0)
        {
            return m_manager->CaptureSectorRequest(0, slot, m_manager->CapturePreparationSettings(), m_channel, true,
                m_channel ? m_channel->m_snapshot.load() : nullptr);
        }
        ResultPtr Empty(size_t slot = 0)
        {
            return std::make_shared<Result>(Manager::PrepareSector(Capture(slot)));
        }
        bool Accept(AZStd::vector<ResultPtr> results)
        {
            return m_manager->AcceptPreparedSectors(results, [this](auto& sector, const auto& result)
            {
                // Every production GPU/SRG/AABB mutation lives in this one sink.
                ++m_commits;
                sector.m_aabb = result.m_aabb;
                sector.m_hasData = result.m_hasData;
                m_order.push_back(result.m_request.m_slot);
            });
        }
        void SupplyTerrain(::testing::NiceMock<UnitTest::MockTerrainDataRequests>& terrain)
        {
            ON_CALL(terrain, TerrainAreaExistsInBounds).WillByDefault(::testing::Return(true));
            ON_CALL(terrain, QueryRegion).WillByDefault([](const auto& region, auto, auto callback, auto)
            {
                for (size_t y = 0; y < region.m_numPointsY; ++y)
                    for (size_t x = 0; x < region.m_numPointsX; ++x)
                    {
                        AzFramework::SurfaceData::SurfacePoint surface;
                        surface.m_position = AZ::Vector3(
                            region.m_startPoint.GetX() + x * region.m_stepSize.GetX(),
                            region.m_startPoint.GetY() + y * region.m_stepSize.GetY(), 1.0f);
                        callback(x, y, surface, true);
                    }
            });
        }

        TerrainCompositor::TestSupport::ScopedNameDictionary m_names;
        AZ::RHI::RHISystem m_rhi;
        std::unique_ptr<Manager> m_manager;
        TerrainCompositor::TerrainMeshCutoutRenderChannelPtr m_channel;
        size_t m_commits = 0;
        AZStd::vector<size_t> m_order;
    };

    void TerrainSectorLifetimeTests::CheckReorderedIndependentCompletionsCommitOnceIncludingEmpty()
    {
        Executor executor;
        executor.Submit(Capture(0));
        executor.Submit(Capture(1));
        const auto second = executor.Complete(1);
        const auto first = executor.Complete(0);
        EXPECT_EQ(second->m_status, Status::Empty);
        EXPECT_TRUE(Accept({ second }));
        EXPECT_TRUE(Accept({ first }));
        EXPECT_FALSE(Accept({ first }));
        EXPECT_EQ(m_order, (AZStd::vector<size_t>{ 1, 0 }));
        EXPECT_EQ(m_commits, 2);
    }

    void TerrainSectorLifetimeTests::CheckSupersededRequestCannotCommitAtSameCoordinate()
    {
        Executor executor;
        executor.Submit(Capture());
        executor.Submit(Capture());
        EXPECT_TRUE(Accept({ executor.Complete(1) }));
        EXPECT_FALSE(Accept({ executor.Complete(0) }));
        EXPECT_EQ(m_commits, 1);
    }

    void TerrainSectorLifetimeTests::CheckCoordinateRoundTripAcrossZeroRejectsPreviousLease()
    {
        Executor executor;
        executor.Submit(Capture());
        auto& sector = m_manager->m_sectorLods[0].m_sectors[0];
        sector.m_worldCoord = { 1, 0 };
        executor.Submit(Capture());
        sector.m_worldCoord = { -1, 0 };
        const auto current = Empty();
        EXPECT_FALSE(Accept({ executor.Complete(1) }));
        EXPECT_FALSE(Accept({ executor.Complete(0) }));
        EXPECT_TRUE(Accept({ current }));
        EXPECT_EQ(m_commits, 1);
    }

    void TerrainSectorLifetimeTests::CheckRecreatedSlotAtSameCoordinateAndSerialRejectsOldIdentity()
    {
        const auto old = Empty();
        m_manager->m_sectorLods[0].m_sectors.clear();
        Configure();
        const auto current = Empty();
        ASSERT_EQ(old->m_request.m_serial, current->m_request.m_serial);
        EXPECT_FALSE(Accept({ old }));
        EXPECT_TRUE(Accept({ current }));
    }

    void TerrainSectorLifetimeTests::CheckOwnedPreparationOutlivesManagerWithClodRayTracingAndRetainedQueries()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        m_manager->m_sectorLods[0].m_sectors[0].m_rtData = AZStd::make_unique<Manager::RtSector>();
        m_channel = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderChannel>();
        auto snapshot = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderSnapshot>();
        TerrainCompositor::TerrainRenderGeometryQuery query;
        query.m_regionBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-100), AZ::Vector3(100));
        query.m_getHeight = [](const auto&) { return 3.0f; };
        query.m_getTerrainExists = [](const auto&) { return true; };
        snapshot->m_renderGeometryQueries.push_back(query);
        m_channel->m_snapshot.store(snapshot);
        auto request = Capture();
        std::weak_ptr<const Manager::SectorPreparationSettings> settings = request.m_data.m_settings;
        m_manager->m_worldHeightBounds = { 0.0f, 200.0f };
        m_manager->m_vertexOrder.clear();
        m_manager->m_xyPositions.clear();
        m_manager.reset();
        auto result = std::make_shared<Result>(Manager::PrepareSector(AZStd::move(request)));
        EXPECT_EQ(result->m_status, Status::Ready);
        ASSERT_EQ(result->m_heights.size(), 9);
        EXPECT_EQ(result->m_heights[0].m_height, 42598); // 3 in captured [-10, 10], quantized to even.
        EXPECT_EQ(result->m_lodHeights[0].m_height, result->m_heights[0].m_height);
        EXPECT_FLOAT_EQ(result->m_rtPositions[0].x, 1.0f);
        EXPECT_FLOAT_EQ(result->m_aabb.GetMin().GetX(), -2.0f);
        EXPECT_EQ(result->m_queryStatistics->m_ordinarySamples, 41); // 5x5 + 4x4 halos.
        EXPECT_GT(result->m_preparationMicroseconds, 0.0);
        m_manager = std::make_unique<Manager>();
        Configure();
        EXPECT_FALSE(Accept({ result }));
        EXPECT_EQ(m_commits, 0);
        result.reset();
        EXPECT_TRUE(settings.expired());
    }

    void TerrainSectorLifetimeTests::CheckPopulatedAndEmptyResultsShareAllOrNothingAcceptance()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        const auto populated = Empty(0);
        auto emptyRequest = Capture(1);
        emptyRequest.m_hasTerrain = false;
        const auto empty = std::make_shared<Result>(Manager::PrepareSector(AZStd::move(emptyRequest)));
        ASSERT_EQ(populated->m_status, Status::Ready);
        ASSERT_EQ(empty->m_status, Status::Empty);
        empty->m_request.m_cancelled->store(true);
        EXPECT_FALSE(Accept({ populated, empty }));
        EXPECT_EQ(m_commits, 0);
        auto replacementRequest = Capture(1);
        replacementRequest.m_hasTerrain = false;
        const auto replacement = std::make_shared<Result>(Manager::PrepareSector(AZStd::move(replacementRequest)));
        EXPECT_TRUE(Accept({ populated, replacement }));
        EXPECT_EQ(m_commits, 2);
        EXPECT_FALSE(Accept({ populated, replacement }));
    }

    void TerrainSectorLifetimeTests::CheckFailedOrCancelledQueriesNeverReachCommit()
    {
        auto request = Capture();
        request.m_hasTerrain = true; // Ordinary handler disappears after area admission.
        auto failed = std::make_shared<Result>(Manager::PrepareSector(request));
        EXPECT_EQ(failed->m_status, Status::Failed);
        EXPECT_FALSE(Accept({ failed }));
        request.m_cancelled->store(true);
        auto cancelled = std::make_shared<Result>(Manager::PrepareSector(AZStd::move(request)));
        EXPECT_EQ(cancelled->m_status, Status::Cancelled);
        EXPECT_FALSE(Accept({ cancelled }));
        EXPECT_EQ(m_commits, 0);
    }

    void TerrainSectorLifetimeTests::CheckPublicationReplacementSceneRemovalAndRegistryShutdownRejectRetainedWork()
    {
        auto registry = std::make_unique<TerrainCompositor::TerrainMeshCutoutRenderRegistry>();
        const void* scene = this;
        m_channel = registry->AcquireSceneChannel(scene);
        auto old = Empty();
        registry->Publish(scene, AZ::Uuid::CreateRandom(), {});
        EXPECT_FALSE(Accept({ old }));
        auto removed = Empty();
        registry->RemoveScene(scene);
        EXPECT_FALSE(Accept({ removed }));
        EXPECT_EQ(registry->FindSceneChannel(scene), nullptr);
        const auto oldChannel = m_channel;
        m_channel = registry->AcquireSceneChannel(scene);
        EXPECT_NE(m_channel, oldChannel);
        auto shutdown = Empty();
        registry.reset();
        EXPECT_FALSE(Accept({ shutdown }));
        EXPECT_EQ(m_commits, 0);
    }

    void TerrainSectorLifetimeTests::CheckImmediateSourceInvalidationRejectsUnchangedPublication()
    {
        m_channel = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderChannel>();
        auto snapshot = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderSnapshot>();
        TerrainCompositor::TerrainRenderGeometryQuery query;
        query.m_preparationDependency = std::make_shared<TerrainCompositor::TerrainPreparationDependency>();
        snapshot->m_renderGeometryQueries.push_back(query);
        m_channel->m_snapshot.store(snapshot);
        auto result = Empty();
        query.m_preparationDependency->Invalidate();
        ASSERT_EQ(m_channel->m_snapshot.load(), snapshot);
        EXPECT_FALSE(Accept({ result }));
        result = Empty();
        query.m_preparationDependency->Retire();
        EXPECT_FALSE(Accept({ result }));
        EXPECT_EQ(m_commits, 0);
    }

    void TerrainSectorLifetimeTests::CheckTerrainSettingsInvalidationAndResetRejectPendingWork()
    {
        auto result = Empty();
        // The actual notification path increments this authority even when the
        // refresh will only affect another sector or its CLOD/normal halo.
        m_manager->OnTerrainDataChanged(AZ::Aabb::CreateNull(),
            AzFramework::Terrain::TerrainDataNotifications::TerrainDataChangedMask::Settings);
        EXPECT_FALSE(Accept({ result }));
        Configure();
        result = Empty();
        m_manager->Reset();
        Configure();
        EXPECT_FALSE(Accept({ result }));
        EXPECT_EQ(m_commits, 0);
    }

    void TerrainSectorLifetimeTests::CheckConfigurationChangeAndMalformedDataRejectBeforeCommit()
    {
        struct ShaderOptions final : AZ::RPI::ShaderSystemInterface
        {
            void SetGlobalShaderOption(const AZ::Name&, AZ::RPI::ShaderOptionValue) override {}
            AZ::RPI::ShaderOptionValue GetGlobalShaderOption(const AZ::Name&) override { return {}; }
            const GlobalShaderOptionMap& GetGlobalShaderOptions() const override { return m_options; }
            void Connect(GlobalShaderOptionUpdatedEvent::Handler&) override {}
            void SetSupervariantName(const AZ::Name&) override {}
            const AZ::Name& GetSupervariantName() const override { return m_name; }
            GlobalShaderOptionMap m_options;
            AZ::Name m_name;
        } shaderSystem;
        AZ::Interface<AZ::RPI::ShaderSystemInterface>::Register(&shaderSystem);
        auto result = Empty();
        auto configuration = m_manager->m_config;
        configuration.m_clodDistance += 1.0f; // A configuration change that needs no sector rebuild.
        m_manager->SetConfiguration(configuration);
        AZ::Interface<AZ::RPI::ShaderSystemInterface>::Unregister(&shaderSystem);
        EXPECT_FALSE(Accept({ result }));
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        result = Empty();
        result->m_lodHeights.pop_back();
        EXPECT_FALSE(Accept({ result }));
        EXPECT_EQ(m_commits, 0);
    }

    void TerrainSectorLifetimeTests::CheckPublicationLockCoversEveryCommitInBatch()
    {
        m_channel = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderChannel>();
        AZStd::vector<ResultPtr> results{ Empty(0), Empty(1) };
        size_t committed = 0;
        EXPECT_TRUE(m_manager->AcceptPreparedSectors(results, [&](auto&, const auto&)
        {
            bool couldPublish = true;
            std::thread contender([&]
            {
                couldPublish = m_channel->m_publicationMutex.try_lock();
                if (couldPublish) m_channel->m_publicationMutex.unlock();
            });
            contender.join();
            EXPECT_FALSE(couldPublish);
            ++committed;
        }));
        EXPECT_EQ(committed, 2);
    }

    void TerrainSectorLifetimeTests::CheckClodEmptyQueryFallsBackButIncompleteQueryFails()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        bool incomplete = false;
        ON_CALL(terrain, QueryRegion).WillByDefault([&](const auto& region, auto, auto callback, auto)
        {
            const bool clod = region.m_stepSize.GetX() == 2.0f;
            for (size_t y = 0; y < region.m_numPointsY; ++y)
                for (size_t x = 0; x < region.m_numPointsX; ++x)
                {
                    AzFramework::SurfaceData::SurfacePoint point;
                    point.m_position = AZ::Vector3(0, 0, 1);
                    callback(x, y, point, !clod);
                    if (incomplete && clod) return;
                }
        });
        auto result = Empty();
        ASSERT_EQ(result->m_status, Status::Ready);
        ASSERT_EQ(result->m_lodHeights.size(), result->m_heights.size());
        for (size_t index = 0; index < result->m_heights.size(); ++index)
        {
            EXPECT_EQ(result->m_lodHeights[index].m_height, result->m_heights[index].m_height);
            EXPECT_EQ(result->m_lodHeights[index].m_normal, result->m_heights[index].m_normal);
        }
        EXPECT_TRUE(Accept({ result }));
        incomplete = true;
        result = Empty();
        EXPECT_EQ(result->m_status, Status::Failed);
        EXPECT_FALSE(Accept({ result }));
        EXPECT_EQ(m_commits, 1);
    }

    void TerrainSectorLifetimeTests::CheckSourceChangeDuringPreparationRejectsOwnedOutput()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        m_channel = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderChannel>();
        auto snapshot = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderSnapshot>();
        auto dependency = std::make_shared<TerrainCompositor::TerrainPreparationDependency>();
        TerrainCompositor::TerrainRenderGeometryQuery query;
        query.m_regionBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-100), AZ::Vector3(100));
        query.m_preparationDependency = dependency;
        query.m_getHeight = [dependency](const auto&) { dependency->Invalidate(); return 1.0f; };
        query.m_getTerrainExists = [](const auto&) { return true; };
        snapshot->m_renderGeometryQueries.push_back(query);
        m_channel->m_snapshot.store(snapshot);
        auto result = Empty();
        ASSERT_EQ(result->m_status, Status::Ready);
        EXPECT_FALSE(Accept({ result }));
        EXPECT_EQ(m_commits, 0);
    }

    void TerrainSectorLifetimeTests::CheckSceneReactivationPreservesIndependentlyOwnedRegistrations()
    {
        TerrainCompositor::TerrainMeshCutoutRenderRegistry registry;
        const auto session = AZ::Uuid::CreateRandom();
        TerrainCompositor::TerrainRenderGeometryQuery query;
        query.m_getTerrainExists = [](const auto&) { return true; };
        registry.Publish(this, session, {}, query, 1);
        m_channel = registry.AcquireSceneChannel(this);
        const auto result = Empty();
        registry.RemoveScene(this, false); // Actual feature-processor deactivation policy.
        m_channel = registry.AcquireSceneChannel(this);
        EXPECT_FALSE(Accept({ result }));
        EXPECT_EQ(m_channel->m_snapshot.load()->m_renderGeometryQueries.size(), 1);
        EXPECT_TRUE(Accept({ Empty() }));
        registry.Remove(session);
        EXPECT_TRUE(m_channel->m_snapshot.load()->m_renderGeometryQueries.empty());
    }

    void TerrainSectorLifetimeTests::CheckProceduralSnapshotMatchesPackedClodHaloAndRtOutput()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        TerrainCompositor::ProceduralGroundGradientConfig config;
        config.m_hillDensity = 0.3f;
        config.m_amplitudeMeters = 180.0f; // Includes final renderer clamping in [-10, 10].
        TerrainCompositor::SnapshotTestSupport::Composition scene(config);
        ASSERT_TRUE(scene.AddImageHole());
        m_channel = scene.Channel();
        m_manager->m_sectorLods[0].m_sectors[0].m_rtData = AZStd::make_unique<Manager::RtSector>();
        for (bool batch : { false, true })
            for (auto sampler : { Requests::Sampler::EXACT, Requests::Sampler::CLAMP, Requests::Sampler::BILINEAR })
            {
                auto request = Capture();
                auto settings = std::make_shared<Manager::SectorPreparationSettings>(*request.m_data.m_settings);
                settings->m_batchQueries = batch;
                request.m_data.m_settings = settings;
                request.m_data.m_samplerType = sampler;
                const auto captured = request.m_data.m_renderSources;
                ASSERT_TRUE(captured && captured->m_queries.front().m_proceduralSnapshot);
                auto legacy = std::make_shared<TerrainCompositor::TerrainRenderQuerySources>();
                legacy->m_publication = request.m_data.m_renderSnapshot;
                legacy->m_queries = legacy->m_publication->m_renderGeometryQueries;
                request.m_data.m_renderSources = legacy;
                const auto expected = Manager::PrepareSector(request);
                request.m_data.m_renderSources = captured;
                const auto actual = std::make_shared<Result>(Manager::PrepareSector(request));
                ASSERT_EQ(actual->m_status, Status::Ready);
                EXPECT_EQ(actual->m_status, expected.m_status);
                EXPECT_EQ(actual->m_aabb, expected.m_aabb);
                EXPECT_EQ(actual->m_hasData, expected.m_hasData);
                ASSERT_EQ(actual->m_heights.size(), expected.m_heights.size());
                ASSERT_EQ(actual->m_lodHeights.size(), expected.m_lodHeights.size());
                for (size_t i = 0; i < actual->m_heights.size(); ++i)
                {
                    EXPECT_EQ(actual->m_heights[i].m_height, expected.m_heights[i].m_height);
                    EXPECT_EQ(actual->m_heights[i].m_normal, expected.m_heights[i].m_normal);
                    EXPECT_EQ(actual->m_lodHeights[i].m_height, expected.m_lodHeights[i].m_height);
                    EXPECT_EQ(actual->m_lodHeights[i].m_normal, expected.m_lodHeights[i].m_normal);
                    EXPECT_FLOAT_EQ(actual->m_rtPositions[i].x, expected.m_rtPositions[i].x);
                    EXPECT_FLOAT_EQ(actual->m_rtPositions[i].y, expected.m_rtPositions[i].y);
                    EXPECT_FLOAT_EQ(actual->m_rtPositions[i].z, expected.m_rtPositions[i].z);
                    EXPECT_FLOAT_EQ(actual->m_rtNormals[i].x, expected.m_rtNormals[i].x);
                    EXPECT_FLOAT_EQ(actual->m_rtNormals[i].y, expected.m_rtNormals[i].y);
                    EXPECT_FLOAT_EQ(actual->m_rtNormals[i].z, expected.m_rtNormals[i].z);
                }
                EXPECT_EQ(actual->m_queryStatistics->m_ordinarySamples, 41);
                EXPECT_EQ(actual->m_queryStatistics->m_heightSnapshotSamples, 41);
                EXPECT_EQ(actual->m_queryStatistics->m_existenceSnapshotSamples, 41);
                EXPECT_EQ(actual->m_queryStatistics->m_sources.size(), 1); // Captured once across both halos.
                EXPECT_TRUE(Accept({ actual }));
            }
    }

    void TerrainSectorLifetimeTests::CheckProceduralSnapshotChangeRemovalAndReconnectionRejectDelayedResults()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        TerrainCompositor::SnapshotTestSupport::Composition scene;
        m_channel = scene.Channel();
        auto request = Capture();
        const auto publication = request.m_data.m_renderSnapshot;
        const auto snapshot = request.m_data.m_renderSources->m_queries.front().m_proceduralSnapshot;
        const auto before = Manager::PrepareSector(request);
        scene.m_config.m_amplitudeMeters = 0.0f;
        scene.m_source->ReadInConfig(&scene.m_config);
        EXPECT_EQ(scene.Publication(), publication); // No deferred composition republish is needed for rejection.
        auto after = std::make_shared<Result>(Manager::PrepareSector(request));
        ASSERT_EQ(after->m_heights.size(), before.m_heights.size());
        for (size_t i = 0; i < after->m_heights.size(); ++i) EXPECT_EQ(after->m_heights[i].m_height, before.m_heights[i].m_height);
        EXPECT_FALSE(Accept({ after }));
        request = Capture();
        scene.StopSource();
        scene.StartSource();
        EXPECT_FALSE(Accept({ std::make_shared<Result>(Manager::PrepareSector(request)) }));
        auto destroyed = Capture();
        scene.m_source.reset(); // Destruction also retires the provider without an explicit Deactivate.
        auto oldOutput = std::make_shared<Result>(Manager::PrepareSector(destroyed));
        EXPECT_EQ(oldOutput->m_status, Status::Ready);
        EXPECT_FALSE(Accept({ oldOutput }));
        scene.StartSource();
        EXPECT_TRUE(Accept({ Empty() }));
        EXPECT_EQ(m_commits, 1);
        EXPECT_FALSE(TerrainCompositor::SnapshotTestSupport::Current(snapshot));
    }

    void TerrainSectorLifetimeTests::CheckProceduralSnapshotChangeDuringOrdinarySamplingRejectsResults()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        TerrainCompositor::SnapshotTestSupport::Composition scene;
        m_channel = scene.Channel();
        auto request = Capture();
        bool changed = false;
        ON_CALL(terrain, QueryRegion).WillByDefault([&](const auto& region, auto, auto callback, auto)
        {
            if (!changed)
            {
                changed = true;
                scene.m_config.m_amplitudeMeters = 0.0f;
                scene.m_source->ReadInConfig(&scene.m_config);
            }
            for (size_t y = 0; y < region.m_numPointsY; ++y)
                for (size_t x = 0; x < region.m_numPointsX; ++x)
                {
                    AzFramework::SurfaceData::SurfacePoint point;
                    point.m_position = AZ::Vector3(region.m_startPoint.GetX() + x * region.m_stepSize.GetX(),
                        region.m_startPoint.GetY() + y * region.m_stepSize.GetY(), -10);
                    callback(x, y, point, false); // Ordinary collision holes must not disable the overlay.
                }
        });
        auto result = std::make_shared<Result>(Manager::PrepareSector(request));
        EXPECT_EQ(result->m_status, Status::Ready);
        EXPECT_EQ(result->m_queryStatistics->m_heightSnapshotSamples, 41);
        EXPECT_FALSE(Accept({ result }));
        EXPECT_TRUE(Accept({ Empty() }));
    }

    void TerrainSectorLifetimeTests::CheckProceduralSnapshotPublicationReplacementRejectsResults()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        TerrainCompositor::SnapshotTestSupport::Composition scene;
        m_channel = scene.Channel();
        const auto pending = Empty();
        ASSERT_TRUE(scene.AddImageHole());
        EXPECT_FALSE(Accept({ pending }));
        EXPECT_TRUE(Accept({ Empty() }));
        EXPECT_EQ(m_commits, 1);
    }

    void TerrainSectorLifetimeTests::CheckAcquisitionCannotRefreshAnOldSourceGenerationTicket()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        TerrainCompositor::SnapshotTestSupport::Composition scene;
        m_channel = scene.Channel();
        const auto sources = scene.Capture();
        scene.m_config.m_amplitudeMeters = 0.0f;
        scene.m_source->ReadInConfig(&scene.m_config);
        // Simulate a provider returning a previously retained value snapshot to a
        // NEW request. Manager/composition tickets are current; the source's ticket
        // must remain the old generation captured with those values.
        auto publication = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderSnapshot>(*scene.Publication());
        publication->m_renderGeometryQueries.front().m_acquireSources = [sources]
        {
            return std::make_shared<const TerrainCompositor::TerrainRenderGeometryQuery>(sources->m_queries.front());
        };
        m_channel->m_snapshot.store(publication);
        const auto result = Empty();
        EXPECT_EQ(result->m_status, Status::Ready);
        EXPECT_FALSE(Accept({ result }));
        // Even the empty-area fast path carries and validates the same source ticket.
        ON_CALL(terrain, TerrainAreaExistsInBounds).WillByDefault(::testing::Return(false));
        const auto empty = Empty();
        EXPECT_EQ(empty->m_status, Status::Empty);
        EXPECT_FALSE(Accept({ empty }));
        EXPECT_EQ(m_commits, 0);
    }

    TEST_F(TerrainSectorLifetimeTests, ProceduralSnapshotMatchesPackedClodHaloAndRtOutput)
    { CheckProceduralSnapshotMatchesPackedClodHaloAndRtOutput(); }
    TEST_F(TerrainSectorLifetimeTests, ProceduralSnapshotChangeRemovalAndReconnectionRejectDelayedResults)
    { CheckProceduralSnapshotChangeRemovalAndReconnectionRejectDelayedResults(); }
    TEST_F(TerrainSectorLifetimeTests, ProceduralSnapshotChangeDuringOrdinarySamplingRejectsResults)
    { CheckProceduralSnapshotChangeDuringOrdinarySamplingRejectsResults(); }
    TEST_F(TerrainSectorLifetimeTests, ProceduralSnapshotPublicationReplacementRejectsResults)
    { CheckProceduralSnapshotPublicationReplacementRejectsResults(); }
    TEST_F(TerrainSectorLifetimeTests, AcquisitionCannotRefreshAnOldSourceGenerationTicket)
    { CheckAcquisitionCannotRefreshAnOldSourceGenerationTicket(); }

    TEST_F(TerrainSectorLifetimeTests, SceneReactivationPreservesIndependentlyOwnedRegistrations)
    { CheckSceneReactivationPreservesIndependentlyOwnedRegistrations(); }

    TEST_F(TerrainSectorLifetimeTests, ClodEmptyQueryFallsBackButIncompleteQueryFails)
    { CheckClodEmptyQueryFallsBackButIncompleteQueryFails(); }
    TEST_F(TerrainSectorLifetimeTests, SourceChangeDuringPreparationRejectsOwnedOutput)
    { CheckSourceChangeDuringPreparationRejectsOwnedOutput(); }

    TEST_F(TerrainSectorLifetimeTests, ConfigurationChangeAndMalformedDataRejectBeforeCommit)
    { CheckConfigurationChangeAndMalformedDataRejectBeforeCommit(); }
    TEST_F(TerrainSectorLifetimeTests, PublicationLockCoversEveryCommitInBatch)
    { CheckPublicationLockCoversEveryCommitInBatch(); }
    TEST_F(TerrainSectorLifetimeTests, ReorderedIndependentCompletionsCommitOnceIncludingEmpty) { CheckReorderedIndependentCompletionsCommitOnceIncludingEmpty(); }
    TEST_F(TerrainSectorLifetimeTests, SupersededRequestCannotCommitAtSameCoordinate) { CheckSupersededRequestCannotCommitAtSameCoordinate(); }
    TEST_F(TerrainSectorLifetimeTests, CoordinateRoundTripAcrossZeroRejectsPreviousLease) { CheckCoordinateRoundTripAcrossZeroRejectsPreviousLease(); }
    TEST_F(TerrainSectorLifetimeTests, RecreatedSlotAtSameCoordinateAndSerialRejectsOldIdentity) { CheckRecreatedSlotAtSameCoordinateAndSerialRejectsOldIdentity(); }
    TEST_F(TerrainSectorLifetimeTests, OwnedPreparationOutlivesManagerWithClodRayTracingAndRetainedQueries) { CheckOwnedPreparationOutlivesManagerWithClodRayTracingAndRetainedQueries(); }
    TEST_F(TerrainSectorLifetimeTests, PopulatedAndEmptyResultsShareAllOrNothingAcceptance) { CheckPopulatedAndEmptyResultsShareAllOrNothingAcceptance(); }
    TEST_F(TerrainSectorLifetimeTests, FailedOrCancelledQueriesNeverReachCommit) { CheckFailedOrCancelledQueriesNeverReachCommit(); }
    TEST_F(TerrainSectorLifetimeTests, PublicationReplacementSceneRemovalAndRegistryShutdownRejectRetainedWork) { CheckPublicationReplacementSceneRemovalAndRegistryShutdownRejectRetainedWork(); }
    TEST_F(TerrainSectorLifetimeTests, ImmediateSourceInvalidationRejectsUnchangedPublication) { CheckImmediateSourceInvalidationRejectsUnchangedPublication(); }
    TEST_F(TerrainSectorLifetimeTests, TerrainSettingsInvalidationAndResetRejectPendingWork) { CheckTerrainSettingsInvalidationAndResetRejectPendingWork(); }
}
