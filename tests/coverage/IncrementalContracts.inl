
TerrainSectorCoverageSelection KnownOracle(std::span<const TerrainSectorCoverageClaim> claims, size_t lodCount, bool flat)
{
    std::vector<TerrainSectorCoverageClaim> known;
    std::vector<size_t> indices;
    for (size_t i = 0; i < claims.size(); ++i)
        if (claims[i].m_known) { known.push_back(claims[i]); indices.push_back(i); }
    FlatScratch scratch;
    auto output = flat ? ReferenceFlatCoverage(known, lodCount, scratch) : ReferenceTerrainCoverage(known, lodCount);
    for (auto& draw : output.m_draws)
    {
        draw.m_claim = indices[draw.m_claim];
        draw.m_destination = claims[draw.m_claim].m_destination;
        draw.m_resourceVersion = claims[draw.m_claim].m_resourceVersion;
    }
    return output;
}

struct DeltaConsumer
{
    uint64_t generation = 0, plan = 0;
    std::vector<TerrainSectorCoverageSelection::Draw> slots;
    bool Apply(const TerrainCoverageChangeBatch& batch)
    {
        if (!CanApplyTerrainCoverageBatch(batch, generation, plan)) return false;
        if (batch.m_reset) slots.assign(batch.m_nodeCount, {});
        for (const auto& change : batch.m_changes)
        {
            Check(change.m_order < slots.size(), "delta order outside topology");
            if (!batch.m_reset) Check(slots[change.m_order] == change.m_before, "delta has wrong resource baseline");
            slots[change.m_order] = change.m_after;
        }
        generation = batch.m_result; plan = batch.m_plan;
        return true;
    }
    void EqualDraws(const TerrainSectorCoverageSelection& expected)
    {
        size_t index = 0;
        for (const auto& draw : slots)
            if (draw.m_quadrants)
            {
                Check(index < expected.m_draws.size(), "delta added extra draw");
                Check(draw == expected.m_draws[index++], "delta resource identity/order/mask");
            }
        Check(index == expected.m_draws.size(), "delta omitted draw");
    }
};

