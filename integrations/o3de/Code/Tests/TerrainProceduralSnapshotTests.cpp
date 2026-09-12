#include "ProceduralSnapshotTestSupport.h"
#include <AzCore/std/containers/array.h>
#include <thread>
#include <chrono>
#include <cstring>

namespace TerrainCompositor
{
    using SnapshotTestSupport::Acquire;
    using SnapshotTestSupport::Current;

    TEST(TerrainProceduralSnapshotTests, ZeroProfilePruningMatchesUnoptimizedLiveKernelBitForBit)
    {
        using Clock = std::chrono::steady_clock;
        double referenceUs = 0, optimizedUs = 0;
        size_t compared = 0;
        for (float density : { 0.005f, 0.3f, 1.0f })
            for (float frequency : { 0.1f, 1.0f, 16.0f })
            {
                ProceduralGroundGradientConfig config;
                config.m_hillDensity = density;
                config.m_frequency = frequency;
                SnapshotTestSupport::Composition scene(config);
                const auto snapshot = Acquire(scene.m_sourceId);
                ASSERT_TRUE(snapshot && snapshot->m_zeroProfilePruning);
                AZStd::vector<AZ::Vector3> points;
                for (size_t i = 0; i < 32768; ++i)
                    points.emplace_back(float(i % 256) * 0.5f - 64.125f,
                        float(i / 256) * 0.1f - 6.39f + (i % 3 == 0 ? 8192.03f : 0.0f), 0.0f);
                AZStd::vector<float> reference(points.size()), optimized(points.size());
                auto start = Clock::now();
                GradientSignal::GradientRequestBus::Event(scene.m_sourceId, &GradientSignal::GradientRequests::GetValues, points, reference);
                referenceUs += std::chrono::duration<double, std::micro>(Clock::now() - start).count();
                start = Clock::now();
                ASSERT_TRUE(snapshot->SampleHeights(points, optimized));
                optimizedUs += std::chrono::duration<double, std::micro>(Clock::now() - start).count();
                for (size_t i = 0; i < points.size(); ++i) EXPECT_EQ(std::memcmp(&reference[i], &optimized[i], sizeof(float)), 0);
                compared += points.size();
            }
        AZ_Printf("TerrainKernelBenchmark", "samples=%zu reference-us=%.3f pruned-us=%.3f\n", compared, referenceUs, optimizedUs);
    }

    TEST(TerrainProceduralSnapshotTests, NormalizedScalarBatchAndClampedConfigurationMatchLiveKernel)
    {
        const AZ::EntityId id(902001);
        for (float amplitude : { -20.0f, 180.0f, 4096.0f })
        {
            ProceduralGroundGradientConfig config;
            config.m_hillDensity = 0.11f;
            config.m_amplitudeMeters = amplitude;
            config.m_frequency = 19.0f;
            ProceduralGroundGradientComponent source(config);
            source.EditorActivate(id);
            const auto snapshot = Acquire(id);
            ASSERT_TRUE(snapshot && snapshot->HasValidContract());
            EXPECT_TRUE(Current(snapshot));
            for (size_t count : { 0, 1, 255, 256, 257 })
            {
                AZStd::vector<AZ::Vector3> points;
                for (size_t i = 0; i < count; ++i) points.emplace_back(float(i) - 130.5f, float(i) * -0.77f, float(i));
                AZStd::vector<float> live(count), retained(count);
                auto exists = std::make_unique<bool[]>(count);
                GradientSignal::GradientRequestBus::Event(id, &GradientSignal::GradientRequests::GetValues, points, live);
                ASSERT_TRUE(snapshot->SampleHeights(points, retained));
                ASSERT_TRUE(snapshot->SampleExistence(points, { exists.get(), count }));
                for (size_t i = 0; i < count; ++i)
                {
                    EXPECT_FLOAT_EQ(live[i], retained[i]);
                    EXPECT_FLOAT_EQ(snapshot->m_heightValue(points[i]), live[i]);
                    EXPECT_GE(retained[i], 0.0f);
                    EXPECT_LE(retained[i], 1.0f);
                    EXPECT_TRUE(exists[i]);
                    points[i].SetZ(-99999.0f);
                    EXPECT_FLOAT_EQ(snapshot->m_heightValue(points[i]), live[i]);
                }
            }
            source.EditorDeactivate(id);
        }
    }

