#include <AzTest/AzTest.h>
#include <AzCore/Jobs/JobCompletion.h>
#include <AzCore/Jobs/JobManager.h>
#include <AzCore/Jobs/JobManagerBus.h>
#include <TerrainSystem/TerrainSystem.h>
#include <Terrain/MockTerrain.h>
#include <Atom/RHI/RHISystem.h>
#include <Atom/RPI.Public/Shader/ShaderSystemInterface.h>
#include <TerrainRenderer/TerrainMeshManager.h>
#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>
#include <Tests/Mocks/Terrain/MockTerrainDataRequestBus.h>
#include "TerrainTestFixtures.h"
#include "ProceduralSnapshotTestSupport.h"
#include "SectorRayTracingMock.h"
#include <thread>
#include <cstdio>

namespace Terrain
{
    class TerrainSectorLifetimeTests : public ::testing::Test
    {
    protected:
        void CheckReorderedIndependentCompletionsCommitOnceIncludingEmpty();
        void CheckRecoveryRejectsUncertifiedNeighborsWithoutConsumingLeases();
        void CheckRecoveryRequiresIntermediateCoverageAndPreservesHoles();
        void CheckRecoveryCoverageProjectionDistinguishesUnknownAndEmpty();
        void CheckRecoveryUsesRendererDistanceFilter();
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
        void MeasureProceduralHillKernels();
        void CheckRetainedOnlyMatchesRealTerrainAndCoordinates();
        void CheckDeferredAreaCaptureAndSettingsRevalidation();
        void CheckPredictedLeaseAdoptionAndCancellation();
        void CheckUploadsWaitForWholeGroupBeforeConsumingLeases();
        void CheckSourceChangeAndCancellationDuringUploadWaitRejectPublication();
        void CheckRetainedOnlyWholeSectorFallbackAndInvalidation();
        void CheckProceduralSnapshotChangeRemovalAndReconnectionRejectDelayedResults();
        void CheckProceduralSnapshotChangeDuringOrdinarySamplingRejectsResults();
        void CheckProceduralSnapshotPublicationReplacementRejectsResults();
        void CheckAcquisitionCannotRefreshAnOldSourceGenerationTicket();
        void CheckCompletePlanPreservesGatherCoordinatesAndCallbackOrder();
        void CheckPooledJobOwnsCompletePlanWithoutCapturingItsStorage();
        void CheckDeliveryAndAtomicBudgetDoNotConsumeReadyGroup();
        void CheckReplacementUnitRejectsMixedSettingsGroupsAndMissingMembers();
        void CheckSplitAssessmentCannotAuthorizeSharedOrUnrepresentableReplacement();
        void CheckWorkStorageAndLifecycleReclaimedAfterDelayedCancellation();
        void CheckCancellationBetweenGathersSkipsClodAndRayTracing();
        void CheckInvalidationDuringBudgetWaitRejectsBeforeAnyCommit();

        void CheckRequestedPlacementNeverRelabelsCommittedGeometry();
        void CheckFineCoarseGroupCannotCommitPartially();
        void CheckMissingFailedAndEmptyHaveDifferentCoverage();
        void CheckMissingIntermediateLodCannotCoverFinerHoles();
        void CheckFailureAndCancellationRetainOnlyValidOldCoverage();
        void CheckPublicationAndSourceRetirementHideCommittedCoverage();
        void CheckCommittedOwnershipDoesNotRetainWorkerStorage();
        void CheckSharedCoverageMetadataKeepsIndependentInvalidation();
        void CheckSourceInvalidationWithdrawsWarmedRasterAndRayTracing();
        void CheckRayTracingUsesCommittedPlacementAndWithdrawsEmptyReplacement();
        void CheckIncrementalCoverageBatchesKeepRasterRtResourcesCurrent();
        void CheckCoverageMaskChangesKeepCommonRayTracingQuadrants();

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

#include "ProceduralHillKernelSectorTests.inl"

    void TerrainSectorLifetimeTests::CheckRecoveryRejectsUncertifiedNeighborsWithoutConsumingLeases()
    {
        using namespace TerrainCompositor;
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        SnapshotTestSupport::Composition scene;
        ASSERT_TRUE(scene.AddMeshHeight());
        m_channel = scene.Channel();
        const auto captureUnit = [&](size_t slot)
        {
            auto request = Capture(slot);
            auto group = std::make_shared<Manager::SectorCommitGroup>();
            group->m_expectedResults = 1;
            group->m_boundedRecovery = true;
            request.m_group = group;
            request.m_samplingPlan.m_queryPolicy = TerrainSectorQueryPolicy::RetainedOnly;
            request.m_samplingPlan.m_schedulePolicy = TerrainSectorSchedulePolicy::Deferred;
            return request;
        };
        auto first = captureUnit(0), second = captureUnit(1);
        const auto reference = Manager::PrepareSector(first);
        auto b = std::make_shared<Result>(Manager::PrepareSector(second));
        ASSERT_TRUE(Accept({ b }));
        auto& neighbor = m_manager->m_sectorLods[0].m_sectors[1].m_committed;
        ASSERT_TRUE(neighbor.m_samplingCertificate.m_proven);
        const auto saved = neighbor.m_samplingCertificate;
        neighbor.m_samplingCertificate.m_proven = false;
        auto a = std::make_shared<Result>(Manager::PrepareSector(first));
        const auto serial = m_manager->m_sectorLods[0].m_sectors[0].m_preparationSerial;
        Manager::SectorCommitStatistics statistics;
        EXPECT_FALSE(m_manager->AcceptPreparedSectors({ &a, 1 }, [](auto&, const auto&) { FAIL(); }, &statistics));
        EXPECT_EQ(statistics.m_rejection, Manager::SectorRejection::Sampling);
        EXPECT_EQ(m_manager->m_sectorLods[0].m_sectors[0].m_preparationSerial, serial);
        EXPECT_TRUE(neighbor.m_valid);
        neighbor.m_samplingCertificate = saved;
        ASSERT_TRUE(Accept({ a }));
        ASSERT_EQ(a->m_heights.size(), reference.m_heights.size());
        ASSERT_EQ(a->m_lodHeights.size(), reference.m_lodHeights.size());
        for (size_t i = 0; i < a->m_heights.size(); ++i)
        {
            EXPECT_EQ(a->m_heights[i].m_height, reference.m_heights[i].m_height);
            EXPECT_EQ(a->m_heights[i].m_normal, reference.m_heights[i].m_normal);
            EXPECT_EQ(a->m_lodHeights[i].m_height, reference.m_lodHeights[i].m_height);
            EXPECT_EQ(a->m_lodHeights[i].m_normal, reference.m_lodHeights[i].m_normal);
        }
        auto stale = std::make_shared<Result>(Manager::PrepareSector(captureUnit(0)));
        scene.StopSource();
        EXPECT_FALSE(Accept({ stale }));
        EXPECT_EQ(m_commits, 2);
    }

    void TerrainSectorLifetimeTests::CheckRecoveryRequiresIntermediateCoverageAndPreservesHoles()
    {
        using namespace TerrainCompositor;
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        SnapshotTestSupport::Composition scene;
        ASSERT_TRUE(scene.AddImageHole(7));
        m_channel = scene.Channel();
        m_manager->m_sampleSpacing = 1;
        m_manager->m_sectorLods.resize(3);
        for (auto& level : m_manager->m_sectorLods)
        {
            level.m_sectors.resize(1);
            level.m_sectors[0].m_requestedWorldCoord = { 0, 0 };
        }
        const auto prepareUnit = [&](uint32_t lod)
        {
            auto group = std::make_shared<Manager::SectorCommitGroup>();
            group->m_expectedResults = 1;
            group->m_boundedRecovery = true;
            auto request = m_manager->CaptureSectorRequest(lod, 0, m_manager->CapturePreparationSettings(),
                m_channel, true, m_channel->m_snapshot.load(), group);
            request.m_samplingPlan.m_queryPolicy = TerrainSectorQueryPolicy::RetainedOnly;
            request.m_samplingPlan.m_schedulePolicy = TerrainSectorSchedulePolicy::Deferred;
            return std::make_shared<Result>(Manager::PrepareSector(request));
        };
        auto coarse = prepareUnit(2);
        ASSERT_TRUE(coarse->m_hasData);
        ASSERT_TRUE(coarse->m_aabb.IsValid());
        ASSERT_TRUE(Accept({ coarse }));
        auto fine = prepareUnit(0);
        ASSERT_EQ(fine->m_status, Status::Empty);
        Manager::SectorCommitStatistics statistics;
        EXPECT_FALSE(m_manager->AcceptPreparedSectors({ &fine, 1 }, [](auto&, const auto&) { FAIL(); }, &statistics));
        EXPECT_EQ(statistics.m_rejection, Manager::SectorRejection::Coverage);
        auto intermediate = prepareUnit(1);
        ASSERT_TRUE(Accept({ intermediate }));
        ASSERT_TRUE(Accept({ fine }));
        EXPECT_TRUE(m_manager->m_sectorLods[0].m_sectors[0].m_committed.m_valid);
        EXPECT_FALSE(m_manager->m_sectorLods[0].m_sectors[0].m_committed.m_hasData);
    }

