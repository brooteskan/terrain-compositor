#include <AzTest/AzTest.h>
#include <TerrainCompositor/TerrainSectorSampleReuse.h>
#include <TerrainCompositor/TerrainSectorRecovery.h>
#include <map>
#include "ProceduralSnapshotTestSupport.h"

namespace TerrainCompositor
{
    TEST(TerrainSectorRecoveryTests, SharedRegularAndClodHalosMatchAcrossIndependentCertifiedCaptures)
    {
        SnapshotTestSupport::Composition scene;
        ASSERT_TRUE(scene.AddImageHole(16, true));
        ASSERT_TRUE(scene.AddMeshHeight());
        const auto channel = scene.Channel();
        std::map<std::pair<float, float>, std::pair<float, bool>> observed;
        size_t compared = 0;
        for (float startX : { -4.0f, 0.0f })
        {
            TerrainSectorSamplingPlan plan;
            plan.m_publication = channel->m_snapshot.load();
            plan.m_sources = CaptureTerrainRenderQuerySources(plan.m_publication);
            plan.m_channel = channel;
            plan.m_dependencies = plan.m_sources->m_dependencies;
            plan.m_area.m_captured = plan.m_area.m_exists = plan.m_area.m_retainedAcrossFrames = true;
            plan.m_queryPolicy = TerrainSectorQueryPolicy::RetainedOnly;
            plan.m_schedulePolicy = TerrainSectorSchedulePolicy::Deferred;
            plan.m_regular = { AZ::Vector2(startX, -4), 0.5f, 9, 9 };
            plan.m_clod = { plan.m_regular.m_start, 1, 5, 5 };
            plan.m_clodEnabled = true;
            plan.Assess();
            ASSERT_TRUE(CertifyTerrainSectorSampling(plan).m_proven);
            for (const auto& layout : { plan.m_regular, plan.m_clod })
            {
                AZStd::vector<AZ::Vector3> positions;
                for (size_t y = 0; y < layout.Height(); ++y)
                    for (size_t x = 0; x < layout.Width(); ++x) positions.push_back(layout.Position(x, y));
                AZStd::vector<float> heights(layout.Count(), 0);
                auto exists = std::make_unique<bool[]>(layout.Count());
                auto query = layout.Query(positions, true);
                query.m_coordinates = TerrainRenderCoordinates::WorldXY;
                ExecuteTerrainRenderQuery(ResolveTerrainRenderQuery(plan.m_publication, query, nullptr, plan.m_sources),
                    heights, { exists.get(), layout.Count() });
                for (size_t i = 0; i < layout.Count(); ++i)
                {
                    const auto [entry, inserted] = observed.emplace(
                        std::pair{ positions[i].GetX(), positions[i].GetY() }, std::pair{ heights[i], exists[i] });
                    if (!inserted)
                    {
                        ++compared;
                        EXPECT_EQ(std::memcmp(&entry->second.first, &heights[i], sizeof(float)), 0);
                        EXPECT_EQ(entry->second.second, exists[i]);
                    }
                }
            }
            auto undeclared = std::make_shared<TerrainRenderQuerySources>(*plan.m_sources);
            undeclared->m_queries.front().m_capability.m_replacementSampling = false;
            plan.m_sources = undeclared;
            EXPECT_FALSE(CertifyTerrainSectorSampling(plan).m_proven);
        }
        EXPECT_GT(compared, 50);
    }

    TEST(TerrainSectorRecoveryTests, ExactLatticeRejectsFractionalOriginsSpacingsAndUnrepresentableCoordinates)
    {
        TerrainSectorSamplingLayout layout{ AZ::Vector2(-64, -128), 0.5f, 129, 129 };
        EXPECT_TRUE(IsExactTerrainSectorLattice(layout));
        layout.m_start.SetX(-63.9f);
        EXPECT_FALSE(IsExactTerrainSectorLattice(layout));
        layout.m_start.SetX(0);
        layout.m_spacing = 0.1f;
        EXPECT_FALSE(IsExactTerrainSectorLattice(layout));
        layout.m_spacing = 0.5f;
        layout.m_start.SetX(4194304.0f);
        EXPECT_FALSE(IsExactTerrainSectorLattice(layout));
        layout.m_start.SetX(0);
        layout.m_samplesX = 0;
        EXPECT_FALSE(IsExactTerrainSectorLattice(layout));
    }

