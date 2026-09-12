#include <AzTest/AzTest.h>
#include <Atom/RHI/RHISystem.h>
#include <Atom/RPI.Public/Shader/ShaderSystemInterface.h>
#include <TerrainRenderer/TerrainMeshManager.h>
#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>
#include <Tests/Mocks/Terrain/MockTerrainDataRequestBus.h>
#include "TerrainTestFixtures.h"
#include "ProceduralSnapshotTestSupport.h"
#include "SectorRayTracingMock.h"
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

        void CheckRequestedPlacementNeverRelabelsCommittedGeometry();
        void CheckFineCoarseGroupCannotCommitPartially();
        void CheckMissingFailedAndEmptyHaveDifferentCoverage();
        void CheckMissingIntermediateLodCannotCoverFinerHoles();
        void CheckFailureAndCancellationRetainOnlyValidOldCoverage();
        void CheckPublicationAndSourceRetirementHideCommittedCoverage();
        void CheckCommittedOwnershipDoesNotRetainWorkerStorage();
        void CheckRayTracingUsesCommittedPlacementAndWithdrawsEmptyReplacement();

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
            m_manager->m_cameraPosition = AZ::Vector3::CreateZero();
            m_manager->m_gridVerts1D = 3;
            m_manager->m_gridVerts2D = 9;
            m_manager->m_worldHeightBounds = { -10.0f, 10.0f };
            m_manager->m_config.m_clodEnabled = true;
            m_manager->m_vertexOrder = { 8, 7, 6, 5, 4, 3, 2, 1, 0 };
            m_manager->m_xyPositions = { {2,2}, {1,2}, {0,2}, {2,1}, {1,1}, {0,1}, {2,0}, {1,0}, {0,0} };
            m_manager->m_sectorLods.resize(1);
            m_manager->m_sectorLods[0].m_sectors.resize(2);
            m_manager->m_sectorLods[0].m_sectors[0].m_requestedWorldCoord = { -1, 0 };
            m_manager->m_sectorLods[0].m_sectors[1].m_requestedWorldCoord = { 0, 0 };
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
                // Observe the real acceptance/commit bookkeeping without a GPU device.
                EXPECT_EQ(sector.m_committed.m_aabb, result.m_aabb);
                EXPECT_EQ(sector.m_committed.m_hasData, result.m_hasData);
                ++m_commits;
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
        sector.m_requestedWorldCoord = { 1, 0 };
        executor.Submit(Capture());
        sector.m_requestedWorldCoord = { -1, 0 };
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
        m_manager->m_sectorLods[0].m_sectors[0].m_committed.m_rtData = AZStd::make_unique<Manager::RtSector>();
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
        m_manager->m_sectorLods[0].m_sectors[0].m_committed.m_rtData = AZStd::make_unique<Manager::RtSector>();
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

    void TerrainSectorLifetimeTests::CheckRequestedPlacementNeverRelabelsCommittedGeometry()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        auto& sector = m_manager->m_sectorLods[0].m_sectors[0];
        ASSERT_TRUE(Accept({ Empty() }));
        const auto originalBounds = sector.m_committed.m_aabb;
        const auto originalSerial = sector.m_committed.m_serial;
        Executor executor;
        for (const auto coordinate : { Vector2i(0,-1), Vector2i(1,1), Vector2i(-1,0), Vector2i(5000,-5000) })
        {
            m_manager->RequestSectorPlacement(sector, coordinate);
            EXPECT_EQ(sector.m_state, Manager::SectorState::Requested);
            executor.Submit(Capture());
            EXPECT_EQ(sector.m_state, Manager::SectorState::Preparing);
            EXPECT_EQ(sector.m_requestedWorldCoord, coordinate);
            EXPECT_EQ(sector.m_committed.m_worldCoord, Vector2i(-1,0));
            EXPECT_EQ(sector.m_committed.m_aabb, originalBounds);
            EXPECT_EQ(sector.m_committed.m_serial, originalSerial);
            EXPECT_FLOAT_EQ(sector.m_committed.m_objectData.m_xyTranslation[0], -2.0f);
            const auto coverage = m_manager->SelectSectorCoverage();
            ASSERT_EQ(coverage.size(), 1);
            EXPECT_EQ(coverage[0].m_sector, &sector);
        }
        EXPECT_FALSE(Accept({ executor.Complete(2) })); // Same coordinate as the committed content, wrong request.
        EXPECT_FALSE(Accept({ executor.Complete(0) }));
        EXPECT_FALSE(Accept({ executor.Complete(1) }));
        EXPECT_EQ(sector.m_state, Manager::SectorState::Preparing);
        const auto current = executor.Complete(3);
        ASSERT_TRUE(Accept({ current }));
        EXPECT_EQ(sector.m_state, Manager::SectorState::Committed);
        EXPECT_EQ(sector.m_committed.m_worldCoord, Vector2i(5000,-5000));
        EXPECT_EQ(sector.m_committed.m_aabb, current->m_aabb);
        EXPECT_FLOAT_EQ(sector.m_committed.m_objectData.m_xyTranslation[0], 10000.0f);
        EXPECT_FLOAT_EQ(sector.m_committed.m_objectData.m_xyTranslation[1], -10000.0f);
        EXPECT_TRUE(m_manager->SelectSectorCoverage().empty()); // Old coverage is retired; new geometry is far from the camera.
        m_manager->m_cameraPosition = AZ::Vector3(10001,-9999,1);
        EXPECT_EQ(m_manager->SelectSectorCoverage().size(), 1);
        EXPECT_FALSE(Accept({ current }));
        EXPECT_EQ(m_commits, 2);
    }

    void TerrainSectorLifetimeTests::CheckFineCoarseGroupCannotCommitPartially()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        m_manager->m_sectorLods.resize(2);
        m_manager->m_sectorLods[1].m_sectors.resize(1);
        auto& fine = m_manager->m_sectorLods[0].m_sectors[0];
        auto& coarse = m_manager->m_sectorLods[1].m_sectors[0];
        m_manager->RequestSectorPlacement(coarse, {-1,0});
        fine.m_committed.m_rtData = AZStd::make_unique<Manager::RtSector>();
        coarse.m_committed.m_rtData = AZStd::make_unique<Manager::RtSector>();
        const auto settings = m_manager->CapturePreparationSettings();
        auto group = std::make_shared<Manager::SectorCommitGroup>();
        group->m_expectedResults = 2;
        Executor executor;
        executor.Submit(m_manager->CaptureSectorRequest(0,0,settings,{},false,{},group));
        executor.Submit(m_manager->CaptureSectorRequest(1,0,settings,{},false,{},group));
        auto coarseResult = executor.Complete(1);
        Manager::SectorCommitStatistics stats;
        AZStd::vector<ResultPtr> incomplete{coarseResult};
        EXPECT_FALSE(m_manager->AcceptPreparedSectors(incomplete, [this](auto&, const auto&) { ++m_commits; }, &stats));
        EXPECT_EQ(stats.m_rejection, Manager::SectorRejection::IncompleteGroup);
        EXPECT_EQ(coarse.m_state, Manager::SectorState::Ready);
        EXPECT_EQ(fine.m_state, Manager::SectorState::Preparing);
        EXPECT_FALSE(coarse.m_committed.m_valid);
        EXPECT_TRUE(m_manager->SelectSectorCoverage().empty());
        auto fineResult = executor.Complete(0);
        AZStd::vector<ResultPtr> complete{coarseResult, fineResult};
        EXPECT_TRUE(m_manager->AcceptPreparedSectors(complete, [&](auto& sector, const auto& result)
        {
            EXPECT_EQ(sector.m_state, Manager::SectorState::Ready);
            EXPECT_FALSE(fine.m_committed.m_valid);
            EXPECT_FALSE(coarse.m_committed.m_valid);
            EXPECT_EQ(sector.m_committed.m_worldCoord, result.m_request.m_worldCoord);
            EXPECT_EQ(sector.m_committed.m_lodLevel, result.m_request.m_lodLevel);
            EXPECT_EQ(sector.m_committed.m_aabb, result.m_aabb);
            EXPECT_EQ(result.m_heights.size(), 9);
            EXPECT_EQ(result.m_lodHeights.size(), 9);
            EXPECT_EQ(result.m_rtPositions.size(), 9);
            EXPECT_EQ(result.m_rtNormals.size(), 9);
            EXPECT_FLOAT_EQ(sector.m_committed.m_heightOrigin, -10.0f);
            ++m_commits;
        }));
        EXPECT_TRUE(fine.m_committed.m_valid);
        EXPECT_TRUE(coarse.m_committed.m_valid);
        EXPECT_EQ(coarse.m_state, Manager::SectorState::Committed);
        EXPECT_FALSE(Accept(complete));
        EXPECT_EQ(m_commits, 2);
    }

    void TerrainSectorLifetimeTests::CheckMissingFailedAndEmptyHaveDifferentCoverage()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        m_manager->m_sectorLods.resize(2);
        m_manager->m_sectorLods[1].m_sectors.resize(1);
        auto& fine = m_manager->m_sectorLods[0].m_sectors[0];
        auto& coarse = m_manager->m_sectorLods[1].m_sectors[0];
        m_manager->RequestSectorPlacement(fine, {-1,-1});
        m_manager->RequestSectorPlacement(coarse, {-1,-1});
        EXPECT_TRUE(m_manager->SelectSectorCoverage().empty()); // Initial population without fallback.
        auto coarseResult = std::make_shared<Result>(Manager::PrepareSector(
            m_manager->CaptureSectorRequest(1,0,m_manager->CapturePreparationSettings(),{},false,{})));
        ASSERT_TRUE(Accept({coarseResult}));
        const auto check = [&](uint8_t coarseMask, size_t expectedCount)
        {
            const auto coverage = m_manager->SelectSectorCoverage();
            EXPECT_EQ(coverage.size(), expectedCount);
            auto found = AZStd::find_if(coverage.begin(), coverage.end(), [&](const auto& selected) { return selected.m_sector == &coarse; });
            ASSERT_NE(found, coverage.end());
            EXPECT_EQ(found->m_quadrants, coarseMask);
        };
        auto fineRequest = Capture();
        check(0xf,1); // Preparing is not an authored hole.
        auto failed = std::make_shared<Result>();
        failed->m_request = fineRequest;
        EXPECT_FALSE(Accept({failed}));
        EXPECT_EQ(fine.m_state, Manager::SectorState::Failed);
        check(0xf,1);
        fineRequest = Capture();
        fineRequest.m_hasTerrain = false;
        auto empty = std::make_shared<Result>(Manager::PrepareSector(fineRequest));
        ASSERT_TRUE(Accept({empty}));
        EXPECT_TRUE(fine.m_committed.m_valid);
        EXPECT_FALSE(fine.m_committed.m_hasData);
        EXPECT_FALSE(fine.m_committed.m_aabb.IsValid());
        check(0x7,1); // Negative coordinates map the fine empty sector to quadrant 3.
        fineRequest = Capture();
        check(0x7,1); // Retain authoritative empty coverage while a new request prepares.
        ASSERT_TRUE(Accept({std::make_shared<Result>(Manager::PrepareSector(fineRequest))}));
        check(0x7,2);
        auto replacementEmpty = Capture();
        replacementEmpty.m_hasTerrain = false;
        ASSERT_TRUE(Accept({std::make_shared<Result>(Manager::PrepareSector(replacementEmpty))}));
        check(0x7,1);
    }

    void TerrainSectorLifetimeTests::CheckMissingIntermediateLodCannotCoverFinerHoles()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        m_manager->m_sectorLods.resize(3);
        m_manager->m_sectorLods[2].m_sectors.resize(1);
        auto& fine = m_manager->m_sectorLods[0].m_sectors[0];
        auto& coarse = m_manager->m_sectorLods[2].m_sectors[0];
        m_manager->RequestSectorPlacement(fine, {-1,-1});
        m_manager->RequestSectorPlacement(coarse, {-1,-1});
        auto coarseResult = std::make_shared<Result>(Manager::PrepareSector(
            m_manager->CaptureSectorRequest(2,0,m_manager->CapturePreparationSettings(),{},false,{})));
        ASSERT_TRUE(Accept({coarseResult}));
        auto emptyRequest = Capture();
        emptyRequest.m_hasTerrain = false;
        ASSERT_TRUE(Accept({std::make_shared<Result>(Manager::PrepareSector(emptyRequest))}));
        auto coverage = m_manager->SelectSectorCoverage();
        ASSERT_EQ(coverage.size(),1);
        EXPECT_EQ(coverage[0].m_sector, &coarse);
        EXPECT_EQ(coverage[0].m_quadrants, 0x7); // Drawing quadrant 3 would fill a finer authored hole.
        ASSERT_TRUE(Accept({Empty()}));
        coverage = m_manager->SelectSectorCoverage();
        ASSERT_EQ(coverage.size(),2);
        for (const auto& selected : coverage)
            EXPECT_EQ(selected.m_quadrants, selected.m_sector == &coarse ? 0x7 : 0xf);
    }

    void TerrainSectorLifetimeTests::CheckFailureAndCancellationRetainOnlyValidOldCoverage()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        auto& sector = m_manager->m_sectorLods[0].m_sectors[0];
        ASSERT_TRUE(Accept({Empty()}));
        const auto committedSerial = sector.m_committed.m_serial;
        auto request = Capture();
        auto failed = std::make_shared<Result>();
        failed->m_request = request;
        EXPECT_FALSE(Accept({failed}));
        EXPECT_EQ(sector.m_state, Manager::SectorState::Failed);
        EXPECT_EQ(m_manager->SelectSectorCoverage().size(),1);
        request = Capture();
        m_manager->CancelSectorPreparation(sector);
        EXPECT_TRUE(request.m_cancelled->load());
        EXPECT_EQ(sector.m_state, Manager::SectorState::Cancelled);
        EXPECT_EQ(m_manager->SelectSectorCoverage().size(),1);
        EXPECT_FALSE(Accept({std::make_shared<Result>(Manager::PrepareSector(request))}));
        EXPECT_EQ(sector.m_committed.m_serial, committedSerial);
        EXPECT_EQ(m_commits,1);
        m_manager->m_preparationLifetime->Invalidate();
        EXPECT_TRUE(m_manager->SelectSectorCoverage().empty());
        EXPECT_FALSE(sector.m_committed.m_valid);
        EXPECT_EQ(sector.m_state, Manager::SectorState::Invalidated);
        EXPECT_TRUE(m_manager->m_rebuildSectors);
    }

    void TerrainSectorLifetimeTests::CheckPublicationAndSourceRetirementHideCommittedCoverage()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        for (int invalidation = 0; invalidation < 5; ++invalidation)
        {
            auto registry = std::make_unique<TerrainCompositor::TerrainMeshCutoutRenderRegistry>();
            m_channel = registry->AcquireSceneChannel(this);
            auto snapshot = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderSnapshot>();
            TerrainCompositor::TerrainRenderGeometryQuery query;
            query.m_preparationDependency = std::make_shared<TerrainCompositor::TerrainPreparationDependency>();
            snapshot->m_renderGeometryQueries.push_back(query);
            m_channel->m_snapshot.store(snapshot);
            ASSERT_TRUE(Accept({Empty()}));
            auto emptyRequest = Capture(1);
            emptyRequest.m_hasTerrain = false;
            ASSERT_TRUE(Accept({std::make_shared<Result>(Manager::PrepareSector(emptyRequest))}));
            auto populated = Empty();
            auto empty = Empty(1);
            if (invalidation == 0) query.m_preparationDependency->Invalidate();
            if (invalidation == 1) query.m_preparationDependency->Retire();
            if (invalidation == 2) registry->Publish(this, AZ::Uuid::CreateRandom(), {});
            if (invalidation == 3) registry->RemoveScene(this);
            if (invalidation == 4) registry.reset();
            const auto commits = m_commits;
            EXPECT_FALSE(Accept({populated,empty}));
            EXPECT_EQ(m_commits, commits);
            EXPECT_TRUE(m_manager->SelectSectorCoverage().empty());
            for (const auto& sector : m_manager->m_sectorLods[0].m_sectors)
            {
                EXPECT_FALSE(sector.m_committed.m_valid);
                EXPECT_FALSE(sector.m_committed.m_publication);
            }
        }
    }

    void TerrainSectorLifetimeTests::CheckCommittedOwnershipDoesNotRetainWorkerStorage()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        auto result = Empty();
        std::weak_ptr<const Manager::SectorPreparationSettings> settings = result->m_request.m_data.m_settings;
        std::weak_ptr<std::atomic_bool> cancellation = result->m_request.m_cancelled;
        std::weak_ptr<const Manager::SectorDestinationIdentity> identity = result->m_request.m_destinationIdentity;
        EXPECT_GT(Manager::GetPreparedCpuBytes(*result), sizeof(Result));
        EXPECT_EQ(m_manager->GetSectorResourceAccounting().m_pendingRequests,1);
        ASSERT_TRUE(Accept({result}));
        EXPECT_EQ(m_manager->GetSectorResourceAccounting().m_pendingRequests,0);
        result.reset();
        EXPECT_TRUE(settings.expired());
        EXPECT_TRUE(cancellation.expired());
        EXPECT_FALSE(identity.expired()); // Only the slot owns the identity now.
        EXPECT_GT(m_manager->GetSectorResourceAccounting().m_committedCpuBytes,0);
        m_manager->ClearSectorBuffers();
        EXPECT_TRUE(identity.expired());
        const auto accounting = m_manager->GetSectorResourceAccounting();
        EXPECT_EQ(accounting.m_allocatedGpuBytes,0);
        EXPECT_EQ(accounting.m_retainedGpuBytes,0);
        EXPECT_EQ(accounting.m_committedCpuBytes,0);
        EXPECT_EQ(accounting.m_pendingRequests,0);
        EXPECT_TRUE(m_manager->m_candidateSectors.empty());
        EXPECT_TRUE(m_manager->m_sectorsThatNeedSrgCompiled.empty());
    }

    void TerrainSectorLifetimeTests::CheckRayTracingUsesCommittedPlacementAndWithdrawsEmptyReplacement()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        ::testing::StrictMock<AZ::Render::SectorRayTracingMock> rayTracing;
        m_manager->m_rayTracingEnabled = true;
        m_manager->m_rayTracingFeatureProcessor = &rayTracing;
        const std::shared_ptr<void> cleanup(nullptr, [this](void*)
        {
            m_manager->ClearSectorBuffers();
            m_manager->m_rayTracingFeatureProcessor = nullptr;
        });
        auto& sector = m_manager->m_sectorLods[0].m_sectors[0];
        sector.m_committed.m_rtData = AZStd::make_unique<Manager::RtSector>();
        const auto id = sector.m_committed.m_rtData->m_meshGroups[0].m_id;
        ASSERT_TRUE(Accept({Empty()}));
        EXPECT_CALL(rayTracing, AddMesh(id, ::testing::_, ::testing::_)).WillOnce([](const auto&, const auto& mesh, const auto&)
        {
            EXPECT_EQ(mesh.m_transform.GetTranslation(), AZ::Vector3(-2,0,-10));
        });
        m_manager->UpdateCandidateSectors();
        ASSERT_EQ(m_manager->m_candidateSectors.size(), 1);
        ASSERT_EQ(m_manager->m_rayTracedItems.size(), 1);
        EXPECT_EQ(m_manager->m_candidateSectors[0].m_aabb, sector.m_committed.m_aabb);
        m_manager->RequestSectorPlacement(sector, {1,1});
        auto delayed = Empty();
        m_manager->UpdateCandidateSectors(); // Strict mock forbids moving/re-registering the old geometry.
        EXPECT_EQ(sector.m_committed.m_rtData->m_meshGroups[0].m_mesh.m_transform.GetTranslation(), AZ::Vector3(-2,0,-10));
        EXPECT_CALL(rayTracing, RemoveMesh(id));
        ASSERT_TRUE(Accept({delayed}));
        EXPECT_TRUE(m_manager->m_rayTracedItems.empty());
        EXPECT_CALL(rayTracing, AddMesh(id, ::testing::_, ::testing::_)).WillOnce([](const auto&, const auto& mesh, const auto&)
        {
            EXPECT_EQ(mesh.m_transform.GetTranslation(), AZ::Vector3(2,2,-10));
        });
        m_manager->UpdateCandidateSectors();
        EXPECT_FALSE(Accept({delayed})); // Duplicate completion cannot remove or re-add anything.
        auto emptyRequest = Capture();
        emptyRequest.m_hasTerrain = false;
        EXPECT_CALL(rayTracing, RemoveMesh(id));
        ASSERT_TRUE(Accept({std::make_shared<Result>(Manager::PrepareSector(emptyRequest))}));
        EXPECT_TRUE(m_manager->m_rayTracedItems.empty());
        m_manager->UpdateCandidateSectors();
        EXPECT_TRUE(m_manager->m_candidateSectors.empty());
        m_manager->ClearSectorBuffers();
        m_manager->m_rayTracingFeatureProcessor = nullptr;
    }

    TEST_F(TerrainSectorLifetimeTests, RayTracingUsesCommittedPlacementAndWithdrawsEmptyReplacement)
    { CheckRayTracingUsesCommittedPlacementAndWithdrawsEmptyReplacement(); }
    TEST_F(TerrainSectorLifetimeTests, RequestedPlacementNeverRelabelsCommittedGeometry) { CheckRequestedPlacementNeverRelabelsCommittedGeometry(); }
    TEST_F(TerrainSectorLifetimeTests, FineCoarseGroupCannotCommitPartially) { CheckFineCoarseGroupCannotCommitPartially(); }
    TEST_F(TerrainSectorLifetimeTests, MissingFailedAndEmptyHaveDifferentCoverage) { CheckMissingFailedAndEmptyHaveDifferentCoverage(); }
    TEST_F(TerrainSectorLifetimeTests, MissingIntermediateLodCannotCoverFinerHoles) { CheckMissingIntermediateLodCannotCoverFinerHoles(); }
    TEST_F(TerrainSectorLifetimeTests, FailureAndCancellationRetainOnlyValidOldCoverage) { CheckFailureAndCancellationRetainOnlyValidOldCoverage(); }
    TEST_F(TerrainSectorLifetimeTests, PublicationAndSourceRetirementHideCommittedCoverage) { CheckPublicationAndSourceRetirementHideCommittedCoverage(); }
    TEST_F(TerrainSectorLifetimeTests, CommittedOwnershipDoesNotRetainWorkerStorage) { CheckCommittedOwnershipDoesNotRetainWorkerStorage(); }

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
