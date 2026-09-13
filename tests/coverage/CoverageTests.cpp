#include "ReferenceCoverage.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

// Count ordinary C++ heap allocations used by these selectors (including the
// generic library's temporary vectors and compact allocation). The separate
// instrumented executable supplies memory evidence; use the ordinary executable
// for timings without allocation hooks.
namespace Heap
{
    size_t allocations = 0, live = 0, peak = 0;
    struct alignas(std::max_align_t) Header { size_t bytes; };
}
#ifdef TC_COUNT_ALLOCATIONS
void* operator new(size_t size)
{
    auto* header = static_cast<Heap::Header*>(std::malloc(sizeof(Heap::Header) + size));
    if (!header) throw std::bad_alloc();
    header->bytes = size;
    ++Heap::allocations;
    Heap::live += size;
    Heap::peak = std::max(Heap::peak, Heap::live);
    return header + 1;
}
void operator delete(void* pointer) noexcept
{
    if (!pointer) return;
    auto* header = static_cast<Heap::Header*>(pointer) - 1;
    Heap::live -= header->bytes;
    std::free(header);
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, size_t) noexcept { ::operator delete(pointer); }
#endif

using namespace TerrainCompositor;
void Check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void Equal(const TerrainSectorCoverageSelection& actual, const TerrainSectorCoverageSelection& expected)
{
    Check(actual.m_duplicateClaims == expected.m_duplicateClaims, "duplicates");
    Check(actual.m_unrepresentableChildren == expected.m_unrepresentableChildren, "partial/invalid coverage");
    Check(actual.m_draws.size() == expected.m_draws.size(), "draw count");
    for (size_t i = 0; i < actual.m_draws.size(); ++i)
    {
        Check(actual.m_draws[i].m_claim == expected.m_draws[i].m_claim, "draw order");
        Check(actual.m_draws[i].m_quadrants == expected.m_draws[i].m_quadrants, "quadrant mask");
    }
}
void Contracts()
{
    TerrainSectorCoverageScratch cached;
    FlatScratch flat;
    uint32_t random = 0xabc123;
    const auto next = [&]() { random = random * 1664525u + 1013904223u; return random; };
    for (size_t run = 0; run < 3000; ++run)
    {
        const size_t lodCount = run % 10;
        std::vector<TerrainSectorCoverageClaim> claims;
        for (size_t i = 0, count = next() % 300; i < count; ++i)
        {
            const int32_t x = int32_t(next() % 65) - 32, y = int32_t(next() % 65) - 32;
            const uint32_t lod = next() % 11;
            claims.push_back({ x, y, lod, (next() & 0x100) != 0 });
            if (i % 11 == 0) claims.push_back({ x, y, lod, !claims.back().m_hasData });
        }
        claims.push_back({ INT32_MIN, INT32_MAX, 0, true });
        claims.push_back({ INT32_MAX, INT32_MIN, 3, false });
        for (int phase = 0; phase < 3; ++phase)
        {
            const auto expected = ReferenceTerrainCoverage(claims, lodCount);
            Equal(ReferenceFlatCoverage(claims, lodCount, flat), expected);
            const auto generation = cached.m_generation;
            Equal(EvaluateTerrainSectorCoverage(claims, lodCount, cached), expected);
            if (phase) Check(cached.m_generation == generation, "policy change rebuilt topology");
            for (auto& claim : claims) claim.m_hasData = !claim.m_hasData;
        }
    }
    std::vector<TerrainSectorCoverageClaim> claims{ { -1, 0, 0, true }, { -1, 0, 1, true } };
    EvaluateTerrainSectorCoverage(claims, 8, cached);
    auto generation = cached.m_generation;
    const auto allocations = Heap::allocations;
    for (int i = 0; i < 100; ++i)
    {
        for (auto& claim : claims) claim.m_hasData = !claim.m_hasData;
        EvaluateTerrainSectorCoverage(claims, 8, cached);
    }
    Check(Heap::allocations == allocations, "warm evaluation allocated");
    Check(cached.m_generation == generation, "warm evaluation rebuilt");
    std::reverse(claims.begin(), claims.end());
    Equal(EvaluateTerrainSectorCoverage(claims, 8, cached), ReferenceTerrainCoverage(claims, 8));
    Check(cached.m_generation == ++generation, "claim reordering must rebuild");
    claims[0].m_x = INT32_MAX; // Destination reuse / teleport, no pointer identity.
    Equal(EvaluateTerrainSectorCoverage(claims, 8, cached), ReferenceTerrainCoverage(claims, 8));
    Check(cached.m_generation == ++generation, "coordinate change must rebuild");
    cached.Invalidate(); // Same keys in a new owner/topology generation.
    EvaluateTerrainSectorCoverage(claims, 8, cached);
    Check(cached.m_generation == ++generation, "explicit invalidation must rebuild");
    EvaluateTerrainSectorCoverage(claims, 4, cached);
    Check(cached.m_generation == ++generation, "LOD reconfiguration must rebuild");
    auto moved = std::move(cached);
    Check(!cached.m_valid, "move must invalidate source workspace");
    Equal(EvaluateTerrainSectorCoverage(claims, 4, moved), ReferenceTerrainCoverage(claims, 4));
    Equal(EvaluateTerrainSectorCoverage(claims, 4, cached), ReferenceTerrainCoverage(claims, 4));
    Equal(EvaluateTerrainSectorCoverage({}, 0, moved), ReferenceTerrainCoverage({}, 0));
    std::puts("Passed 9000 differential selections, warm allocation and plan lifetime contracts");
}