    TEST(TerrainProceduralSnapshotTests, UnsupportedSpansAndCoordinatesLeaveOutputsUntouched)
    {
        SnapshotTestSupport::Composition scene;
        const auto source = Acquire(scene.m_sourceId);
        AZ::Vector3 point(1, 2, 3);
        float height = 123.0f;
        bool exists = false;
        EXPECT_FALSE(source->SampleHeights({ &point, 1 }, {}));
        EXPECT_FALSE(source->SampleExistence({}, { &exists, 1 }));
        point.SetX(std::numeric_limits<float>::infinity());
        EXPECT_FALSE(source->SampleHeights({ &point, 1 }, { &height, 1 }));
        EXPECT_FALSE(source->SampleExistence({ &point, 1 }, { &exists, 1 }));
        point.SetX(1.0e13f);
        EXPECT_FALSE(source->SampleHeights({ &point, 1 }, { &height, 1 }));
        EXPECT_FLOAT_EQ(height, 123.0f);
        EXPECT_FALSE(exists);
    }

    TEST(TerrainProceduralSnapshotTests, RetainedValuesSurviveConcurrentInvalidationDestructionAndReconnection)
    {
        SnapshotTestSupport::Composition scene;
        const auto old = Acquire(scene.m_sourceId);
        const AZ::Vector3 point(75, 91, 0);
        const float expected = old->m_heightValue(point);
        std::atomic_bool correct{ true };
        std::thread reader([&]
        {
            for (int i = 0; i < 500; ++i)
                if (old->m_heightValue(point) != expected || !old->m_existenceValue(point)) correct = false;
        });
        scene.m_config.m_amplitudeMeters = 0.0f;
        scene.m_source->ReadInConfig(&scene.m_config);
        const auto updated = Acquire(scene.m_sourceId);
        EXPECT_FALSE(Current(old));
        EXPECT_TRUE(Current(updated));
        EXPECT_EQ(old->m_session, updated->m_session);
        EXPECT_LT(old->m_ticket.m_revision, updated->m_ticket.m_revision);
        scene.StopSource();
        scene.StartSource();
        reader.join();
        const auto reconnected = Acquire(scene.m_sourceId);
        EXPECT_TRUE(correct);
        EXPECT_FALSE(Current(updated));
        EXPECT_NE(reconnected->m_session, old->m_session);
        EXPECT_NE(reconnected->m_ticket.m_dependency, old->m_ticket.m_dependency);
        EXPECT_FLOAT_EQ(old->m_heightValue(point), expected);
        EXPECT_FLOAT_EQ(reconnected->m_heightValue(point), 0.5f);
    }

    TEST(TerrainProceduralSnapshotTests, InvalidHeightConfigurationAndConfiguredMasksHaveIndependentGuarantees)
    {
        SnapshotTestSupport::Composition scene;
        scene.m_config.m_hillDensity = std::numeric_limits<float>::quiet_NaN();
        scene.m_source->ReadInConfig(&scene.m_config);
        const auto invalid = Acquire(scene.m_sourceId);
        EXPECT_EQ(invalid->m_heightResult, TerrainSourceAcquisition::InvalidConfiguration);
        EXPECT_EQ(invalid->m_existenceResult, TerrainSourceAcquisition::Acquired);
        scene.m_config.m_hillDensity = 0.005f;
        // Missing, self-referencing and invalid-threshold masks stay live, too.
        for (AZ::EntityId mask : { AZ::EntityId(99), scene.m_sourceId })
        {
            scene.m_config.m_holeMask.m_gradientId = mask;
            scene.m_config.m_holeThreshold = -1.0f;
            scene.m_source->ReadInConfig(&scene.m_config);
            const auto masked = Acquire(scene.m_sourceId);
            EXPECT_EQ(masked->m_heightResult, TerrainSourceAcquisition::Acquired);
            EXPECT_EQ(masked->m_existenceResult, TerrainSourceAcquisition::ExternalMask);
            EXPECT_FALSE(masked->m_existenceValue);
        }
    }

