#include "ProceduralHillReference.h"
#include "ProceduralSnapshotTestSupport.h"
#include <TerrainCompositor/TerrainBatchCandidates.h>
#include <TerrainCompositor/TerrainSectorSampleReuse.h>
#include <cstring>

namespace TerrainCompositor
{
    TEST(ProceduralHillKernelTests, FractionalFrequencySweepMatchesReference)
    {
        for (int step = 1; step <= 160; ++step)
        {
            ProceduralGroundGradientConfig config;
            config.m_hillDensity = 0.73f;
            config.m_amplitudeMeters = 1024;
            config.m_frequency = float(step) * 0.1f;
            ProceduralHillKernel kernel(config.m_hillDensity, config.m_amplitudeMeters, config.m_frequency);
            ProceduralHillKernel::Scratch scratch;
            for (int i = 0; i < 1025; ++i)
            {
                const AZ::Vector3 point(float(i % 31) * 0.1329f - 2.125f, float(i / 31) * 0.771f - 3.3f, 0);
                const float expected = TestSupport::HillReference::Evaluate(point, config);
                const float actual = kernel.Sample(point.GetX(), point.GetY(), scratch);
                ASSERT_EQ(std::memcmp(&expected, &actual, sizeof(float)), 0);
            }
        }
    }

    TEST(ProceduralHillKernelTests, CachedKernelMatchesOriginalComponentReference)
    {
        AZStd::vector<AZ::Vector3> positions;
        for (float origin : { -8192.03f, -1.125f, 0.0f, 1000000.25f, -1.0e12f, 1.0e12f, -1.0e18f, 1.0e18f })
            for (int i = 0; i < 513; ++i)
                positions.emplace_back(origin + float(i % 31) * 0.137f, origin - float(i / 31) * 0.379f, 999.0f);
        for (float density : { -1.0f, 0.0f, 0.000001f, 0.0099f, 0.3f, 1.0f, 2.0f })
            for (float frequency : { -1.0f, 0.1f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 7.3f, 16.0f, 20.0f })
                for (float amplitude : { 0.0f, 3.0f, 1024.0f, 2048.0f })
                {
                    ProceduralGroundGradientConfig config;
                    config.m_hillDensity = density; config.m_frequency = frequency; config.m_amplitudeMeters = amplitude;
                    ProceduralHillKernel cached(density, amplitude, frequency);
                    ProceduralHillKernel::Scratch b;
                    for (const auto& p : positions)
                    {
                        const float expected = TestSupport::HillReference::Evaluate(p, config);
                        const float actual = cached.Sample(p.GetX(), p.GetY(), b);
                        ASSERT_EQ(std::memcmp(&expected, &actual, sizeof(float)), 0)
                            << density << "," << frequency << "," << amplitude << "," << p.GetX();
                        ASSERT_TRUE(std::isfinite(actual));
                        ASSERT_GE(actual, 0); ASSERT_LE(actual, 1);
                    }
                }
    }

    TEST(ProceduralHillKernelTests, CentersSupportRimsAndCacheEvictionsPreserveBits)
    {
        for (float frequency : { 0.1f, 0.2f, 0.75f, 3.0f, 15.9f, 16.0f })
        {
            ProceduralHillKernel reference(1, 1024, frequency, ProceduralHillPolicy::Reference), cached(1, 1024, frequency);
            ProceduralHillKernel::Scratch a, b;
            for (int cell = -100; cell <= 100; ++cell)
            {
                const auto center = ProceduralHillKernel::MakeCell(cell, -cell);
                for (float angle : { 0.0f, 0.37f, 1.5707963f, 3.1415927f })
                    for (int rim = -256; rim <= 256; ++rim)
                    {
                        const float radius = rim == -256 ? 0 : ProceduralHillKernel::Radius + float(rim) * 1.0e-7f;
                        const float x = center.m_x + radius * std::cos(angle), y = center.m_y + radius * std::sin(angle);
                        const float expected = reference.Sample(x, y, a), actual = cached.Sample(x, y, b);
                        ASSERT_EQ(std::memcmp(&expected, &actual, sizeof(float)), 0);
                    }
            }
        }
    }