template<class Scratch, class Evaluate>
void Measure(const char* name, Scratch& scratch, Evaluate evaluate, std::vector<TerrainSectorCoverageClaim> claims)
{
    const size_t beforeBytes = Heap::live, beforeAllocations = Heap::allocations;
    Heap::peak = Heap::live;
    evaluate(claims, 7, scratch);
    std::printf("memory,%s,cold,allocations=%zu,retained=%zu,peak=%zu\n", name,
        Heap::allocations - beforeAllocations, Heap::live - beforeBytes, Heap::peak - beforeBytes);
    for (int workload = 0; workload < 3; ++workload)
    {
        size_t draws = 0;
        auto run = [&]()
        {
            for (size_t repeat = 0; repeat < 500; ++repeat)
            {
                if (workload == 1) claims[repeat % claims.size()].m_hasData ^= true;
                if (workload == 2) claims[0].m_x = repeat % 2 ? -6 : -5;
                draws += evaluate(claims, 7, scratch).m_draws.size();
            }
        };
        const auto start = std::chrono::steady_clock::now();
        run();
        const auto us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 500;
        const auto count = Heap::allocations;
        run();
        std::printf("selection,%s,%s,us=%.3f,allocations-per-call=%.3f,draws=%zu\n", name,
            workload == 0 ? "warm" : workload == 1 ? "policy" : "topology", us,
            double(Heap::allocations - count) / 500, draws);
    }
    const auto start = std::chrono::steady_clock::now();
    size_t draws = 0;
    for (int repeat = 0; repeat < 500; ++repeat)
    {
        Scratch cold;
        draws += evaluate(claims, 7, cold).m_draws.size();
    }
    const auto us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 500;
    std::printf("selection,%s,cold,us=%.3f,draws=%zu\n", name, us, draws);
}
int main(int argc, char** argv)
{
#ifdef TC_COUNT_ALLOCATIONS
    std::puts("Allocation hooks enabled: use the ordinary executable for timings");
#else
    std::puts("Allocation hooks disabled: memory counters below are unavailable");
#endif
    if (argc < 2 || std::strcmp(argv[1], "--benchmark")) { Contracts(); return 0; }
    std::vector<TerrainSectorCoverageClaim> claims;
    for (uint32_t lod = 0; lod < 7; ++lod)
        for (int32_t x = -5; x < 5; ++x)
            for (int32_t y = -5; y < 5; ++y) claims.push_back({ x, y, lod, (x + y) % 7 != 0 });
    FlatScratch flat;
    Measure("flat", flat, ReferenceFlatCoverage, claims);
    TerrainSectorCoverageScratch cached;
    Measure("polytree", cached, EvaluateTerrainSectorCoverage, claims);
}