    TEST(TerrainProceduralSnapshotTests, CompositionOverlayMatchesLegacyForAllSamplersAndBatchBoundaries)
    {
        ProceduralGroundGradientConfig config;
        config.m_hillDensity = 0.05f;
        config.m_amplitudeMeters = 340.0f;
        SnapshotTestSupport::Composition scene(config);
        const auto publication = scene.Publication();
        const auto sources = scene.Capture();
        ASSERT_EQ(sources->m_queries.size(), 1);
        using Sampler = AzFramework::Terrain::TerrainDataRequests::Sampler;
        for (auto sampler : { Sampler::EXACT, Sampler::CLAMP, Sampler::BILINEAR })
            for (bool batch : { false, true })
                for (size_t count : { 0, 1, 255, 256, 257 })
                {
                    AZStd::vector<AZ::Vector3> points;
                    for (size_t i = 0; i < count; ++i) points.emplace_back(float(i) - 128.0f, 5.0f, -4000.0f);
                    AZStd::vector<float> expected(count), actual(count);
                    auto expectedExists = std::make_unique<bool[]>(count);
                    auto actualExists = std::make_unique<bool[]>(count);
                    ApplyTerrainRenderGeometry(*publication, points, expected, { expectedExists.get(), count });
                    TerrainRenderQueryRequest request;
                    request.m_positions = points;
                    request.m_sampler = sampler;
                    request.m_allowBatch = batch;
                    TerrainRenderQueryStatistics stats;
                    const auto plan = ResolveTerrainRenderQuery(publication, request, &stats, sources);
                    EXPECT_TRUE(plan.RequiresOrdinaryResults());
                    ExecuteTerrainRenderQuery(plan, actual, { actualExists.get(), count }, &stats);
                    EXPECT_EQ(actual, expected);
                    for (size_t i = 0; i < count; ++i) EXPECT_EQ(actualExists[i], expectedExists[i]);
                    EXPECT_EQ(stats.m_heightSnapshotSamples, count);
                    EXPECT_EQ(stats.m_existenceSnapshotSamples, count);
                    EXPECT_EQ(stats.m_heightSourceCalls + stats.m_existenceSourceCalls + stats.m_hierarchySourceCalls, 0);
                }
    }

    namespace SnapshotMasks
    {
        class ZMask : public GradientSignal::GradientRequestBus::Handler
        {
        public:
            explicit ZMask(AZ::EntityId id) { BusConnect(id); }
            ~ZMask() override { BusDisconnect(); }
            float GetValue(const GradientSignal::GradientSampleParams& params) const override { return params.m_position.GetZ() < 0 ? 1.0f : 0.0f; }
        };
        class Provider : public GradientSignal::GradientRequestBus::Handler, public TerrainProceduralSnapshotRequestBus::Handler
        {
        public:
            Provider(AZ::EntityId id, bool optIn)
            {
                GradientSignal::GradientRequestBus::Handler::BusConnect(id);
                if (optIn) TerrainProceduralSnapshotRequestBus::Handler::BusConnect(id);
            }
            ~Provider() override
            {
                TerrainProceduralSnapshotRequestBus::Handler::BusDisconnect();
                GradientSignal::GradientRequestBus::Handler::BusDisconnect();
            }
            float GetValue(const GradientSignal::GradientSampleParams&) const override
            {
                if (m_onValue) m_onValue();
                return 0.75f;
            }
            bool IsEntityInHierarchy(const AZ::EntityId&) const override { return m_cyclic; }
            TerrainProceduralSnapshotPtr AcquireTerrainSnapshot() const override
            {
                if (m_onAcquire) m_onAcquire();
                return m_snapshot;
            }
            bool m_cyclic = false;
            AZStd::function<void()> m_onValue, m_onAcquire;
            TerrainProceduralSnapshotPtr m_snapshot;
        };
    }
    TEST(TerrainProceduralSnapshotTests, ZDependentMaskRetainsOrdinaryCollisionFallbackZ)
    {
        const AZ::EntityId maskId(902008);
        SnapshotMasks::ZMask mask(maskId);
        ProceduralGroundGradientConfig config;
        config.m_holeMask.m_gradientId = maskId;
        SnapshotTestSupport::Composition scene(config);
        const auto sources = scene.Capture();
        ASSERT_EQ(sources->m_queries.size(), 1);
        const auto& query = sources->m_queries.front();
        EXPECT_EQ(query.m_capability.m_height.m_source, TerrainRenderSource::RetainedAvailable);
        EXPECT_EQ(query.m_capability.m_existence.m_source, TerrainRenderSource::Live);
        EXPECT_EQ(query.m_sourceProvenance.m_existence, TerrainSourceAcquisition::ExternalMask);
        const AZStd::array points{ AZ::Vector3(10, 10, -1024), AZ::Vector3(10, 10, 50) };
        for (bool batch : { false, true })
        {
            AZStd::array<float, 2> heights{};
            AZStd::array<bool, 2> exists{};
            TerrainRenderQueryRequest request;
            request.m_positions = points;
            request.m_allowBatch = batch;
            TerrainRenderQueryStatistics stats;
            const auto plan = ResolveTerrainRenderQuery(scene.Publication(), request, &stats, sources);
            ExecuteTerrainRenderQuery(plan, heights, exists, &stats);
            EXPECT_FALSE(exists[0]);
            EXPECT_TRUE(exists[1]);
            EXPECT_FLOAT_EQ(heights[0], heights[1]);
            EXPECT_EQ(stats.m_heightSnapshotSamples, 2);
            EXPECT_EQ(stats.m_existenceSnapshotSamples, 0);
            EXPECT_GT(stats.m_existenceSourceCalls, 0);
        }
    }

