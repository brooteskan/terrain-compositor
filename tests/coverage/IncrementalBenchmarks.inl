void IncrementalBenchmarks()
{
    for (const bool full : { false, true })
        for (int workload = 0; workload < 6; ++workload)
        {
            std::vector<TerrainSectorCoverageClaim> claims;
            for (uint32_t lod = 0; lod < 7; ++lod)
                for (int32_t x = -5; x < 5; ++x)
                    for (int32_t y = -5; y < 5; ++y)
                        claims.push_back({x, y, lod, (x+y)%7 != 0, true, claims.size()+1, 1});
            TerrainSectorCoverageScratch scratch;
            std::vector<double> times(1000);
            const auto beforeBytes = Heap::live, beforeAllocations = Heap::allocations;
            Heap::peak = Heap::live;
            EvaluateTerrainSectorCoverageChanges(claims, 7, scratch, full);
            const auto retained = Heap::live - beforeBytes, coldAllocations = Heap::allocations - beforeAllocations;
            size_t nodes = 0, edges = 0, changes = 0, direct = 0, rebuilds = 0;
            const auto allocationStart = Heap::allocations;
            for (size_t step = 0; step < times.size(); ++step)
            {
                switch(workload)
                {
                case 1: claims[step % claims.size()].m_hasData ^= true; break;
                case 2: for (auto& claim : claims) claim.m_hasData ^= true; break;
                case 3: claims[step % claims.size()].m_known ^= true; break;
                case 4: claims[0].m_x = step % 2 ? -6 : -5; break;
                case 5: ++claims[step % claims.size()].m_resourceVersion; break;
                }
                const auto start = std::chrono::steady_clock::now();
                EvaluateTerrainSectorCoverageChanges(claims, 7, scratch, full);
                times[step] = std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
                const auto& stats = scratch.m_statistics;
                nodes += stats.m_visitedNodes; edges += stats.m_visitedEdges;
                changes += stats.m_emittedChanges; direct += stats.m_directlyChangedClaims;
                rebuilds += stats.m_rebuild != TerrainCoverageRebuild::None;
            }
            const auto warmAllocations = Heap::allocations - allocationStart, peak = Heap::peak - beforeBytes;
            std::sort(times.begin(), times.end());
            const char* workloads[] = {"unchanged","sparse-policy","dense-policy","eligibility","topology","resource"};
            std::printf("incremental,%s,%s,p50=%.3f,p95=%.3f,p99=%.3f,max=%.3f,direct=%zu,nodes=%zu,edges=%zu,changes=%zu,rebuilds=%zu,cold-allocations=%zu,warm-allocations=%zu,retained=%zu,peak=%zu,payload=%zu\n",
                full ? "full" : "delta", workloads[workload], times[500], times[950], times[990], times.back(),
                direct,nodes,edges,changes,rebuilds,coldAllocations,warmAllocations,retained,peak,scratch.m_statistics.m_retainedBytes);
        }
}


void SupersetBenchmarks()
{
    for (const bool compact : {false, true})
    {
        std::vector<TerrainSectorCoverageClaim> slots, input;
        for (uint32_t lod=0; lod<7; ++lod)
            for (int32_t x=-5; x<5; ++x)
                for (int32_t y=-5; y<5; ++y)
                    slots.push_back({x,y,lod,true,slots.size()%3==0,slots.size()+1,1});
        input.reserve(slots.size());
        TerrainSectorCoverageScratch scratch;
        std::vector<double> times(1000);
        const auto run = [&]()
        {
            input.clear();
            for (const auto& slot : slots) if (!compact || slot.m_known) input.push_back(slot);
            EvaluateTerrainSectorCoverageChanges(input,7,scratch);
        };
        const auto memoryStart=Heap::live;
        Heap::peak=Heap::live;
        run();
        const auto coldRetained=Heap::live-memoryStart;
        size_t nodes=0,rebuilds=0,changes=0;
        const auto allocationStart=Heap::allocations;
        for (size_t step=0;step<times.size();++step)
        {
            slots[step%slots.size()].m_known ^= true;
            const auto start=std::chrono::steady_clock::now();
            run();
            times[step]=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
            nodes+=scratch.m_statistics.m_visitedNodes;
            rebuilds+=scratch.m_statistics.m_rebuild!=TerrainCoverageRebuild::None;
            changes+=scratch.m_statistics.m_emittedChanges;
        }
        const auto allocations=Heap::allocations-allocationStart, retained=Heap::live-memoryStart, peak=Heap::peak-memoryStart;
        std::sort(times.begin(),times.end());
        std::printf("superset,%s,p50=%.3f,p95=%.3f,p99=%.3f,max=%.3f,nodes=%zu,rebuilds=%zu,changes=%zu,allocations=%zu,cold-retained=%zu,retained=%zu,peak=%zu\n",
            compact?"eligible-only":"bounded",times[500],times[950],times[990],times.back(),nodes,rebuilds,changes,allocations,coldRetained,retained,peak);
    }
}
