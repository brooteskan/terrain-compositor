// Included after the renderer's friend fixture. Uses actual packing/CLOD/RT code.
void TerrainSectorLifetimeTests::MeasureProceduralHillKernels()
{
    using namespace TerrainCompositor;
    using Clock = std::chrono::steady_clock;
    ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
    SupplyTerrain(terrain);
    m_manager->m_gridSize = 128;
    m_manager->m_gridVerts1D = 129;
    m_manager->m_gridVerts2D = 129 * 129;
    m_manager->m_sampleSpacing = 0.5f;
    m_manager->m_worldHeightBounds = { -1024, 1024 };
    m_manager->m_vertexOrder.clear(); m_manager->m_xyPositions.clear();
    for (uint16_t y = 0; y < 129; ++y)
        for (uint16_t x = 0; x < 129; ++x)
        {
            m_manager->m_vertexOrder.push_back(y * 129 + x);
            m_manager->m_xyPositions.push_back({ uint8_t(x), uint8_t(y) });
        }
    m_manager->m_sectorLods[0].m_sectors[0].m_committed.m_rtData = AZStd::make_unique<Manager::RtSector>();
    ProceduralGroundGradientConfig config;
    config.m_hillDensity = 0.0099f; config.m_frequency = 3; config.m_amplitudeMeters = 3;
    SnapshotTestSupport::Composition scene(config);
    ASSERT_TRUE(scene.AddImageHole(16, true));
    ASSERT_TRUE(scene.AddMeshHeight());
    m_channel = scene.Channel();
    Result reference;
    for (auto policy : { ProceduralHillPolicy::Reference, ProceduralHillPolicy::PrunedReference,
             ProceduralHillPolicy::CachedExact, ProceduralHillPolicy::IntegerPowers })
        for (bool reuse : { false, true })
            for (auto schedule : { TerrainSectorSchedulePolicy::Synchronous, TerrainSectorSchedulePolicy::Deferred })
            {
                config.m_kernelPolicy = policy;
                ASSERT_TRUE(scene.m_source->ReadInConfig(&config));
                AZStd::vector<double> elapsed;
                const auto capture = [&](bool statistics)
                {
                    auto request = Capture();
                    auto settings = std::make_shared<Manager::SectorPreparationSettings>(*request.m_data.m_settings);
                    settings->m_batchQueries = settings->m_retainedOnlyQueries = true;
                    settings->m_sampleReuse = reuse;
                    request.m_data.m_settings = settings;
                    request.m_collectStatistics = statistics;
                    request.m_samplingPlan.m_schedulePolicy = schedule;
                    request.m_samplingPlan.m_queryPolicy = TerrainSectorQueryPolicy::RetainedOnly;
                    request.m_samplingPlan.m_area.m_retainedAcrossFrames = true;
                    return request;
                };
                for (int repeat = 0; repeat < 9; ++repeat)
                {
                    auto request = capture(false);
                    const auto start = Clock::now();
                    auto result = Manager::PrepareSector(AZStd::move(request));
                    const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
                    ASSERT_EQ(result.m_status, Status::Ready);
                    if (repeat) elapsed.push_back(us);
                    if (policy == ProceduralHillPolicy::Reference && !reuse && schedule == TerrainSectorSchedulePolicy::Synchronous && !repeat)
                        reference = AZStd::move(result);
                    else
                    {
                        ASSERT_EQ(result.m_heights.size(), reference.m_heights.size());
                        ASSERT_EQ(result.m_lodHeights.size(), reference.m_lodHeights.size());
                        for (size_t i = 0; i < result.m_heights.size(); ++i)
                        {
                            if (policy != ProceduralHillPolicy::IntegerPowers)
                            {
                                ASSERT_EQ(result.m_heights[i].m_height, reference.m_heights[i].m_height);
                                ASSERT_EQ(result.m_heights[i].m_normal, reference.m_heights[i].m_normal);
                                ASSERT_EQ(result.m_lodHeights[i].m_height, reference.m_lodHeights[i].m_height);
                                ASSERT_EQ(result.m_lodHeights[i].m_normal, reference.m_lodHeights[i].m_normal);
                                ASSERT_EQ(result.m_rtPositions[i].z, reference.m_rtPositions[i].z);
                                ASSERT_EQ(result.m_rtNormals[i].x, reference.m_rtNormals[i].x);
                                ASSERT_EQ(result.m_rtNormals[i].y, reference.m_rtNormals[i].y);
                                ASSERT_EQ(result.m_rtNormals[i].z, reference.m_rtNormals[i].z);
                            }
                        }
                    }
                }
                AZStd::sort(elapsed.begin(), elapsed.end());
                const auto measured = Manager::PrepareSector(capture(true));
                ASSERT_EQ(measured.m_status, Status::Ready);
                const auto& s = *measured.m_queryStatistics;
                std::printf(
                    "TerrainKernelSectorBenchmark policy=%d reuse=%d deferred=%d median-us=%.3f source-us=%.3f composition-us=%.3f ownership-us=%.3f coordinates-us=%.3f unique=%zu cells=%zu hits=%zu sqrt=%zu pow=%zu scratch=%zu\n",
                    int(policy), reuse, schedule == TerrainSectorSchedulePolicy::Deferred, elapsed[elapsed.size() / 2],
                    s.m_sourceHeightMicroseconds, s.m_compositionMicroseconds, s.m_resolutionMicroseconds,
                    s.m_coordinateMicroseconds, s.m_kernelSamples, s.m_kernelCells, s.m_kernelCellHits, s.m_kernelSqrt, s.m_kernelPow,
                    s.m_kernelScratchBytes + s.m_reuseScratchBytes);
            }
}

TEST_F(TerrainSectorLifetimeTests, DISABLED_MeasureProceduralHillKernels)
{
    MeasureProceduralHillKernels();
}