    TEST(TerrainProceduralSnapshotTests, MissingUnsupportedCyclicRejectedAndReentrantAcquisitionPreserveFallback)
    {
        SnapshotTestSupport::Composition scene({}, false);
        const auto reason = [&] { return scene.Capture()->m_queries.front().m_sourceProvenance.m_height; };
        EXPECT_EQ(reason(), TerrainSourceAcquisition::MissingProvider);
        {
            SnapshotMasks::Provider legacy(scene.m_sourceId, false);
            EXPECT_EQ(reason(), TerrainSourceAcquisition::UnsupportedProvider);
            EXPECT_FLOAT_EQ(scene.Capture()->m_queries.front().m_getHeight(AZ::Vector3::CreateZero()), 512.0f);
        }
        SnapshotMasks::Provider provider(scene.m_sourceId, true);
        EXPECT_EQ(reason(), TerrainSourceAcquisition::Rejected);
        auto malformed = std::make_shared<TerrainProceduralSnapshot>();
        malformed->m_entityId = scene.m_sourceId;
        provider.m_snapshot = malformed;
        EXPECT_EQ(reason(), TerrainSourceAcquisition::Rejected);
        provider.m_cyclic = true;
        EXPECT_EQ(reason(), TerrainSourceAcquisition::Cyclic);
        EXPECT_FLOAT_EQ(scene.Capture()->m_queries.front().m_getHeight(AZ::Vector3::CreateZero()), -1024.0f);
        provider.m_cyclic = false;
        provider.m_onValue = [&] { EXPECT_EQ(reason(), TerrainSourceAcquisition::Reentrant); };
        float value = 0.0f;
        GradientSignal::GradientRequestBus::EventResult(value, scene.m_sourceId, &GradientSignal::GradientRequests::GetValue,
            GradientSignal::GradientSampleParams(AZ::Vector3::CreateZero()));
        provider.m_onAcquire = [&] { EXPECT_EQ(reason(), TerrainSourceAcquisition::Reentrant); };
        EXPECT_EQ(reason(), TerrainSourceAcquisition::Rejected);
    }

    TEST(TerrainProceduralSnapshotTests, InvalidCompositionIdentityDoesNotAcquire)
    {
        SnapshotTestSupport::Composition scene;
        for (AZ::EntityId id : { AZ::EntityId{}, scene.m_owner, scene.m_region })
        {
            TerrainCompositionConfig config;
            config.m_proceduralSourceEntityId = id;
            config.m_targetTerrainRegionEntityId = scene.m_region;
            AZ_TEST_START_TRACE_SUPPRESSION;
            scene.m_composition->ReadInConfig(&config);
            AZ_TEST_STOP_TRACE_SUPPRESSION_NO_COUNT; // The configuration warning is deduplicated by its call site.
            const auto sources = scene.Capture();
            ASSERT_FALSE(sources->m_queries.empty());
            EXPECT_EQ(sources->m_queries.front().m_sourceProvenance.m_height, TerrainSourceAcquisition::InvalidIdentity);
            EXPECT_FALSE(sources->m_queries.front().m_proceduralSnapshot);
        }
    }