    TEST(ProceduralHillKernelTests, BatchOwnsCacheAndBypassesScalarAndConstantExistenceCallbacks)
    {
        SnapshotTestSupport::Composition scene;
        auto source = std::make_shared<TerrainProceduralSnapshot>(*SnapshotTestSupport::Acquire(scene.m_sourceId));
        source->m_heightValue = [](const auto&) { ADD_FAILURE(); return -99.0f; };
        source->m_existenceValue = [](const auto&) { ADD_FAILURE(); return false; };
        AZStd::vector<AZ::Vector3> positions;
        for (int i = 0; i < 1027; ++i) positions.emplace_back(float(i % 32) * 0.5f, float(i / 32) * 0.5f, 0.0f);
        AZStd::vector<float> heights(positions.size()), split(positions.size());
        auto exists = std::make_unique<bool[]>(positions.size());
        ProceduralHillCounters counters;
        ASSERT_TRUE(source->SampleHeights(positions, heights, &counters));
        ASSERT_TRUE(source->SampleExistence(positions, { exists.get(), positions.size() }));
        EXPECT_GT(counters.m_cellHits, counters.m_cells);
        EXPECT_LT(counters.m_sqrt, positions.size() * 9);
        EXPECT_EQ(counters.m_samples, positions.size());
        for (size_t start = 0; start < positions.size(); start += 7)
        {
            const size_t count = AZStd::min(size_t(7), positions.size() - start);
            ASSERT_TRUE(source->SampleHeights({ positions.data() + start, count }, { split.data() + start, count }));
        }
        EXPECT_EQ(std::memcmp(heights.data(), split.data(), heights.size() * sizeof(float)), 0);
        for (size_t i = 0; i < positions.size(); ++i) EXPECT_TRUE(exists[i]);
    }

    TEST(ProceduralHillKernelTests, ExperimentalPolicyInvalidatesAndIsSharedByOrdinaryAndRetainedConsumers)
    {
        ProceduralGroundGradientConfig config;
        config.m_frequency = 3;
        SnapshotTestSupport::Composition scene(config);
        const auto old = SnapshotTestSupport::Acquire(scene.m_sourceId);
        config.m_kernelPolicy = ProceduralHillPolicy::IntegerPowers;
        ASSERT_TRUE(scene.m_source->ReadInConfig(&config));
        EXPECT_FALSE(SnapshotTestSupport::Current(old));
        const auto current = SnapshotTestSupport::Acquire(scene.m_sourceId);
        EXPECT_EQ(current->m_hillKernel->Policy(), ProceduralHillPolicy::IntegerPowers);
        EXPECT_TRUE(SnapshotTestSupport::Current(current));
        AZStd::vector<AZ::Vector3> points{ AZ::Vector3(-1.125f, 6, 0), AZ::Vector3(300, -1024.5f, 7) };
        AZStd::vector<float> live(2), retained(2);
        GradientSignal::GradientRequestBus::Event(scene.m_sourceId, &GradientSignal::GradientRequests::GetValues, points, live);
        ASSERT_TRUE(current->SampleHeights(points, retained));
        EXPECT_EQ(std::memcmp(live.data(), retained.data(), sizeof(float) * 2), 0);
        EXPECT_EQ(old->m_hillKernel->Policy(), ProceduralHillPolicy::CachedExact);
    }