    TEST(TerrainSectorSampleReuseTests, RawChannelsMatchIndependentFullGridsIncludingEveryHalo)
    {
        SnapshotTestSupport::Composition scene;
        ASSERT_TRUE(scene.AddImageHole(16, true));
        ASSERT_TRUE(scene.AddMeshHeight());
        auto channel = scene.Channel();
        for (float spacing : { 0.1f, 0.5f, 1.3f })
            for (float start : { -63.9f, -0.125f, 8192.03f })
            {
                TerrainSectorSamplingPlan plan;
                plan.m_publication = channel->m_snapshot.load();
                plan.m_sources = CaptureTerrainRenderQuerySources(plan.m_publication);
                plan.m_channel = channel;
                plan.m_dependencies = plan.m_sources->m_dependencies;
                plan.m_area.m_captured = plan.m_area.m_exists = true;
                plan.m_regular = { AZ::Vector2(start, -0.125f), spacing, 129, 129 };
                plan.m_clod = { plan.m_regular.m_start, spacing * 2, 65, 65 };
                plan.m_clodEnabled = true;
                plan.Assess();
                ASSERT_TRUE(TerrainSectorSampleReuse::Supported(plan));
                TerrainSectorSampleReuse reuse(plan);
                TerrainRenderQueryStatistics statistics;
                for (const auto layout : { plan.m_regular, plan.m_clod })
                {
                    AZStd::vector<AZ::Vector3> positions;
                    for (size_t y = 0; y < layout.Height(); ++y)
                        for (size_t x = 0; x < layout.Width(); ++x) positions.push_back(layout.Position(x, y));
                    AZStd::vector<float> expected(layout.Count(), 0.0f), actual(layout.Count(), 0.0f);
                    auto expectedExists = std::make_unique<bool[]>(layout.Count());
                    auto actualExists = std::make_unique<bool[]>(layout.Count());
                    auto query = layout.Query(positions, true);
                    query.m_coordinates = TerrainRenderCoordinates::WorldXY;
                    ExecuteTerrainRenderQuery(ResolveTerrainRenderQuery(plan.m_publication, query, nullptr, plan.m_sources),
                        expected, { expectedExists.get(), layout.Count() });
                    reuse.Gather(layout, actual, { actualExists.get(), layout.Count() }, true, &statistics);
                    for (size_t i = 0; i < layout.Count(); ++i)
                    {
                        EXPECT_EQ(std::memcmp(&expected[i], &actual[i], sizeof(float)), 0);
                        EXPECT_EQ(expectedExists[i], actualExists[i]);
                    }
                }
                EXPECT_EQ(statistics.m_requestedSamples, 21650);
                EXPECT_EQ(statistics.m_evaluatedSamples + statistics.m_reusedSamples, statistics.m_requestedSamples);
                if (spacing == 0.5f)
                {
                    EXPECT_EQ(statistics.m_reusedSamples, 4225);
                    EXPECT_EQ(statistics.m_evaluatedSamples, 17425);
                }
            }
    }
    TEST(TerrainSectorSampleReuseTests, UnknownPointwiseOrSubsetSemanticsDoNotEnableReuse)
    {
        TerrainSectorSamplingPlan plan;
        plan.m_assessed = true;
        plan.m_regular = { AZ::Vector2(0), 1, 3, 3 };
        plan.m_clod = { AZ::Vector2(0), 2, 2, 2 };
        auto publication = std::make_shared<TerrainMeshCutoutRenderSnapshot>();
        auto sources = std::make_shared<TerrainRenderQuerySources>();
        sources->m_publication = publication;
        sources->m_queries.resize(1);
        auto& capability = sources->m_queries.front().m_capability;
        capability.m_height.m_source = capability.m_existence.m_source = TerrainRenderSource::RetainedAvailable;
        capability.m_acceptsExplicitPositions = true;
        capability.m_height.m_sampling.m_explicitPositions = capability.m_existence.m_sampling.m_explicitPositions = true;
        plan.m_publication = publication;
        plan.m_sources = sources;
        EXPECT_FALSE(TerrainSectorSampleReuse::Supported(plan));
        capability.m_pointwise = true;
        EXPECT_TRUE(TerrainSectorSampleReuse::Supported(plan));
        capability.m_height.m_sampling.m_minSamples = 2;
        EXPECT_FALSE(TerrainSectorSampleReuse::Supported(plan));
        capability.m_height.m_sampling.m_minSamples = 0;
        plan.m_clod.m_sampler = AzFramework::Terrain::TerrainDataRequests::Sampler::EXACT;
        EXPECT_FALSE(TerrainSectorSampleReuse::Supported(plan));
    }
}