    TEST(TerrainProceduralSnapshotTests, PreOrdinaryGridClassificationRequiresFullChannelCoverage)
    {
        SnapshotTestSupport::Composition scene;
        const AZStd::array points{ AZ::Vector3(0, 0, 0), AZ::Vector3(1, 0, 0), AZ::Vector3(0, 1, 0), AZ::Vector3(1, 1, 0) };
        TerrainRenderQueryRequest request;
        request.m_positions = points;
        request.m_coordinates = TerrainRenderCoordinates::WorldXY;
        request.m_grid = TerrainRenderGrid::Regular;
        request.m_gridWidth = request.m_gridHeight = 2;
        request.m_gridSpacing = AZ::Vector2(1, 1);
        const auto sources = scene.Capture();
        TerrainRenderQueryStatistics stats;
        const auto plan = ResolveTerrainRenderQuery(scene.Publication(), request, &stats, sources);
        EXPECT_EQ(stats.m_independentSamples, 4);
        EXPECT_EQ(plan.m_firstRun.m_fallbackReasons, TerrainRenderFallbackBit(TerrainRenderFallback::PreservedPolicy));
        EXPECT_TRUE(plan.RequiresOrdinaryResults());
        request.m_gridWidth = 3;
        const auto incomplete = ResolveTerrainRenderQuery(scene.Publication(), request, nullptr, sources);
        EXPECT_NE(incomplete.m_firstRun.m_fallbackReasons & TerrainRenderFallbackBit(TerrainRenderFallback::UnsupportedRequest), 0);
        request.m_gridWidth = 2;
        scene.m_config.m_holeMask.m_gradientId = AZ::EntityId(12345);
        scene.m_source->ReadInConfig(&scene.m_config);
        TerrainRenderQueryStatistics maskedStats;
        ResolveTerrainRenderQuery(scene.Publication(), request, &maskedStats, scene.Capture());
        EXPECT_EQ(maskedStats.m_independentSamples, 0);
        EXPECT_EQ(maskedStats.m_heightSnapshotSamples, 4);
    }

    TEST(TerrainProceduralSnapshotTests, UnsupportedSnapshotSamplerOrSampleFormUsesOriginalDispatch)
    {
        ProceduralGroundGradientConfig config;
        config.m_amplitudeMeters = 0.0f;
        SnapshotTestSupport::Composition scene(config);
        auto source = std::make_shared<TerrainProceduralSnapshot>(*Acquire(scene.m_sourceId));
        scene.StopSource();
        SnapshotMasks::Provider provider(scene.m_sourceId, true);
        const AZStd::array points{ AZ::Vector3(0, 0, 0), AZ::Vector3(1, 0, 0) };
        for (bool batch : { false, true })
        {
            source->m_height.m_sampling.m_exact = batch ? false : true;
            source->m_height.m_sampling.m_minSamples = batch ? 0 : 2;
            provider.m_snapshot = std::make_shared<const TerrainProceduralSnapshot>(*source);
            const auto sources = scene.Capture();
            TerrainRenderQueryRequest request;
            request.m_positions = points;
            request.m_allowBatch = batch;
            TerrainRenderQueryStatistics stats;
            const auto plan = ResolveTerrainRenderQuery(scene.Publication(), request, &stats, sources);
            AZStd::array<float, 2> heights{};
            AZStd::array<bool, 2> exists{};
            ExecuteTerrainRenderQuery(plan, heights, exists, &stats);
            EXPECT_FLOAT_EQ(heights[0], 512.0f); // Live .75, not retained .5.
            EXPECT_FLOAT_EQ(heights[1], 512.0f);
            EXPECT_EQ(stats.m_heightSnapshotSamples, 0);
            EXPECT_EQ(stats.m_batchCallbacks, batch ? 1 : 0);
            EXPECT_EQ(stats.m_scalarCallbacks, batch ? 0 : 4);
            EXPECT_NE(plan.m_firstRun.m_fallbackReasons & TerrainRenderFallbackBit(TerrainRenderFallback::UnsupportedRequest), 0);
        }
    }