    void TerrainSectorLifetimeTests::CheckRecoveryCoverageProjectionDistinguishesUnknownAndEmpty()
    {
        m_manager->m_sampleSpacing = 1;
        m_manager->m_sectorLods.resize(3);
        for (auto& lod : m_manager->m_sectorLods) lod.m_sectors.resize(1);
        auto& coarse = m_manager->m_sectorLods[2].m_sectors[0].m_committed;
        coarse.m_valid = coarse.m_hasData = true;
        coarse.m_worldCoord = { 0, 0 };
        coarse.m_lodLevel = 2;
        coarse.m_aabb = AZ::Aabb::CreateFromMinMaxValues(0, 0, -10, 8, 8, 10);
        auto& fine = m_manager->m_sectorLods[0].m_sectors[0].m_committed;
        fine.m_valid = true;
        fine.m_hasData = false;
        fine.m_worldCoord = { 0, 0 };
        fine.m_lodLevel = 0;
        EXPECT_TRUE(m_manager->HasRecoveryCoverage(AZ::Vector3(1, 1, 0))); // Authoritative empty.
        EXPECT_FALSE(m_manager->HasRecoveryCoverage(AZ::Vector3(3, 3, 0))); // Missing intermediate remainder.
        EXPECT_TRUE(m_manager->HasRecoveryCoverage(AZ::Vector3(6, 6, 0))); // Representable coarse quadrant.
        auto& intermediate = m_manager->m_sectorLods[1].m_sectors[0].m_committed;
        intermediate.m_valid = true;
        intermediate.m_hasData = false;
        intermediate.m_worldCoord = { 0, 0 };
        intermediate.m_lodLevel = 1;
        EXPECT_TRUE(m_manager->HasRecoveryCoverage(AZ::Vector3(3, 3, 0)));
    }

    void TerrainSectorLifetimeTests::CheckRecoveryUsesRendererDistanceFilter()
    {
        using namespace TerrainCompositor;
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        SnapshotTestSupport::Composition scene;
        ASSERT_TRUE(scene.AddMeshHeight());
        m_channel = scene.Channel();
        m_manager->m_sampleSpacing = 1;
        m_manager->m_sectorLods.resize(3);
        for (auto& level : m_manager->m_sectorLods)
        {
            level.m_sectors.resize(1);
            level.m_sectors[0].m_requestedWorldCoord = { 0, 0 };
        }
        const auto prepare = [&](uint32_t lod)
        {
            auto group = std::make_shared<Manager::SectorCommitGroup>();
            group->m_expectedResults = 1;
            group->m_boundedRecovery = true;
            auto request = m_manager->CaptureSectorRequest(lod, 0, m_manager->CapturePreparationSettings(),
                m_channel, true, m_channel->m_snapshot.load(), group);
            request.m_samplingPlan.m_queryPolicy = TerrainSectorQueryPolicy::RetainedOnly;
            request.m_samplingPlan.m_schedulePolicy = TerrainSectorSchedulePolicy::Deferred;
            return std::make_shared<Result>(Manager::PrepareSector(request));
        };
        auto fine = prepare(0);
        ASSERT_TRUE(fine->m_hasData);
        ASSERT_TRUE(Accept({ fine }));
        auto coarse = prepare(2);
        ASSERT_TRUE(coarse->m_hasData);
        Manager::SectorCommitStatistics statistics;
        EXPECT_FALSE(m_manager->AcceptPreparedSectors({ &coarse, 1 }, [](auto&, const auto&) { FAIL(); }, &statistics));
        EXPECT_EQ(statistics.m_rejection, Manager::SectorRejection::Coverage);
        // The old fine claim no longer participates in drawing after movement.
        // Its missing intermediate must not force a destructive full rebuild.
        m_manager->m_cameraPosition = AZ::Vector3(300, 0, 0);
        ASSERT_TRUE(Accept({ coarse }));
        EXPECT_TRUE(m_manager->m_sectorLods[0].m_sectors[0].m_committed.m_valid);
        EXPECT_EQ(m_manager->m_sectorLods[0].m_sectors[0].m_committed.m_worldCoord.m_x, 0);
    }

    TEST_F(TerrainSectorLifetimeTests, RecoveryUsesRendererDistanceFilter)
    { CheckRecoveryUsesRendererDistanceFilter(); }
    TEST_F(TerrainSectorLifetimeTests, RecoveryCoverageProjectionDistinguishesUnknownAndEmpty)
    { CheckRecoveryCoverageProjectionDistinguishesUnknownAndEmpty(); }
    TEST_F(TerrainSectorLifetimeTests, RecoveryRejectsUncertifiedNeighborsWithoutConsumingLeases)
    { CheckRecoveryRejectsUncertifiedNeighborsWithoutConsumingLeases(); }
    TEST_F(TerrainSectorLifetimeTests, RecoveryRequiresIntermediateCoverageAndPreservesHoles)
    { CheckRecoveryRequiresIntermediateCoverageAndPreservesHoles(); }

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
        emptyRequest.m_samplingPlan.m_area.m_exists = false;
        const auto empty = std::make_shared<Result>(Manager::PrepareSector(AZStd::move(emptyRequest)));
        ASSERT_EQ(populated->m_status, Status::Ready);
        ASSERT_EQ(empty->m_status, Status::Empty);
        empty->m_request.m_cancelled->store(true);
        EXPECT_FALSE(Accept({ populated, empty }));
        EXPECT_EQ(m_commits, 0);
        auto replacementRequest = Capture(1);
        replacementRequest.m_samplingPlan.m_area.m_exists = false;
        const auto replacement = std::make_shared<Result>(Manager::PrepareSector(AZStd::move(replacementRequest)));
        EXPECT_TRUE(Accept({ populated, replacement }));
        EXPECT_EQ(m_commits, 2);
        EXPECT_FALSE(Accept({ populated, replacement }));
    }