void IncrementalContracts()
{
    TerrainSectorCoverageScratch incremental, full;
    DeltaConsumer consumer;
    uint32_t random = 0x93ba78;
    const auto next = [&]() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    for (size_t run = 0; run < 120; ++run)
    {
        std::vector<TerrainSectorCoverageClaim> claims;
        const size_t lodCount = 1 + next() % 9;
        for (uint64_t i = 0; i < 130; ++i)
            claims.push_back({ int32_t(next() % 25) - 12, int32_t(next() % 25) - 12,
                next() % 10, bool(next() % 2), bool(next() % 2), i + 1, 1 });
        claims.push_back({ INT32_MIN, INT32_MAX, 0, true, true, 150, 1 });
        claims.push_back({ INT32_MAX, INT32_MIN, 3, false, true, 151, 1 });
        claims.push_back(claims[0]); claims.back().m_destination = 152;
        for (size_t step = 0; step < 100; ++step)
        {
            auto& claim = claims[next() % claims.size()];
            switch (step % 9)
            {
            case 0: claim.m_known = !claim.m_known; break;
            case 1: claim.m_hasData = !claim.m_hasData; break;
            case 2: ++claim.m_resourceVersion; break;
            case 3: if (step == 30) claim.m_x = INT32_MIN; break;
            case 4: if (step == 40) std::reverse(claims.begin(), claims.end()); break;
            case 5: if (step == 50) incremental.Invalidate(); break;
            case 6: if (step == 60) ++claim.m_destination; break;
            case 7: if (step == 70) for (auto& item : claims) item.m_known = false; break;
            case 8: if (step == 80) for (auto& item : claims) { item.m_known = true; item.m_hasData = true; } break;
            }
            const auto& batch = EvaluateTerrainSectorCoverageChanges(claims, lodCount, incremental);
            Check(consumer.Apply(batch), "consumer rejected current batch");
            Check(!consumer.Apply(batch), "consumer accepted repeated/obsolete batch");
            const auto& actual = MaterializeTerrainSectorCoverage(incremental);
            const auto expected = KnownOracle(claims, lodCount, false);
            if (actual.m_draws.size() != expected.m_draws.size())
                std::fprintf(stderr, "run=%zu step=%zu actual=%zu expected=%zu reset=%d direct=%zu visited=%zu\n", run, step,
                    actual.m_draws.size(), expected.m_draws.size(), batch.m_reset,
                    incremental.m_statistics.m_directlyChangedClaims, incremental.m_statistics.m_visitedNodes);
            Equal(actual, expected);
            Equal(actual, KnownOracle(claims, lodCount, true));
            consumer.EqualDraws(expected);
            EvaluateTerrainSectorCoverageChanges(claims, lodCount, full, true);
            Equal(actual, MaterializeTerrainSectorCoverage(full));
        }
    }
    // A descendant draw can disappear while every ancestor aggregate stays Full.
    std::vector<TerrainSectorCoverageClaim> claims{{0,0,0,true,true,1,1}, {0,0,1,true,true,2,1}};
    Check(consumer.Apply(EvaluateTerrainSectorCoverageChanges(claims, 2, incremental)), "reset consumer");
    claims[0].m_hasData = false;
    const auto& removal = EvaluateTerrainSectorCoverageChanges(claims, 2, incremental);
    Check(removal.m_changes.size() == 1 && !removal.m_changes[0].m_after.m_quadrants, "unchanged aggregate swallowed descendant removal");
    Check(consumer.Apply(removal), "apply removal");
    claims[1].m_resourceVersion++;
    const auto& replacement = EvaluateTerrainSectorCoverageChanges(claims, 2, incremental);
    Check(replacement.m_changes.size() == 1 && replacement.m_changes[0].m_before.m_quadrants ==
        replacement.m_changes[0].m_after.m_quadrants, "identical-mask resource replacement omitted");
    Check(consumer.Apply(replacement), "apply resource replacement");
    // Complete snapshot comparison catches two updates within one frame.
    claims[0].m_known = false;
    EvaluateTerrainSectorCoverageChanges(claims, 2, incremental); // missed update
    claims[1].m_hasData = false;
    const auto& missed = EvaluateTerrainSectorCoverageChanges(claims, 2, incremental);
    Check(!consumer.Apply(missed), "missed baseline accepted");
    Check(consumer.Apply(EvaluateTerrainSectorCoverageChanges(claims, 2, incremental, true)), "full resynchronization rejected");
    consumer.EqualDraws(KnownOracle(claims, 2, false));
    // Warm sparse and dense updates include unknown-to-populated growth and batches.
    for (auto& item : claims) { item.m_hasData = true; item.m_known = true; }
    EvaluateTerrainSectorCoverageChanges(claims, 2, incremental);
    const auto allocations = Heap::allocations, generation = incremental.m_generation;
    for (size_t step = 0; step < 1000; ++step)
    {
        claims[step % 2].m_known ^= true;
        claims[step % 2].m_hasData ^= true;
        ++claims[step % 2].m_resourceVersion;
        EvaluateTerrainSectorCoverageChanges(claims, 2, incremental);
    }
    Check(allocations == Heap::allocations, "reserved sparse/dense delta evaluation allocated");
    Check(generation == incremental.m_generation, "eligibility/resource update rebuilt topology");
    EvaluateTerrainSectorCoverageChanges(claims, 2, incremental);
    Check(incremental.m_statistics.m_visitedNodes == 0 && incremental.m_batch.m_changes.empty(), "unchanged evaluation did work");
    std::puts("Passed 12000 full/incremental/map/flat/delta selections and generation/resource/allocation contracts");
}