    TEST(ProceduralHillKernelTests, CandidateEdgesOrderInvalidBoundsAndCapacityAreConservative)
    {
        AZStd::vector<PreparedTerrainExistenceContributor> records;
        for (int i = 0; i < 70; ++i)
        {
            PreparedTerrainExistenceContributor item;
            item.m_imageMask = TestSupport::MakePreparedMask(1,
                i % 2 ? TerrainExistenceOperation::RestoreTerrain : TerrainExistenceOperation::RemoveTerrain);
            records.push_back(item);
        }
        AZStd::array<AZ::Vector3, 2> points{ AZ::Vector3(1, -1, -100), AZ::Vector3(1, 1, 100) };
        const auto bounds = TerrainBatchBounds(points);
        TerrainBatchCandidates<PreparedTerrainExistenceContributor> overflow(records, bounds);
        EXPECT_TRUE(overflow.m_full);
        records.resize(12);
        TerrainBatchCandidates<PreparedTerrainExistenceContributor> touching(records, bounds);
        EXPECT_FALSE(touching.m_full);
        ASSERT_EQ(touching.m_records.size(), 12);
        for (size_t i = 0; i < records.size(); ++i) EXPECT_EQ(touching.m_records[i], &records[i]);
        const auto far = AZ::Aabb::CreateFromMinMaxValues(2, 2, 0, 3, 3, 0);
        EXPECT_TRUE(TerrainBatchCandidates<PreparedTerrainExistenceContributor>(records, far).m_records.empty());
        records.front().m_imageMask.m_placement.m_worldBounds = AZ::Aabb::CreateNull();
        EXPECT_EQ(TerrainBatchCandidates<PreparedTerrainExistenceContributor>(records, far).m_records.size(), 1);
    }

    TEST(ProceduralHillKernelTests, OwnedQuerySurvivesPlanCopyAndAssessmentStorageDestruction)
    {
        SnapshotTestSupport::Composition scene;
        ASSERT_TRUE(scene.AddImageHole(16, true));
        ASSERT_TRUE(scene.AddMeshHeight());
        for (auto schedule : { TerrainSectorSchedulePolicy::Synchronous, TerrainSectorSchedulePolicy::Deferred })
        {
            TerrainSectorSamplingPlan copy;
            {
                TerrainSectorSamplingPlan plan;
                plan.m_publication = scene.Publication(); plan.m_sources = scene.Capture();
                plan.m_dependencies = plan.m_sources->m_dependencies;
                plan.m_area.m_captured = plan.m_area.m_exists = true;
                plan.m_regular = { AZ::Vector2(-4), 0.5f, 17, 17 };
                plan.m_clod = { AZ::Vector2(-4), 1, 9, 9 }; plan.m_clodEnabled = true;
                plan.m_schedulePolicy = schedule;
                plan.Assess();
                ASSERT_TRUE(plan.CanAvoidOrdinaryResults());
                ASSERT_TRUE(plan.m_regularQuery);
                copy = plan;
            }
            TerrainSectorSampleReuse reuse(copy);
            AZStd::vector<float> heights(copy.m_regular.Count());
            auto exists = std::make_unique<bool[]>(heights.size());
            TerrainRenderQueryStatistics statistics;
            reuse.Gather(copy.m_regular, heights, { exists.get(), heights.size() }, true, &statistics);
            EXPECT_EQ(statistics.m_resolutionMicroseconds, 0);
            AZStd::vector<AZ::Vector3> positions;
            for (size_t y = 0; y < copy.m_regular.Height(); ++y)
                for (size_t x = 0; x < copy.m_regular.Width(); ++x) positions.push_back(copy.m_regular.Position(x, y));
            AZStd::vector<float> reference(heights.size());
            auto expectedExists = std::make_unique<bool[]>(heights.size());
            auto request = copy.m_regular.Query(positions, true);
            request.m_coordinates = TerrainRenderCoordinates::WorldXY;
            ExecuteTerrainRenderQuery(ResolveTerrainRenderQuery(copy.m_publication, request, nullptr, copy.m_sources),
                reference, { expectedExists.get(), heights.size() });
            EXPECT_EQ(std::memcmp(heights.data(), reference.data(), heights.size() * sizeof(float)), 0);
            for (size_t i = 0; i < heights.size(); ++i) EXPECT_EQ(exists[i], expectedExists[i]);
        }
    }
}