    void TerrainSectorLifetimeTests::CheckFailedOrCancelledQueriesNeverReachCommit()
    {
        auto request = Capture();
        request.m_samplingPlan.m_area.m_exists = true; // Ordinary handler disappears after area admission.
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
                request.m_samplingPlan.m_batchQueries = batch;
                request.m_samplingPlan.m_regular.m_sampler = request.m_samplingPlan.m_clod.m_sampler = sampler;
                const auto captured = request.m_samplingPlan.m_sources;
                ASSERT_TRUE(captured && captured->m_queries.front().m_proceduralSnapshot);
                auto legacy = std::make_shared<TerrainCompositor::TerrainRenderQuerySources>();
                legacy->m_publication = request.m_samplingPlan.m_publication;
                legacy->m_queries = legacy->m_publication->m_renderGeometryQueries;
                request.m_samplingPlan.m_sources = legacy;
                const auto expected = Manager::PrepareSector(request);
                request.m_samplingPlan.m_sources = captured;
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
                EXPECT_EQ(actual->m_queryStatistics->m_ordinarySamples, sampler == Requests::Sampler::CLAMP ? 0 : 41);
                EXPECT_EQ(actual->m_queryStatistics->m_skippedOrdinarySamples, sampler == Requests::Sampler::CLAMP ? 41 : 0);
                EXPECT_EQ(actual->m_queryStatistics->m_requiredSectorSamples, 41);
                EXPECT_EQ(actual->m_queryStatistics->m_requiredClodSamples, 16);
                EXPECT_EQ(actual->m_queryStatistics->m_sectorBothOwned, 41);
                EXPECT_EQ(actual->m_queryStatistics->m_sectorAvoidOrdinaryEligible, sampler == Requests::Sampler::CLAMP);
                EXPECT_FALSE(actual->m_queryStatistics->m_sectorAcrossFramesEligible);
                EXPECT_EQ(actual->m_queryStatistics->m_reusedSamples, sampler == Requests::Sampler::CLAMP ? 4 : 0);
                EXPECT_EQ(actual->m_queryStatistics->m_heightSnapshotSamples, sampler == Requests::Sampler::CLAMP ? 37 : 41);
                EXPECT_EQ(actual->m_queryStatistics->m_existenceSnapshotSamples, sampler == Requests::Sampler::CLAMP ? 37 : 41);
                EXPECT_EQ(actual->m_queryStatistics->m_sources.size(), 1); // Captured once across both halos.
                EXPECT_TRUE(Accept({ actual }));
            }
    }

    void TerrainSectorLifetimeTests::CheckRetainedOnlyMatchesRealTerrainAndCoordinates()
    {
        using namespace TerrainCompositor;
        class Jobs final : public AZ::JobManagerBus::Handler
        {
        public:
            Jobs() { BusConnect(); }
            ~Jobs() override { BusDisconnect(); }
            AZ::JobManager* GetManager() override { return &m_manager; }
            AZ::JobContext* GetGlobalContext() override { return &m_context; }
            AZ::JobManager m_manager{ AZ::JobManagerDesc{} };
            AZ::JobContext m_context{ m_manager };
        } jobs;
        class CountingTerrain final : public TerrainSystem
        {
        public:
            void QueryRegion(const AzFramework::Terrain::TerrainQueryRegion& region, TerrainDataMask mask,
                AzFramework::Terrain::SurfacePointRegionFillCallback callback, Sampler sampler) const override
            {
                const auto& layout = m_layouts.at(m_calls++);
                TerrainSystem::QueryRegion(region, mask, [&](size_t x, size_t y, const auto& point, bool exists)
                {
                    EXPECT_EQ(point.m_position.GetX(), layout.Position(x, y).GetX());
                    EXPECT_EQ(point.m_position.GetY(), layout.Position(x, y).GetY());
                    ++m_points;
                    if (!exists) ++m_holes;
                    callback(x, y, point, exists);
                }, sampler);
            }
            AZStd::vector<TerrainSectorSamplingLayout> m_layouts;
            mutable size_t m_calls = 0, m_points = 0, m_holes = 0;
        };
        ProceduralGroundGradientConfig config;
        config.m_hillDensity = 0.3f;
        config.m_amplitudeMeters = 180;
        SnapshotTestSupport::Composition scene(config);
        ASSERT_TRUE(scene.AddImageHole(16, true));
        ASSERT_TRUE(scene.AddMeshHeight());
        ::testing::NiceMock<UnitTest::MockTerrainSpawnerRequests> spawner(scene.m_region);
        ::testing::NiceMock<UnitTest::MockTerrainAreaHeightRequests> provider(scene.m_region);
        ON_CALL(spawner, GetUseGroundPlane).WillByDefault(::testing::Return(false));
        ON_CALL(spawner, GetPriority).WillByDefault([](uint32_t& layer, int32_t& priority) { layer = 0; priority = 0; });
        const TerrainCompositionAddress address{ scene.m_context.m_id, scene.m_owner };
        ON_CALL(provider, GetHeights).WillByDefault([&](AZStd::span<AZ::Vector3> points, AZStd::span<bool> exists)
        {
            AZStd::vector<float> heights(points.size());
            TerrainCompositionHeightRequestBus::Event(address, &TerrainCompositionHeightRequests::GetHeights,
                scene.m_region, AZStd::span<const AZ::Vector3>(points), AZStd::span<float>(heights), exists);
            for (size_t i = 0; i < points.size(); ++i)
            {
                points[i].SetZ(heights[i]);
                // Exercise collision-only holes in the actual ordinary sampler.
                // Render existence deliberately remains independent of these.
                if (points[i].GetX() < 0) exists[i] = false;
            }
        });
        CountingTerrain terrain;
        terrain.SetTerrainHeightBounds({ -10, 10 });
        terrain.SetTerrainHeightQueryResolution(0.5f);
        terrain.Activate();
        AZ::TickBus::Broadcast(&AZ::TickEvents::OnTick, 0.0f, AZ::ScriptTimePoint{});
        m_channel = scene.Channel();
        m_manager->m_gridSize = 128;
        m_manager->m_gridVerts1D = 129;
        m_manager->m_gridVerts2D = 129 * 129;
        m_manager->m_vertexOrder.clear();
        m_manager->m_xyPositions.clear();
        for (uint16_t y = 0; y < 129; ++y)
            for (uint16_t x = 0; x < 129; ++x)
            {
                m_manager->m_vertexOrder.push_back(y * 129 + x);
                m_manager->m_xyPositions.push_back({ uint8_t(x), uint8_t(y) });
            }
        for (bool batch : { false, true })
            for (float spacing : { 0.1f, 0.5f, 1.3f })
                for (float start : { -63.9f, -0.125f, 8192.03f })
                {
                    SCOPED_TRACE(::testing::Message() << batch << "/" << spacing << "/" << start);
                    auto request = Capture();
                    auto settings = std::make_shared<Manager::SectorPreparationSettings>(*request.m_data.m_settings);
                    settings->m_batchQueries = batch;
                    settings->m_retainedOnlyQueries = false;
                    request.m_data.m_settings = settings;
                    auto& sampling = request.m_samplingPlan;
                    sampling.m_batchQueries = batch;
                    sampling.m_rayTracing = true;
                    sampling.m_regular.m_start = sampling.m_clod.m_start = AZ::Vector2(start, -0.125f);
                    sampling.m_regular.m_spacing = spacing;
                    sampling.m_clod.m_spacing = spacing * 2;
                    terrain.m_layouts = { sampling.m_regular, sampling.m_clod };
                    terrain.m_calls = 0;
                    const auto expected = Manager::PrepareSector(request);
                    ASSERT_EQ(expected.m_status, Status::Ready);
                    EXPECT_EQ(terrain.m_calls, 2);
                    EXPECT_EQ(expected.m_queryStatistics->m_ordinarySamples, 21650);
                    // Preserve the comparison's immutable settings object.
                    for (bool reuse : { false, true })
                    {
                    settings = std::make_shared<Manager::SectorPreparationSettings>(*settings);
                    settings->m_retainedOnlyQueries = true;
                    settings->m_sampleReuse = reuse;
                    request.m_data.m_settings = settings;
                    terrain.m_calls = 0;
                    const auto actual = Manager::PrepareSector(request);
                    EXPECT_EQ(terrain.m_calls, 0);
                    EXPECT_EQ(actual.m_status, expected.m_status);
                    EXPECT_EQ(actual.m_aabb, expected.m_aabb);
                    EXPECT_EQ(actual.m_hasData, expected.m_hasData);
                    EXPECT_EQ(actual.m_queryStatistics->m_ordinarySamples, 0);
                    EXPECT_EQ(actual.m_queryStatistics->m_skippedOrdinarySamples, 21650);
                    EXPECT_EQ(actual.m_queryStatistics->m_heightSourceCalls + actual.m_queryStatistics->m_existenceSourceCalls, 0);
                    ASSERT_EQ(actual.m_heights.size(), expected.m_heights.size());
                    ASSERT_EQ(actual.m_lodHeights.size(), expected.m_lodHeights.size());
                    ASSERT_EQ(actual.m_rtPositions.size(), expected.m_rtPositions.size());
                    ASSERT_EQ(actual.m_rtNormals.size(), expected.m_rtNormals.size());
                    for (size_t i = 0; i < actual.m_heights.size(); ++i)
                    {
                        EXPECT_EQ(actual.m_heights[i].m_height, expected.m_heights[i].m_height);
                        EXPECT_EQ(actual.m_heights[i].m_normal, expected.m_heights[i].m_normal);
                        EXPECT_EQ(actual.m_lodHeights[i].m_height, expected.m_lodHeights[i].m_height);
                        EXPECT_EQ(actual.m_lodHeights[i].m_normal, expected.m_lodHeights[i].m_normal);
                        EXPECT_EQ(actual.m_rtPositions[i].x, expected.m_rtPositions[i].x);
                        EXPECT_EQ(actual.m_rtPositions[i].y, expected.m_rtPositions[i].y);
                        EXPECT_EQ(actual.m_rtPositions[i].z, expected.m_rtPositions[i].z);
                        EXPECT_EQ(actual.m_rtNormals[i].x, expected.m_rtNormals[i].x);
                        EXPECT_EQ(actual.m_rtNormals[i].y, expected.m_rtNormals[i].y);
                        EXPECT_EQ(actual.m_rtNormals[i].z, expected.m_rtNormals[i].z);
                    }
                    }
                }
        EXPECT_EQ(terrain.m_points, 18 * 21650);
        EXPECT_GT(terrain.m_holes, 0);
    }

    void TerrainSectorLifetimeTests::CheckRetainedOnlyWholeSectorFallbackAndInvalidation()
    {
        using namespace TerrainCompositor;
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        SnapshotTestSupport::Composition scene;
        m_channel = scene.Channel();
        EXPECT_CALL(terrain, QueryRegion).Times(6);
        // A single unowned corner of either halo forces BOTH gathers to fall back.
        for (bool clod : { false, true })
        {
            auto request = Capture();
            (clod ? request.m_samplingPlan.m_clod : request.m_samplingPlan.m_regular).m_start = AZ::Vector2(9999);
            const auto result = Manager::PrepareSector(request);
            EXPECT_EQ(result.m_status, Status::Ready);
            EXPECT_EQ(result.m_queryStatistics->m_skippedOrdinarySamples, 0);
            EXPECT_EQ(result.m_queryStatistics->m_ordinarySamples, 41);
        }
        scene.m_config.m_holeMask.m_gradientId = AZ::EntityId(998877);
        scene.m_source->ReadInConfig(&scene.m_config);
        const auto masked = Empty();
        EXPECT_EQ(masked->m_queryStatistics->m_skippedOrdinarySamples, 0);
        EXPECT_EQ(masked->m_queryStatistics->m_ordinarySamples, 41);
        EXPECT_NE(masked->m_queryStatistics->m_sectorOrdinaryFallbacks & TerrainRenderFallbackBit(TerrainRenderFallback::ExternalMask), 0);
        scene.m_config.m_holeMask.m_gradientId = AZ::EntityId{};
        scene.m_source->ReadInConfig(&scene.m_config);
        ::testing::Mock::VerifyAndClearExpectations(&terrain);
        EXPECT_CALL(terrain, QueryRegion).Times(0);
        auto request = Capture();
        auto sources = std::make_shared<TerrainRenderQuerySources>(*request.m_samplingPlan.m_sources);
        auto& query = sources->m_queries.front();
        const auto original = query.m_execute;
        const auto authority = query.m_proceduralSnapshot->m_ticket.m_dependency;
        query.m_execute = [original, authority](auto dispatch, auto positions, auto heights, auto exists, auto* statistics)
        {
            original(dispatch, positions, heights, exists, statistics);
            authority->Invalidate(); // Values remain readable; GPU acceptance must fail.
        };
        request.m_samplingPlan.m_sources = sources;
        const auto result = std::make_shared<Result>(Manager::PrepareSector(request));
        EXPECT_EQ(result->m_status, Status::Ready);
        EXPECT_EQ(result->m_queryStatistics->m_skippedOrdinarySamples, 41);
        EXPECT_FALSE(Accept({ result }));
        EXPECT_EQ(m_commits, 0);
        ASSERT_TRUE(scene.AddImageHole(100));
        const auto empty = Empty();
        EXPECT_EQ(empty->m_status, Status::Empty);
        EXPECT_EQ(empty->m_queryStatistics->m_skippedOrdinarySamples, 25);
        EXPECT_EQ(empty->m_queryStatistics->m_requiredSectorSamples, 41);
        EXPECT_TRUE(Accept({ empty }));
    }

    void TerrainSectorLifetimeTests::CheckProceduralSnapshotChangeRemovalAndReconnectionRejectDelayedResults()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        TerrainCompositor::SnapshotTestSupport::Composition scene;
        m_channel = scene.Channel();
        auto request = Capture();
        const auto publication = request.m_samplingPlan.m_publication;
        const auto snapshot = request.m_samplingPlan.m_sources->m_queries.front().m_proceduralSnapshot;
        const auto before = Manager::PrepareSector(request);
        scene.m_config.m_amplitudeMeters = 0.0f;
        scene.m_source->ReadInConfig(&scene.m_config);
        EXPECT_EQ(scene.Publication(), publication); // No deferred composition republish is needed for rejection.
        auto after = std::make_shared<Result>(Manager::PrepareSector(request));
        EXPECT_NE(after->m_queryStatistics->m_sectorOrdinaryFallbacks &
            TerrainCompositor::TerrainRenderFallbackBit(TerrainCompositor::TerrainRenderFallback::StaleDependency), 0);
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
        EXPECT_NE(oldOutput->m_queryStatistics->m_sectorOrdinaryFallbacks &
            TerrainCompositor::TerrainRenderFallbackBit(TerrainCompositor::TerrainRenderFallback::StaleDependency), 0);
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
        auto settings = std::make_shared<Manager::SectorPreparationSettings>(*request.m_data.m_settings);
        settings->m_retainedOnlyQueries = false;
        request.m_data.m_settings = settings;
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
        fineRequest.m_samplingPlan.m_area.m_exists = false;
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
        replacementEmpty.m_samplingPlan.m_area.m_exists = false;
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
        emptyRequest.m_samplingPlan.m_area.m_exists = false;
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
            emptyRequest.m_samplingPlan.m_area.m_exists = false;
            ASSERT_TRUE(Accept({std::make_shared<Result>(Manager::PrepareSector(emptyRequest))}));
            auto populated = Empty();
            auto empty = Empty(1);
            ASSERT_EQ(m_manager->SelectSectorCoverage().size(), 1);
            EXPECT_FALSE(m_manager->m_coverageValidationDirty);
            EXPECT_EQ(m_manager->m_coveragePublications.size(), 1);
            const auto planGeneration = m_manager->m_coverageSelectionScratch.m_generation;
            ASSERT_GT(planGeneration, 0);
            ASSERT_EQ(m_manager->SelectSectorCoverage().size(), 1);
            EXPECT_EQ(m_manager->m_coverageSelectionScratch.m_generation, planGeneration);
            m_manager->RefreshCommittedCoverage(); // Exercise the unchanged metadata path.
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

    void TerrainSectorLifetimeTests::CheckIncrementalCoverageBatchesKeepRasterRtResourcesCurrent()
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
        auto& sectors = m_manager->m_sectorLods[0].m_sectors;
        for (auto& sector : sectors) sector.m_committed.m_rtData = AZStd::make_unique<Manager::RtSector>();
        const auto firstId = sectors[0].m_committed.m_rtData->m_meshGroups[0].m_id;
        const auto secondId = sectors[1].m_committed.m_rtData->m_meshGroups[0].m_id;
        ASSERT_TRUE(Accept({ Empty(0), Empty(1) }));
        EXPECT_CALL(rayTracing, AddMesh(firstId, ::testing::_, ::testing::_));
        EXPECT_CALL(rayTracing, AddMesh(secondId, ::testing::_, ::testing::_));
        m_manager->UpdateCandidateSectors();
        ASSERT_EQ(m_manager->m_candidateSectors.size(), 2);
        const auto plan = m_manager->m_coverageSelectionScratch.m_generation;
        m_manager->UpdateCandidateSectors();
        EXPECT_EQ(m_manager->m_coverageSelectionScratch.m_statistics.m_visitedNodes, 0);
        EXPECT_EQ(m_manager->m_coverageCandidateEdits, 0);
        EXPECT_EQ(m_manager->m_coverageRtEdits, 0);

        // Eligibility changes with fixed grid coordinates preserve the plan.
        m_manager->m_cameraPosition = AZ::Vector3(100000, 100000, 0);
        EXPECT_CALL(rayTracing, RemoveMesh(firstId));
        EXPECT_CALL(rayTracing, RemoveMesh(secondId));
        m_manager->UpdateCandidateSectors();
        EXPECT_TRUE(m_manager->m_candidateSectors.empty());
        EXPECT_EQ(m_manager->m_coverageSelectionScratch.m_generation, plan);
        m_manager->m_cameraPosition = AZ::Vector3::CreateZero();
        EXPECT_CALL(rayTracing, AddMesh(firstId, ::testing::_, ::testing::_));
        EXPECT_CALL(rayTracing, AddMesh(secondId, ::testing::_, ::testing::_));
        m_manager->UpdateCandidateSectors();
        EXPECT_EQ(m_manager->m_coverageSelectionScratch.m_generation, plan);

        // Same coordinate/mask and same mock mesh UUID still replace the bundle.
        const auto version = sectors[0].m_committed.m_coverageResourceVersion;
        EXPECT_CALL(rayTracing, RemoveMesh(firstId));
        ASSERT_TRUE(Accept({ Empty(0) }));
        EXPECT_NE(sectors[0].m_committed.m_coverageResourceVersion, version);
        EXPECT_EQ(m_manager->m_candidateSectors.size(), 1);
        EXPECT_CALL(rayTracing, AddMesh(firstId, ::testing::_, ::testing::_));
        m_manager->UpdateCandidateSectors(); // Strict mock forbids touching sector 1.
        EXPECT_EQ(m_manager->m_candidateSectors.size(), 2);
        EXPECT_EQ(m_manager->m_coverageRtEdits, 1);
        EXPECT_EQ(m_manager->m_coverageSelectionScratch.m_generation, plan);

        m_manager->RequestSectorPlacement(sectors[0], {1, 0});
        EXPECT_CALL(rayTracing, RemoveMesh(firstId));
        ASSERT_TRUE(Accept({ Empty(0) }));
        EXPECT_CALL(rayTracing, AddMesh(firstId, ::testing::_, ::testing::_));
        m_manager->UpdateCandidateSectors(); // Topology reset must not touch sector 1's RT resource.
        EXPECT_TRUE(m_manager->m_coverageSelectionScratch.m_batch.m_reset);
        EXPECT_EQ(m_manager->m_coverageRtEdits, 1);
        EXPECT_GT(m_manager->m_coverageSelectionScratch.m_generation, plan);

        // An independent inspection consumes an evaluation without applying it.
        m_manager->SelectSectorCoverage();
        // Resynchronization must preserve unchanged RT registrations and BLAS.
        m_manager->UpdateCandidateSectors();
        EXPECT_TRUE(m_manager->m_coverageSelectionScratch.m_batch.m_reset);
        EXPECT_EQ(m_manager->m_coverageRtEdits, 0);
        EXPECT_EQ(m_manager->m_appliedCoverageGeneration, m_manager->m_coverageSelectionScratch.m_batch.m_result);
        EXPECT_CALL(rayTracing, RemoveMesh(firstId));
        EXPECT_CALL(rayTracing, RemoveMesh(secondId));
    }

    TEST_F(TerrainSectorLifetimeTests, IncrementalCoverageBatchesKeepRasterRtResourcesCurrent)
    { CheckIncrementalCoverageBatchesKeepRasterRtResourcesCurrent(); }

    void TerrainSectorLifetimeTests::CheckCoverageMaskChangesKeepCommonRayTracingQuadrants()
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
        auto& initial = m_manager->m_sectorLods[0].m_sectors;
        m_manager->RequestSectorPlacement(initial[0], {0,0});
        m_manager->RequestSectorPlacement(initial[1], {1,0});
        for (auto& sector : initial) sector.m_committed.m_rtData = AZStd::make_unique<Manager::RtSector>();
        ASSERT_TRUE(Accept({Empty(0), Empty(1)}));
        m_manager->m_sectorLods.resize(2);
        auto& fine = m_manager->m_sectorLods[0].m_sectors;
        m_manager->m_sectorLods[1].m_sectors.resize(1);
        auto& coarse = m_manager->m_sectorLods[1].m_sectors[0].m_committed;
        coarse.m_worldCoord = {0,0};
        coarse.m_lodLevel = 1;
        coarse.m_valid = coarse.m_hasData = true;
        coarse.m_dependencies = fine[0].m_committed.m_dependencies;
        coarse.m_aabb = AZ::Aabb::CreateFromMinMax(AZ::Vector3(0,0,0), AZ::Vector3(4,4,1));
        m_manager->CreateAabbQuadrants(coarse.m_aabb, coarse.m_quadrantAabbs);
        coarse.m_coverageResourceVersion = ++m_manager->m_nextCoverageResourceVersion;
        coarse.m_rtData = AZStd::make_unique<Manager::RtSector>();
        const auto firstId = fine[0].m_committed.m_rtData->m_meshGroups[0].m_id;
        const auto secondId = fine[1].m_committed.m_rtData->m_meshGroups[0].m_id;
        const auto q1 = coarse.m_rtData->m_meshGroups[2].m_id;
        const auto q2 = coarse.m_rtData->m_meshGroups[3].m_id;
        const auto q3 = coarse.m_rtData->m_meshGroups[4].m_id;
        EXPECT_CALL(rayTracing, AddMesh(firstId, ::testing::_, ::testing::_));
        EXPECT_CALL(rayTracing, AddMesh(secondId, ::testing::_, ::testing::_));
        EXPECT_CALL(rayTracing, AddMesh(q2, ::testing::_, ::testing::_));
        EXPECT_CALL(rayTracing, AddMesh(q3, ::testing::_, ::testing::_));
        m_manager->UpdateCandidateSectors();
        EXPECT_CALL(rayTracing, RemoveMesh(secondId));
        m_manager->HideCommittedSector(fine[1]);
        EXPECT_CALL(rayTracing, AddMesh(q1, ::testing::_, ::testing::_));
        m_manager->UpdateCandidateSectors(); // The common q2/q3 registrations stay live.
        EXPECT_EQ(m_manager->m_coverageRtEdits, 1);
        EXPECT_CALL(rayTracing, RemoveMesh(firstId));
        EXPECT_CALL(rayTracing, RemoveMesh(q1));
        EXPECT_CALL(rayTracing, RemoveMesh(q2));
        EXPECT_CALL(rayTracing, RemoveMesh(q3));
    }

    TEST_F(TerrainSectorLifetimeTests, CoverageMaskChangesKeepCommonRayTracingQuadrants)
    { CheckCoverageMaskChangesKeepCommonRayTracingQuadrants(); }


    void TerrainSectorLifetimeTests::CheckSharedCoverageMetadataKeepsIndependentInvalidation()
    {
        using Dependency = TerrainCompositor::TerrainPreparationDependency;
        auto a = Empty(0), b = Empty(1);
        auto dependency = std::make_shared<Dependency>();
        a->m_request.m_samplingPlan.m_dependencies.push_back({ dependency, dependency->Capture() });
        b->m_request.m_samplingPlan.m_dependencies.push_back({ dependency, dependency->Capture() });
        ASSERT_TRUE(Accept({ a, b }));
        auto& sectors = m_manager->m_sectorLods[0].m_sectors;
        EXPECT_EQ(sectors[0].m_committed.m_dependencies, sectors[1].m_committed.m_dependencies);
        m_manager->UpdateCandidateSectors();
        EXPECT_FALSE(m_manager->m_candidateSectorsDirty);
        EXPECT_FALSE(m_manager->m_coverageValidationDirty);
        EXPECT_EQ(m_manager->m_coverageDependencies.size(), 1);
        m_manager->RefreshCommittedCoverage();
        EXPECT_TRUE(sectors[0].m_committed.m_valid);
        EXPECT_FALSE(m_manager->m_candidateSectorsDirty);
        dependency->Invalidate();
        m_manager->RefreshCommittedCoverage();
        EXPECT_FALSE(sectors[0].m_committed.m_valid);
        EXPECT_FALSE(sectors[1].m_committed.m_valid);
        EXPECT_TRUE(m_manager->m_candidateSectorsDirty);
        EXPECT_TRUE(m_manager->m_candidateSectors.empty());
        m_manager->UpdateCandidateSectors();
        EXPECT_FALSE(m_manager->m_candidateSectorsDirty); // Empty coverage is settled too.
        EXPECT_TRUE(m_manager->m_candidateSectors.empty());

        a = Empty(0); b = Empty(1);
        a->m_request.m_samplingPlan.m_dependencies.push_back({ dependency, dependency->Capture() });
        ASSERT_TRUE(Accept({ a, b }));
        EXPECT_NE(sectors[0].m_committed.m_dependencies, sectors[1].m_committed.m_dependencies);
        m_manager->RefreshCommittedCoverage();
        dependency->Retire();
        m_manager->RefreshCommittedCoverage();
        EXPECT_FALSE(sectors[0].m_committed.m_valid);
        EXPECT_TRUE(sectors[1].m_committed.m_valid); // No union broadens its dependencies.
    }

    TEST_F(TerrainSectorLifetimeTests, SharedCoverageMetadataKeepsIndependentInvalidation)
    { CheckSharedCoverageMetadataKeepsIndependentInvalidation(); }

    void TerrainSectorLifetimeTests::CheckSourceInvalidationWithdrawsWarmedRasterAndRayTracing()
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
        auto source = std::make_shared<TerrainCompositor::TerrainPreparationDependency>();
        auto prepared = Empty();
        prepared->m_request.m_samplingPlan.m_dependencies.push_back({ source, source->Capture() });
        ASSERT_TRUE(Accept({ prepared }));
        EXPECT_CALL(rayTracing, AddMesh(id, ::testing::_, ::testing::_));
        m_manager->UpdateCandidateSectors();
        EXPECT_EQ(m_manager->m_candidateSectors.size(), 1);
        EXPECT_EQ(m_manager->m_rayTracedItems.size(), 1);
        EXPECT_FALSE(m_manager->m_coverageValidationDirty);
        m_manager->RefreshCommittedCoverage();
        source->Invalidate();
        EXPECT_CALL(rayTracing, RemoveMesh(id));
        m_manager->RefreshCommittedCoverage(); // No frame advance or terrain notification.
        EXPECT_FALSE(sector.m_committed.m_valid);
        EXPECT_TRUE(m_manager->m_candidateSectors.empty());
        EXPECT_TRUE(m_manager->m_rayTracedItems.empty());
        EXPECT_TRUE(m_manager->m_coverageDependencies.empty());
        EXPECT_TRUE(m_manager->m_coveragePublications.empty());
        EXPECT_TRUE(m_manager->m_candidateSectorsDirty);
    }

    TEST_F(TerrainSectorLifetimeTests, SourceInvalidationWithdrawsWarmedRasterAndRayTracing)
    { CheckSourceInvalidationWithdrawsWarmedRasterAndRayTracing(); }

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
        emptyRequest.m_samplingPlan.m_area.m_exists = false;
        EXPECT_CALL(rayTracing, RemoveMesh(id));
        ASSERT_TRUE(Accept({std::make_shared<Result>(Manager::PrepareSector(emptyRequest))}));
        EXPECT_TRUE(m_manager->m_rayTracedItems.empty());
        m_manager->UpdateCandidateSectors();
        EXPECT_TRUE(m_manager->m_candidateSectors.empty());
        EXPECT_FALSE(m_manager->m_candidateSectorsDirty);
        EXPECT_EQ(m_manager->m_appliedCoverageGeneration, m_manager->m_coverageSelectionScratch.m_batch.m_result);
        m_manager->ClearSectorBuffers();
        m_manager->m_rayTracingFeatureProcessor = nullptr;
    }

    void TerrainSectorLifetimeTests::CheckCompletePlanPreservesGatherCoordinatesAndCallbackOrder()
    {
        using namespace TerrainCompositor;
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        for (bool batch : { false, true })
        {
            AZStd::vector<size_t> events;
            auto snapshot = std::make_shared<TerrainMeshCutoutRenderSnapshot>();
            TerrainRenderGeometryQuery query;
            query.m_regionBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-100), AZ::Vector3(100));
            query.m_getTerrainExists = [&](const auto& p) { events.push_back(2); EXPECT_FLOAT_EQ(p.GetZ(), -999); return true; };
            query.m_getHeight = [&](const auto& p) { events.push_back(3); EXPECT_FLOAT_EQ(p.GetZ(), -999); return 3.0f; };
            query.m_getGeometry = [&](auto positions, auto heights, auto exists)
            {
                events.push_back(100 + positions.size());
                for (size_t i = 0; i < positions.size(); ++i)
                {
                    EXPECT_FLOAT_EQ(positions[i].GetZ(), -999);
                    heights[i] = 3;
                    exists[i] = true;
                }
            };
            query.m_acquireSources = [&]()
            {
                events.push_back(0);
                return std::make_shared<const TerrainRenderGeometryQuery>(query);
            };
            snapshot->m_renderGeometryQueries.push_back(query);
            m_channel = std::make_shared<TerrainMeshCutoutRenderChannel>();
            m_channel->m_snapshot.store(snapshot);
            ON_CALL(terrain, TerrainAreaExistsInBounds).WillByDefault([&](const auto&) { events.push_back(1); return true; });
            auto settings = std::make_shared<Manager::SectorPreparationSettings>(*m_manager->CapturePreparationSettings());
            settings->m_batchQueries = batch;
            auto request = m_manager->CaptureSectorRequest(0, 0, settings, m_channel, true, snapshot);
            EXPECT_EQ(events, (AZStd::vector<size_t>{0,1})); // Acquisition and area capture only.
            size_t gathers = 0;
            ON_CALL(terrain, QueryRegion).WillByDefault([&](const auto& region, auto mask, auto callback, auto sampler)
            {
                const auto& layout = gathers++ ? request.m_samplingPlan.m_clod : request.m_samplingPlan.m_regular;
                EXPECT_EQ(region.m_startPoint.GetX(), layout.QueryStart().GetX());
                EXPECT_EQ(region.m_startPoint.GetY(), layout.QueryStart().GetY());
                EXPECT_EQ(region.m_stepSize, AZ::Vector2(layout.m_spacing));
                EXPECT_EQ(region.m_numPointsX, layout.Width());
                EXPECT_EQ(region.m_numPointsY, layout.Height());
                EXPECT_EQ(sampler, layout.m_sampler);
                EXPECT_EQ(mask, Requests::TerrainDataMask::Heights);
                events.push_back(10 + layout.Count());
                for (size_t y = 0; y < layout.Height(); ++y)
                    for (size_t x = 0; x < layout.Width(); ++x)
                    {
                        AzFramework::SurfaceData::SurfacePoint point;
                        point.m_position = layout.Position(x,y);
                        point.m_position.SetZ(-999); // Ordinary collision-only fallback reaches retained callbacks unchanged.
                        callback(x, y, point, false);
                    }
            });
            const auto result = Manager::PrepareSector(request);
            EXPECT_EQ(result.m_status, Status::Ready);
            EXPECT_EQ(gathers, 2);
            AZStd::vector<size_t> expected{0,1};
            for (size_t count : {25,16})
            {
                expected.push_back(10 + count);
                if (batch) expected.push_back(100 + count);
                else for (size_t i = 0; i < count; ++i) { expected.push_back(2); expected.push_back(3); }
            }
            EXPECT_EQ(events, expected);
            EXPECT_EQ(result.m_queryStatistics->m_requiredSectorSamples, 41);
            EXPECT_EQ(result.m_queryStatistics->m_ordinarySamples, 41);
            EXPECT_EQ(result.m_queryStatistics->m_scalarCallbacks, batch ? 0 : 82);
            EXPECT_EQ(result.m_queryStatistics->m_batchCallbacks, batch ? 2 : 0);
        }
    }

    void TerrainSectorLifetimeTests::CheckPooledJobOwnsCompletePlanWithoutCapturingItsStorage()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        TerrainCompositor::SnapshotTestSupport::Composition scene;
        m_channel = scene.Channel();
        auto result = std::make_shared<Result>();
        result->m_request = Capture();
        AZ::JobManagerDesc descriptor;
        descriptor.m_workerThreads.emplace_back();
        AZ::JobManager manager(descriptor);
        AZ::JobContext context(manager);
        AZ::JobCompletion completion(&context);
        auto* job = Manager::CreateSectorPreparationJob(result, &context);
        ASSERT_NE(job, nullptr); // Actual pool allocation, not just direct PrepareSector calls.
        m_manager.reset(); // The job owns the plan and cannot refer to the renderer.
        job->SetDependent(&completion);
        job->Start();
        completion.StartAndWaitForCompletion();
        ASSERT_EQ(result->m_status, Status::Ready);
        EXPECT_TRUE(result->m_request.m_samplingPlan.m_assessed);
        EXPECT_EQ(result->m_queryStatistics->m_requiredSectorSamples, 41);
        EXPECT_EQ(result->m_queryStatistics->m_ordinarySamples, 41);
        EXPECT_EQ(result->m_heights.size(), 9);
        EXPECT_EQ(result->m_lodHeights.size(), 9);
    }

    void TerrainSectorLifetimeTests::CheckDeliveryAndAtomicBudgetDoNotConsumeReadyGroup()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        const auto settings = m_manager->CapturePreparationSettings();
        auto group = std::make_shared<Manager::SectorCommitGroup>();
        group->m_expectedResults = 2;
        Executor executor;
        executor.Submit(m_manager->CaptureSectorRequest(0, 0, settings, {}, true, {}, group));
        executor.Submit(m_manager->CaptureSectorRequest(0, 1, settings, {}, true, {}, group));
        auto second = executor.Complete(1);
        EXPECT_EQ(m_manager->DeliverPreparedSector(*second), Manager::SectorRejection::None);
        EXPECT_EQ(m_manager->m_sectorLods[0].m_sectors[1].m_state, Manager::SectorState::Ready);
        EXPECT_FALSE(m_manager->m_sectorLods[0].m_sectors[1].m_committed.m_valid);
        EXPECT_EQ(m_commits, 0);
        const auto deliveryTime = second->m_deliveredAtUs;
        EXPECT_EQ(m_manager->DeliverPreparedSector(*second), Manager::SectorRejection::None);
        EXPECT_EQ(deliveryTime, second->m_deliveredAtUs);
        AZStd::vector<ResultPtr> results{ second, executor.Complete(0) };
        const auto bytes = Manager::GetSectorUploadBytes(results);
        EXPECT_EQ(bytes, 2 * 9 * 2 * sizeof(Manager::HeightNormalVertex));
        Manager::SectorCommitStatistics stats;
        const auto sink = [this](auto&, const auto&) { ++m_commits; };
        m_manager->m_rebuildSectors = false;
        EXPECT_FALSE(m_manager->AcceptPreparedSectors(results, sink, &stats, bytes - 1, bytes));
        EXPECT_EQ(stats.m_rejection, Manager::SectorRejection::Budget);
        EXPECT_FALSE(m_manager->m_rebuildSectors);
        EXPECT_EQ(m_commits, 0);
        for (const auto& result : results) EXPECT_NE(m_manager->FindPreparationDestination(result->m_request), nullptr);
        EXPECT_FALSE(m_manager->AcceptPreparedSectors(results, sink, &stats, bytes, bytes - 1));
        EXPECT_EQ(stats.m_rejection, Manager::SectorRejection::AtomicGroupTooLarge);
        EXPECT_EQ(m_commits, 0);
        EXPECT_TRUE(m_manager->AcceptPreparedSectors(results, sink, &stats, bytes, bytes));
        EXPECT_EQ(stats.m_rejection, Manager::SectorRejection::None);
        EXPECT_EQ(m_commits, 2);
        for (const auto& result : results)
        {
            EXPECT_LE(result->m_request.m_capturedAtUs, result->m_startedAtUs);
            EXPECT_LE(result->m_startedAtUs, result->m_completedAtUs);
            EXPECT_LE(result->m_completedAtUs, result->m_deliveredAtUs);
            const auto& committed = m_manager->m_sectorLods[0].m_sectors[result->m_request.m_slot].m_committed;
            EXPECT_EQ(committed.m_groupId, group->m_id);
            EXPECT_LE(result->m_deliveredAtUs, committed.m_committedAtUs);
        }
        EXPECT_EQ(m_manager->DeliverPreparedSector(*second), Manager::SectorRejection::Destination);
        EXPECT_EQ(m_manager->m_sectorLods[0].m_sectors[1].m_state, Manager::SectorState::Committed);
    }

    void TerrainSectorLifetimeTests::CheckReplacementUnitRejectsMixedSettingsGroupsAndMissingMembers()
    {
        const auto settings = m_manager->CapturePreparationSettings();
        auto group = std::make_shared<Manager::SectorCommitGroup>();
        group->m_expectedResults = 2;
        auto a = std::make_shared<Result>(Manager::PrepareSector(m_manager->CaptureSectorRequest(0, 0, settings, {}, false, {}, group)));
        auto b = std::make_shared<Result>(Manager::PrepareSector(m_manager->CaptureSectorRequest(0, 1, settings, {}, false, {}, group)));
        AZStd::vector<ResultPtr> results{ a, b };
        EXPECT_EQ(Manager::ValidateSectorReplacementUnit(results), Manager::SectorRejection::None);
        results[1].reset();
        EXPECT_EQ(Manager::ValidateSectorReplacementUnit(results), Manager::SectorRejection::IncompleteGroup);
        EXPECT_FALSE(Accept(results));
        results[1] = a;
        EXPECT_EQ(Manager::ValidateSectorReplacementUnit(results), Manager::SectorRejection::Duplicate);
        results[1] = b;
        b->m_request.m_data.m_settings = m_manager->CapturePreparationSettings();
        EXPECT_EQ(Manager::ValidateSectorReplacementUnit(results), Manager::SectorRejection::Settings);
        EXPECT_FALSE(Accept(results));
        b->m_request.m_data.m_settings = settings;
        auto otherGroup = std::make_shared<Manager::SectorCommitGroup>();
        otherGroup->m_expectedResults = 2;
        EXPECT_NE(group->m_id, otherGroup->m_id);
        b->m_request.m_group = otherGroup;
        EXPECT_FALSE(Accept(results));
        EXPECT_EQ(m_commits, 0);
        b->m_request.m_group = group;
        // A current empty result is authoritative only after the complete group.
        EXPECT_TRUE(Accept(results));
        EXPECT_EQ(m_commits, 2);
    }

    void TerrainSectorLifetimeTests::CheckSplitAssessmentCannotAuthorizeSharedOrUnrepresentableReplacement()
    {
        const auto settings = m_manager->CapturePreparationSettings();
        const auto a = m_manager->CaptureSectorRequest(0, 0, settings, {}, false, {});
        const auto b = m_manager->CaptureSectorRequest(0, 1, settings, {}, false, {});
        AZStd::vector<TerrainCompositor::TerrainSectorCoverageClaim> claims{ { -1, -1, 2, true }, { -1, -1, 0, false } };
        const auto projection = TerrainCompositor::SelectTerrainSectorCoverage(claims, 3);
        const auto blockers = Manager::AssessSectorSplit(a, b, projection);
        using Blocker = TerrainCompositor::TerrainSectorSplitBlocker;
        for (auto reason : { Blocker::WholeGroupPolicy, Blocker::LiveSampling, Blocker::SharedSamples, Blocker::ClodSamples, Blocker::Coverage })
            EXPECT_NE(blockers & static_cast<AZ::u32>(reason), 0);
        EXPECT_EQ(blockers & static_cast<AZ::u32>(Blocker::Settings), 0);
        EXPECT_EQ(m_commits, 0);
        EXPECT_TRUE(m_manager->SelectSectorCoverage().empty());
    }

    void TerrainSectorLifetimeTests::CheckWorkStorageAndLifecycleReclaimedAfterDelayedCancellation()
    {
        m_channel = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderChannel>();
        m_channel->m_snapshot.store(std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderSnapshot>());
        const auto settings = m_manager->CapturePreparationSettings();
        auto group = std::make_shared<Manager::SectorCommitGroup>();
        group->m_expectedResults = 2;
        auto pending = std::make_shared<Result>();
        pending->m_request = m_manager->CaptureSectorRequest(0, 0, settings, m_channel, false, m_channel->m_snapshot.load(), group);
        auto finished = std::make_shared<Result>(Manager::PrepareSector(m_manager->CaptureSectorRequest(
            0, 1, settings, m_channel, false, m_channel->m_snapshot.load(), group)));
        m_manager->DeliverPreparedSector(*finished);
        AZStd::vector<ResultPtr> results{ pending, finished };
        const auto storage = Manager::GetSectorWorkStorage(results);
        EXPECT_EQ(storage.m_pendingResults, 1);
        EXPECT_EQ(storage.m_completedResults, 1); // Timing is disabled.
        EXPECT_GT(storage.m_pendingCpuBytes, 0);
        EXPECT_GT(storage.m_completedCpuBytes, 0);
        EXPECT_GE(storage.m_sharedSettingsBytes, sizeof(Manager::SectorPreparationSettings));
        EXPECT_EQ(storage.m_retainedSourceSets, 2);
        EXPECT_EQ(storage.m_retainedPublications, 1);
        EXPECT_GE(storage.m_retainedSourceBytes, 2 * sizeof(TerrainCompositor::TerrainRenderQuerySources));
        EXPECT_GE(storage.m_retainedPublicationBytes, sizeof(TerrainCompositor::TerrainMeshCutoutRenderSnapshot));
        std::weak_ptr<const TerrainCompositor::TerrainRenderQuerySources> retainedSources = pending->m_request.m_samplingPlan.m_sources;
        std::weak_ptr<std::atomic_bool> cancellation = pending->m_request.m_cancelled;
        std::weak_ptr<const Manager::SectorCommitGroup> retainedGroup = group;
        m_manager->RequestSectorPlacement(m_manager->m_sectorLods[0].m_sectors[0], { 10000, -10000 });
        *pending = Manager::PrepareSector(AZStd::move(pending->m_request));
        EXPECT_EQ(pending->m_status, Status::Cancelled);
        EXPECT_EQ(m_manager->DeliverPreparedSector(*pending), Manager::SectorRejection::Destination);
        EXPECT_EQ(m_manager->m_sectorLods[0].m_sectors[0].m_state, Manager::SectorState::Requested);
        EXPECT_FALSE(Accept(results));
        m_manager.reset();
        results.clear();
        pending.reset();
        finished.reset();
        group.reset();
        EXPECT_TRUE(cancellation.expired());
        EXPECT_TRUE(retainedGroup.expired());
        EXPECT_TRUE(retainedSources.expired());
        EXPECT_EQ(Manager::GetSectorWorkStorage(results).m_pendingCpuBytes, 0);
    }

    void TerrainSectorLifetimeTests::CheckCancellationBetweenGathersSkipsClodAndRayTracing()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        auto request = Capture();
        request.m_rayTracing = request.m_samplingPlan.m_rayTracing = true;
        EXPECT_CALL(terrain, QueryRegion).Times(1).WillOnce([&](const auto& region, auto, auto callback, auto)
        {
            for (size_t y = 0; y < region.m_numPointsY; ++y)
                for (size_t x = 0; x < region.m_numPointsX; ++x)
                {
                    AzFramework::SurfaceData::SurfacePoint surface;
                    surface.m_position = AZ::Vector3(float(x), float(y), 1.0f);
                    callback(x, y, surface, true);
                }
            request.m_cancelled->store(true);
        });
        const auto result = Manager::PrepareSector(request);
        EXPECT_EQ(result.m_status, Status::Cancelled);
        EXPECT_TRUE(result.m_lodHeights.empty());
        EXPECT_TRUE(result.m_rtPositions.empty());
        EXPECT_EQ(m_commits, 0);
    }

    void TerrainSectorLifetimeTests::CheckInvalidationDuringBudgetWaitRejectsBeforeAnyCommit()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        m_channel = std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderChannel>();
        m_channel->m_snapshot.store(std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderSnapshot>());
        for (bool changePublication : { true, false })
        {
            const auto settings = m_manager->CapturePreparationSettings();
            auto group = std::make_shared<Manager::SectorCommitGroup>();
            group->m_expectedResults = 2;
            AZStd::vector<ResultPtr> results;
            for (size_t slot = 0; slot < 2; ++slot)
                results.push_back(std::make_shared<Result>(Manager::PrepareSector(m_manager->CaptureSectorRequest(
                    0, slot, settings, m_channel, false, m_channel->m_snapshot.load(), group))));
            const auto bytes = Manager::GetSectorUploadBytes(results);
            Manager::SectorCommitStatistics stats;
            const auto sink = [this](auto&, const auto&) { ++m_commits; };
            EXPECT_FALSE(m_manager->AcceptPreparedSectors(results, sink, &stats, 0, bytes));
            EXPECT_EQ(stats.m_rejection, Manager::SectorRejection::Budget);
            if (changePublication)
                m_channel->m_snapshot.store(std::make_shared<TerrainCompositor::TerrainMeshCutoutRenderSnapshot>());
            else
                m_manager->m_preparationLifetime->Invalidate();
            EXPECT_FALSE(m_manager->AcceptPreparedSectors(results, sink, &stats, bytes, bytes));
            EXPECT_EQ(stats.m_rejection, changePublication ? Manager::SectorRejection::Publication : Manager::SectorRejection::Dependency);
            EXPECT_EQ(m_commits, 0);
            for (const auto& sector : m_manager->m_sectorLods[0].m_sectors) EXPECT_FALSE(sector.m_committed.m_valid);
        }
    }

    TEST_F(TerrainSectorLifetimeTests, DeliveryAndAtomicBudgetDoNotConsumeReadyGroup)
    { CheckDeliveryAndAtomicBudgetDoNotConsumeReadyGroup(); }
    TEST_F(TerrainSectorLifetimeTests, ReplacementUnitRejectsMixedSettingsGroupsAndMissingMembers)
    { CheckReplacementUnitRejectsMixedSettingsGroupsAndMissingMembers(); }
    TEST_F(TerrainSectorLifetimeTests, SplitAssessmentCannotAuthorizeSharedOrUnrepresentableReplacement)
    { CheckSplitAssessmentCannotAuthorizeSharedOrUnrepresentableReplacement(); }
    TEST_F(TerrainSectorLifetimeTests, WorkStorageAndLifecycleReclaimedAfterDelayedCancellation)
    { CheckWorkStorageAndLifecycleReclaimedAfterDelayedCancellation(); }
    TEST_F(TerrainSectorLifetimeTests, CancellationBetweenGathersSkipsClodAndRayTracing)
    { CheckCancellationBetweenGathersSkipsClodAndRayTracing(); }
    TEST_F(TerrainSectorLifetimeTests, InvalidationDuringBudgetWaitRejectsBeforeAnyCommit)
    { CheckInvalidationDuringBudgetWaitRejectsBeforeAnyCommit(); }

    TEST_F(TerrainSectorLifetimeTests, PooledJobOwnsCompletePlanWithoutCapturingItsStorage)
    { CheckPooledJobOwnsCompletePlanWithoutCapturingItsStorage(); }
    TEST_F(TerrainSectorLifetimeTests, CompletePlanPreservesGatherCoordinatesAndCallbackOrder)
    { CheckCompletePlanPreservesGatherCoordinatesAndCallbackOrder(); }
    TEST_F(TerrainSectorLifetimeTests, RayTracingUsesCommittedPlacementAndWithdrawsEmptyReplacement)
    { CheckRayTracingUsesCommittedPlacementAndWithdrawsEmptyReplacement(); }
    TEST_F(TerrainSectorLifetimeTests, RequestedPlacementNeverRelabelsCommittedGeometry) { CheckRequestedPlacementNeverRelabelsCommittedGeometry(); }
    TEST_F(TerrainSectorLifetimeTests, FineCoarseGroupCannotCommitPartially) { CheckFineCoarseGroupCannotCommitPartially(); }
    TEST_F(TerrainSectorLifetimeTests, MissingFailedAndEmptyHaveDifferentCoverage) { CheckMissingFailedAndEmptyHaveDifferentCoverage(); }
    TEST_F(TerrainSectorLifetimeTests, MissingIntermediateLodCannotCoverFinerHoles) { CheckMissingIntermediateLodCannotCoverFinerHoles(); }
    TEST_F(TerrainSectorLifetimeTests, FailureAndCancellationRetainOnlyValidOldCoverage) { CheckFailureAndCancellationRetainOnlyValidOldCoverage(); }
    TEST_F(TerrainSectorLifetimeTests, PublicationAndSourceRetirementHideCommittedCoverage) { CheckPublicationAndSourceRetirementHideCommittedCoverage(); }
    TEST_F(TerrainSectorLifetimeTests, CommittedOwnershipDoesNotRetainWorkerStorage) { CheckCommittedOwnershipDoesNotRetainWorkerStorage(); }


    void TerrainSectorLifetimeTests::CheckDeferredAreaCaptureAndSettingsRevalidation()
    {
        using namespace TerrainCompositor;
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        SnapshotTestSupport::Composition scene;
        m_channel = scene.Channel();
        auto request = Capture();
        request.m_samplingPlan.m_queryPolicy = TerrainSectorQueryPolicy::RetainedOnly;
        request.m_samplingPlan.m_schedulePolicy = TerrainSectorSchedulePolicy::Deferred;
        EXPECT_CALL(terrain, QueryRegion).Times(0);
        auto ready = std::make_shared<Result>(Manager::PrepareSector(request));
        ASSERT_EQ(ready->m_status, Status::Ready);
        EXPECT_EQ(ready->m_admission, TerrainSectorAdmission::Deferred);
        EXPECT_TRUE(ready->m_request.m_samplingPlan.CanExecuteAcrossFrames());
        // Area registration can change before the terrain notification tick. The
        // owned worker decision is safe, but no replacement may publish it stale.
        ON_CALL(terrain, TerrainAreaExistsInBounds).WillByDefault(::testing::Return(false));
        EXPECT_FALSE(Accept({ ready }));
        EXPECT_EQ(m_commits, 0);
        m_manager->m_rebuildSectors = false;
        ON_CALL(terrain, TerrainAreaExistsInBounds).WillByDefault(::testing::Return(true));
        request = Capture();
        request.m_samplingPlan.m_queryPolicy = TerrainSectorQueryPolicy::RetainedOnly;
        request.m_samplingPlan.m_schedulePolicy = TerrainSectorSchedulePolicy::Deferred;
        ready = std::make_shared<Result>(Manager::PrepareSector(request));
        m_manager->m_config.m_clodEnabled = false;
        EXPECT_FALSE(Accept({ ready }));
        EXPECT_EQ(m_commits, 0);
        m_manager->m_config.m_clodEnabled = true;
        EXPECT_TRUE(Accept({ ready }));
        EXPECT_EQ(m_commits, 1);
    }

    void TerrainSectorLifetimeTests::CheckPredictedLeaseAdoptionAndCancellation()
    {
        using namespace TerrainCompositor;
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        SnapshotTestSupport::Composition scene;
        m_channel = scene.Channel();
        auto& sector = m_manager->m_sectorLods[0].m_sectors[0];
        const Vector2i target(1, 0);
        auto request = m_manager->CaptureSectorRequest(0, 0, m_manager->CapturePreparationSettings(), m_channel,
            true, m_channel->m_snapshot.load(), {}, &target);
        request.m_samplingPlan.m_queryPolicy = TerrainSectorQueryPolicy::RetainedOnly;
        request.m_samplingPlan.m_schedulePolicy = TerrainSectorSchedulePolicy::Deferred;
        auto ready = std::make_shared<Result>(Manager::PrepareSector(request));
        EXPECT_EQ(sector.m_requestedWorldCoord, Vector2i(-1, 0));
        EXPECT_EQ(m_manager->FindPreparationDestination(ready->m_request), nullptr);
        const auto serial = sector.m_preparationSerial;
        m_manager->RequestSectorPlacement(sector, target);
        EXPECT_EQ(sector.m_preparationSerial, serial);
        EXPECT_FALSE(request.m_cancelled->load());
        EXPECT_TRUE(Accept({ ready }));
        EXPECT_EQ(sector.m_committed.m_worldCoord, target);
        const Vector2i next(2, 0);
        auto obsolete = m_manager->CaptureSectorRequest(0, 0, m_manager->CapturePreparationSettings(), m_channel,
            true, m_channel->m_snapshot.load(), {}, &next);
        m_manager->RequestSectorPlacement(sector, Vector2i(-1, 0));
        EXPECT_TRUE(obsolete.m_cancelled->load());
        EXPECT_EQ(sector.m_committed.m_worldCoord, target);
        EXPECT_FALSE(Accept({ std::make_shared<Result>(Manager::PrepareSector(obsolete)) }));
    }

    void TerrainSectorLifetimeTests::CheckUploadsWaitForWholeGroupBeforeConsumingLeases()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        const auto settings = m_manager->CapturePreparationSettings();
        auto group = std::make_shared<Manager::SectorCommitGroup>();
        group->m_expectedResults = 2;
        AZStd::vector<ResultPtr> results;
        for (size_t slot = 0; slot < 2; ++slot)
        {
            // Preserve distinct old coverage while the replacement is pending.
            auto& old = m_manager->m_sectorLods[0].m_sectors[slot].m_committed;
            old.m_worldCoord = Vector2i(-20 + int(slot), 0);
            old.m_valid = old.m_hasData = true;
            auto request = m_manager->CaptureSectorRequest(0, slot, settings, {}, false, {}, group);
            request.m_stageUploads = true;
            results.push_back(std::make_shared<Result>(Manager::PrepareSector(AZStd::move(request))));
            ASSERT_EQ(results.back()->m_status, Status::Ready);
        }
        const auto serial = results[0]->m_request.m_serial;
        Manager::SectorCommitStatistics stats;
        EXPECT_FALSE(m_manager->AcceptPreparedSectors(results, [](auto&, const auto&) { FAIL() << "Early upload commit"; }, &stats));
        EXPECT_EQ(stats.m_rejection, Manager::SectorRejection::UploadPending);
        // No upload state, or just one ready member, cannot publish any member.
        results[0]->m_uploads = std::make_unique<Result::Uploads>();
        results[0]->m_uploads->m_ready = true;
        EXPECT_FALSE(Accept(results));
        EXPECT_EQ(m_manager->m_sectorLods[0].m_sectors[0].m_preparationSerial, serial);
        EXPECT_EQ(m_manager->m_sectorLods[0].m_sectors[0].m_committed.m_worldCoord, Vector2i(-20, 0));
        EXPECT_TRUE(m_manager->m_sectorLods[0].m_sectors[0].m_committed.m_valid);
        EXPECT_TRUE(m_manager->m_sectorLods[0].m_sectors[0].m_committed.m_hasData);
        EXPECT_EQ(m_commits, 0);
        // Inject immediate completed transfers; the sink observes the real
        // all-or-nothing acceptance without needing hardware buffers.
        results[1]->m_uploads = std::make_unique<Result::Uploads>();
        results[1]->m_uploads->m_ready = true;
        EXPECT_TRUE(Accept(results));
        EXPECT_EQ(m_commits, 2);
        EXPECT_FALSE(Accept(results));
        EXPECT_EQ(m_commits, 2);
    }

    void TerrainSectorLifetimeTests::CheckSourceChangeAndCancellationDuringUploadWaitRejectPublication()
    {
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        SupplyTerrain(terrain);
        for (bool cancel : { false, true })
        {
            auto request = Capture();
            request.m_stageUploads = true;
            auto result = std::make_shared<Result>(Manager::PrepareSector(AZStd::move(request)));
            ASSERT_EQ(result->m_status, Status::Ready);
            EXPECT_FALSE(Accept({ result }));
            if (cancel) result->m_request.m_cancelled->store(true);
            else m_manager->m_preparationLifetime->Invalidate();
            result->m_uploads = std::make_unique<Result::Uploads>();
            result->m_uploads->m_ready = true;
            EXPECT_FALSE(Accept({ result }));
        }
        EXPECT_EQ(m_commits, 0);
    }

    TEST_F(TerrainSectorLifetimeTests, UploadsWaitForWholeGroupBeforeConsumingLeases)
    { CheckUploadsWaitForWholeGroupBeforeConsumingLeases(); }
    TEST_F(TerrainSectorLifetimeTests, SourceChangeAndCancellationDuringUploadWaitRejectPublication)
    { CheckSourceChangeAndCancellationDuringUploadWaitRejectPublication(); }

    TEST_F(TerrainSectorLifetimeTests, DeferredAreaCaptureAndSettingsRevalidation)
    { CheckDeferredAreaCaptureAndSettingsRevalidation(); }
    TEST_F(TerrainSectorLifetimeTests, PredictedLeaseAdoptionAndCancellation)
    { CheckPredictedLeaseAdoptionAndCancellation(); }

    TEST_F(TerrainSectorLifetimeTests, RetainedOnlyMatchesRealTerrainAndCoordinates)
    { CheckRetainedOnlyMatchesRealTerrainAndCoordinates(); }
    TEST_F(TerrainSectorLifetimeTests, RetainedOnlyWholeSectorFallbackAndInvalidation)
    { CheckRetainedOnlyWholeSectorFallbackAndInvalidation(); }
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