    TEST(TerrainProceduralSnapshotTests, NormalizedSourceCannotDeclareFinalCompositionEquivalence)
    {
        SnapshotTestSupport::Composition scene;
        auto source = std::make_shared<TerrainProceduralSnapshot>(*Acquire(scene.m_sourceId));
        source->m_kernel = TerrainProceduralSnapshot::Kernel::Unspecified;
        source->m_height.m_ordinaryEquivalence = source->m_existence.m_ordinaryEquivalence = { true, true, true, true, true, true };
        scene.StopSource();
        SnapshotMasks::Provider provider(scene.m_sourceId, true);
        provider.m_snapshot = source;
        const auto sources = scene.Capture();
        ASSERT_TRUE(sources->m_queries.front().m_proceduralSnapshot);
        for (const auto& channel : { sources->m_queries.front().m_capability.m_height, sources->m_queries.front().m_capability.m_existence })
        {
            EXPECT_FALSE(channel.m_ordinaryEquivalence.m_coordinates);
            EXPECT_FALSE(channel.m_ordinaryEquivalence.m_exact);
            EXPECT_FALSE(channel.m_ordinaryEquivalence.m_clamp);
            EXPECT_FALSE(channel.m_ordinaryEquivalence.m_bilinear);
            EXPECT_FALSE(channel.m_ordinaryEquivalence.m_renderValue);
            EXPECT_FALSE(channel.m_ordinaryEquivalence.m_collisionFallback);
        }
    }

    TEST(TerrainProceduralSnapshotTests, InvalidOutputSpansAndMismatchedPublicationCannotExecuteRetainedSource)
    {
        SnapshotTestSupport::Composition scene;
        const AZ::Vector3 point(0, 0, 0);
        const auto publication = scene.Publication();
        const auto sources = scene.Capture();
        TerrainRenderQueryRequest request;
        request.m_positions = { &point, 1 };
        const auto plan = ResolveTerrainRenderQuery(publication, request, nullptr, sources);
        bool exists = false;
        AZ_TEST_START_TRACE_SUPPRESSION;
        ExecuteTerrainRenderQuery(plan, {}, { &exists, 1 });
        AZ_TEST_STOP_TRACE_SUPPRESSION(1);
        EXPECT_FALSE(exists);
        ASSERT_TRUE(scene.AddImageHole());
        AZ_TEST_START_TRACE_SUPPRESSION;
        const auto wrong = ResolveTerrainRenderQuery(scene.Publication(), request, nullptr, sources);
        AZ_TEST_STOP_TRACE_SUPPRESSION(1);
        EXPECT_EQ(wrong.m_firstRun.m_count, 0);
    }

    TEST(TerrainProceduralSnapshotTests, ImageTopologyAndPublicationReplacementRemainIndependentOfRetainedSource)
    {
        SnapshotTestSupport::Composition scene;
        const auto oldPublication = scene.Publication();
        const auto oldSources = scene.Capture();
        ASSERT_TRUE(scene.AddImageHole());
        const auto sources = scene.Capture();
        const AZ::Vector3 center(0, 0, -4000);
        EXPECT_TRUE(oldSources->m_queries.front().m_getTerrainExists(center));
        EXPECT_FALSE(sources->m_queries.front().m_getTerrainExists(center));
        EXPECT_TRUE(sources->m_queries.front().m_getTerrainExists(AZ::Vector3(2, 2, -4000)));
        EXPECT_NE(scene.Publication(), oldPublication);
        EXPECT_EQ(oldSources->m_queries.front().m_proceduralSnapshot->m_session, sources->m_queries.front().m_proceduralSnapshot->m_session);
        EXPECT_TRUE(Current(oldSources->m_queries.front().m_proceduralSnapshot));
        TerrainRenderQueryStatistics stats;
        RecordTerrainRenderSourceAcquisitions(sources, &stats);
        ASSERT_EQ(stats.m_sources.size(), 1);
        EXPECT_EQ(stats.m_sources.front().m_entityId, scene.m_sourceId);
        EXPECT_EQ(stats.m_sourceAcquisitions[static_cast<size_t>(TerrainSourceAcquisition::Acquired)], 2);
    }
}
