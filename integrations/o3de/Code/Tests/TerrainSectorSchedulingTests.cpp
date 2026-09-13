#include <AzTest/AzTest.h>
#include <TerrainCompositor/TerrainSectorScheduling.h>
#include <map>
#include <thread>
#include <chrono>
#include <cstdio>

namespace TerrainCompositor
{
    // Preserved pre-optimization projection as a differential oracle.
    //! The same projection serves production raster/RT selection and replacement
    //! assessment. Missing claims are unknown, never authoritative holes.
    inline TerrainSectorCoverageSelection ReferenceTerrainCoverage(
        AZStd::span<const TerrainSectorCoverageClaim> claims, size_t lodCount)
    {
        using Coordinate = std::pair<int32_t, int32_t>;
        constexpr size_t noClaim = AZStd::numeric_limits<size_t>::max();
        AZStd::vector<std::map<Coordinate, size_t>> levels(lodCount);
        TerrainSectorCoverageSelection selection;
        const auto parent = [](Coordinate coordinate)
        {
            const auto half = [](int32_t value) { return value / 2 - (value < 0 && value % 2 != 0); };
            return Coordinate{ half(coordinate.first), half(coordinate.second) };
        };
        for (size_t index = 0; index < claims.size(); ++index)
        {
            const auto& claim = claims[index];
            if (claim.m_lod >= lodCount) { ++selection.m_unrepresentableChildren; continue; }
            Coordinate coordinate{ claim.m_x, claim.m_y };
            auto [node, inserted] = levels[claim.m_lod].try_emplace(coordinate, noClaim);
            if (!inserted && node->second != noClaim) ++selection.m_duplicateClaims;
            node->second = index;
            for (size_t ancestor = claim.m_lod + 1; ancestor < levels.size(); ++ancestor)
            {
                coordinate = parent(coordinate);
                levels[ancestor].try_emplace(coordinate, noClaim);
            }
        }
        enum class Coverage { Missing, Partial, Full };
        const auto select = [&](auto&& self, size_t lod, Coordinate coordinate) -> Coverage
        {
            const auto node = levels[lod].find(coordinate);
            if (node == levels[lod].end()) return Coverage::Missing;
            const size_t index = node->second;
            uint8_t missing = 0;
            bool partial = false;
            for (uint8_t quadrant = 0; quadrant < 4; ++quadrant)
            {
                const int64_t x = int64_t(coordinate.first) * 2 + (quadrant & 1);
                const int64_t y = int64_t(coordinate.second) * 2 + (quadrant >> 1);
                const bool representable = x >= INT32_MIN && x <= INT32_MAX && y >= INT32_MIN && y <= INT32_MAX;
                const auto coverage = lod && representable ? self(self, lod - 1, { int32_t(x), int32_t(y) }) : Coverage::Missing;
                if (coverage == Coverage::Missing) missing |= 1 << quadrant;
                partial |= coverage == Coverage::Partial;
                if (index != noClaim && claims[index].m_hasData && coverage == Coverage::Partial)
                    ++selection.m_unrepresentableChildren;
            }
            if (index != noClaim)
            {
                if (!claims[index].m_hasData) return Coverage::Full;
                if (missing) selection.m_draws.push_back({ index, missing });
                return partial ? Coverage::Partial : Coverage::Full;
            }
            return !missing && !partial ? Coverage::Full : missing == 0xf ? Coverage::Missing : Coverage::Partial;
        };
        if (!levels.empty())
            for (const auto& [coordinate, unused] : levels.back()) select(select, levels.size() - 1, coordinate);
        return selection;
    }


    TEST(TerrainSectorSchedulingTests, CompleteLivePlanUsesSynchronousFallbackAndNeverAdmitsDeferredWork)
    {
        TerrainSectorSamplingPlan plan;
        EXPECT_EQ(AssessTerrainSectorAdmission(plan, false), TerrainSectorAdmission::InvalidPlan);
        plan.m_regular = { AZ::Vector2(0), 1, 3, 3 };
        plan.m_clod = { AZ::Vector2(0), 2, 2, 2 };
        plan.m_clodEnabled = true;
        plan.m_area.m_captured = true;
        plan.Assess();
        EXPECT_FALSE(plan.CanExecuteAcrossFrames());
        EXPECT_EQ(AssessTerrainSectorAdmission(plan, false), TerrainSectorAdmission::Synchronous);
        EXPECT_EQ(AssessTerrainSectorAdmission(plan, false, true), TerrainSectorAdmission::DeferredUnsupported);
        EXPECT_EQ(AssessTerrainSectorAdmission(plan, true), TerrainSectorAdmission::Cancelled);
        plan.m_clod.m_spacing = 0;
        EXPECT_EQ(AssessTerrainSectorAdmission(plan, false), TerrainSectorAdmission::InvalidPlan);
        // Even a hypothetical capable plan does not enable scheduling policy.
        plan.m_clodEnabled = false;
        plan.m_acrossFramesFallbacks = 0;
        EXPECT_TRUE(plan.CanExecuteAcrossFrames());
        EXPECT_EQ(AssessTerrainSectorAdmission(plan, false, true), TerrainSectorAdmission::DeferredUnsupported);
    }

    TEST(TerrainSectorSchedulingTests, AtomicUploadBudgetDistinguishesWaitingFromAnImpossibleGroup)
    {
        using Decision = TerrainSectorCommitAdmission;
        EXPECT_EQ(AssessTerrainSectorCommitBudget(100, 100, 100), Decision::CommitWhole);
        EXPECT_EQ(AssessTerrainSectorCommitBudget(100, 99, 100), Decision::WaitForBudget);
        EXPECT_EQ(AssessTerrainSectorCommitBudget(101, 100, 100), Decision::AtomicGroupTooLarge);
        EXPECT_EQ(AssessTerrainSectorCommitBudget(101, 200, 100), Decision::AtomicGroupTooLarge);
        EXPECT_EQ(AssessTerrainSectorCommitBudget(0, 0, 0), Decision::CommitWhole);
        const auto maximum = AZStd::numeric_limits<size_t>::max();
        EXPECT_EQ(AssessTerrainSectorCommitBudget(maximum, maximum, maximum), Decision::CommitWhole);
    }

    TEST(TerrainSectorSchedulingTests, SplitAssessmentIncludesRegularAndClodHalosAndExactSourceAuthorities)
    {
        TerrainSectorSamplingPlan a, b;
        a.m_regular = { AZ::Vector2(-2, 0), 1, 3, 3 };
        b.m_regular = { AZ::Vector2(0, 0), 1, 3, 3 };
        a.m_clod = { AZ::Vector2(-2, 0), 2, 2, 2 };
        b.m_clod = { AZ::Vector2(0, 0), 2, 2, 2 };
        a.m_clodEnabled = b.m_clodEnabled = true;
        auto dependency = std::make_shared<TerrainPreparationDependency>();
        a.m_dependencies = b.m_dependencies = { { dependency, dependency->Capture() } };
        const auto has = [](AZ::u32 reasons, TerrainSectorSplitBlocker reason) { return (reasons & static_cast<AZ::u32>(reason)) != 0; };
        auto blockers = AssessTerrainSectorSplitSampling(a, b);
        EXPECT_TRUE(has(blockers, TerrainSectorSplitBlocker::LiveSampling));
        EXPECT_TRUE(has(blockers, TerrainSectorSplitBlocker::SharedSamples));
        EXPECT_TRUE(has(blockers, TerrainSectorSplitBlocker::ClodSamples));
        EXPECT_FALSE(has(blockers, TerrainSectorSplitBlocker::SourceTickets));
        // Regular grids need not overlap for a CLOD halo to cross the cut.
        b.m_regular.m_start = b.m_clod.m_start = AZ::Vector2(4, 0);
        blockers = AssessTerrainSectorSplitSampling(a, b);
        EXPECT_FALSE(has(blockers, TerrainSectorSplitBlocker::SharedSamples));
        EXPECT_TRUE(has(blockers, TerrainSectorSplitBlocker::ClodSamples));
        b.m_regular.m_start = b.m_clod.m_start = AZ::Vector2(100, 0);
        EXPECT_FALSE(has(AssessTerrainSectorSplitSampling(a, b), TerrainSectorSplitBlocker::ClodSamples));
        ++b.m_dependencies[0].m_revision;
        EXPECT_TRUE(has(AssessTerrainSectorSplitSampling(a, b), TerrainSectorSplitBlocker::SourceTickets));
        b.m_dependencies = { { std::make_shared<TerrainPreparationDependency>(), 0 } };
        EXPECT_TRUE(has(AssessTerrainSectorSplitSampling(a, b), TerrainSectorSplitBlocker::SourceTickets));
        b.m_publication = std::make_shared<TerrainMeshCutoutRenderSnapshot>();
        EXPECT_TRUE(has(AssessTerrainSectorSplitSampling(a, b), TerrainSectorSplitBlocker::Publication));
    }

    TEST(TerrainSectorSchedulingTests, CoverageProjectionPreservesSignedEmptyClaimsAndRejectsMissingIntermediateReplacement)
    {
        AZStd::vector<TerrainSectorCoverageClaim> claims{ { -1, -1, 2, true }, { -1, -1, 0, false } };
        auto selection = SelectTerrainSectorCoverage(claims, 3);
        ASSERT_EQ(selection.m_draws.size(), 1);
        EXPECT_EQ(selection.m_draws[0].m_quadrants, 0x7);
        EXPECT_FALSE(selection.IsRepresentable());
        EXPECT_EQ(selection.m_unrepresentableChildren, 1);
        claims.push_back({ -1, -1, 1, true });
        selection = SelectTerrainSectorCoverage(claims, 3);
        ASSERT_EQ(selection.m_draws.size(), 2);
        EXPECT_TRUE(selection.IsRepresentable());
        for (const auto& draw : selection.m_draws) EXPECT_EQ(draw.m_quadrants, 0x7);
        claims[1].m_hasData = true;
        selection = SelectTerrainSectorCoverage(claims, 3);
        ASSERT_EQ(selection.m_draws.size(), 3);
        EXPECT_TRUE(selection.IsRepresentable());
        claims.push_back(claims[1]);
        EXPECT_EQ(SelectTerrainSectorCoverage(claims, 3).m_duplicateClaims, 1);
    }

    TEST(TerrainSectorSchedulingTests, CoverageProjectionMatchesIndependentCellOracleAcrossZeroAndExtremeCoordinates)
    {
        // Exhaust all populated/empty/unknown combinations for four children.
        // Compare raster/RT masks against independent unit-cell ownership.
        for (int32_t x : { -2, -1, 0, 1 })
            for (int32_t y : { -2, -1, 0, 1 })
                for (unsigned code = 0; code < 81; ++code)
                {
                    AZStd::vector<TerrainSectorCoverageClaim> claims{ { x, y, 1, true } };
                    unsigned states = code;
                    unsigned expectedDraws[4]{};
                    for (unsigned q = 0; q < 4; ++q, states /= 3)
                    {
                        const auto state = states % 3;
                        if (state) claims.push_back({ x * 2 + int32_t(q & 1), y * 2 + int32_t(q >> 1), 0, state == 2 });
                        expectedDraws[q] = state == 1 ? 0 : 1;
                    }
                    const auto selection = SelectTerrainSectorCoverage(claims, 2);
                    EXPECT_TRUE(selection.IsRepresentable());
                    unsigned actualDraws[4]{};
                    for (const auto& draw : selection.m_draws)
                    {
                        const auto& claim = claims[draw.m_claim];
                        if (claim.m_lod == 1)
                            for (unsigned q = 0; q < 4; ++q) actualDraws[q] += (draw.m_quadrants >> q) & 1;
                        else
                        {
                            const auto q = unsigned(claim.m_x - x * 2) + 2 * unsigned(claim.m_y - y * 2);
                            ++actualDraws[q];
                        }
                    }
                    for (unsigned q = 0; q < 4; ++q) EXPECT_EQ(actualDraws[q], expectedDraws[q]);
                }
        const AZStd::vector<TerrainSectorCoverageClaim> extremes{
            { INT32_MIN, INT32_MAX, 0, true }, { INT32_MAX, INT32_MIN, 0, false } };
        const auto selection = SelectTerrainSectorCoverage(extremes, 3);
        ASSERT_EQ(selection.m_draws.size(), 1);
        EXPECT_EQ(selection.m_draws[0].m_claim, 0);
        EXPECT_EQ(selection.m_draws[0].m_quadrants, 0xf);
    }
    TEST(TerrainSectorSchedulingTests, ReusedAdmissionKeepsExactTicketsAndRevalidatesEveryTime)
    {
        auto dependency = std::make_shared<TerrainPreparationDependency>();
        auto other = std::make_shared<TerrainPreparationDependency>();
        AZStd::vector<TerrainPreparationDependencyTicket> tickets{
            { dependency, dependency->Capture() }, { other, other->Capture() }, { dependency, dependency->Capture() } };
        const auto set = std::make_shared<const TerrainPreparationDependencySet>(tickets);
        TerrainPreparationAdmissionScratch scratch;
        EXPECT_TRUE(TerrainPreparationAdmission(set, scratch).IsValid());
        EXPECT_TRUE(TerrainPreparationAdmission(set, scratch).IsValid());
        dependency->Invalidate();
        EXPECT_FALSE(TerrainPreparationAdmission(set, scratch).IsValid());
        tickets[0].m_revision = dependency->Capture();
        EXPECT_FALSE(set->Matches(tickets));
        // Conflicting duplicate tickets must not collapse into the newer revision.
        EXPECT_FALSE(TerrainPreparationAdmission(std::make_shared<const TerrainPreparationDependencySet>(tickets), scratch).IsValid());
        tickets[2].m_revision = tickets[0].m_revision;
        const auto current = std::make_shared<const TerrainPreparationDependencySet>(tickets);
        EXPECT_TRUE(TerrainPreparationAdmission(current, scratch).IsValid());
        other->Retire();
        EXPECT_FALSE(TerrainPreparationAdmission(current, scratch).IsValid());
    }

    TEST(TerrainSectorSchedulingTests, AdmissionScratchReleasesLocksAndDoesNotRetainDependencies)
    {
        TerrainPreparationAdmissionScratch scratch;
        std::weak_ptr<TerrainPreparationDependency> weak;
        {
            auto dependency = std::make_shared<TerrainPreparationDependency>();
            weak = dependency;
            auto set = std::make_shared<const TerrainPreparationDependencySet>(
                AZStd::vector<TerrainPreparationDependencyTicket>{ { dependency, dependency->Capture() } });
            {
                TerrainPreparationAdmission admission(set, scratch);
                set.reset();
                dependency.reset();
                EXPECT_TRUE(admission.IsValid());
                EXPECT_FALSE(weak.expired());
            }
            EXPECT_TRUE(weak.expired());
        }
        EXPECT_FALSE(TerrainPreparationAdmission(nullptr, scratch).IsValid());
        EXPECT_FALSE(TerrainPreparationAdmission(std::make_shared<const TerrainPreparationDependencySet>(
            AZStd::vector<TerrainPreparationDependencyTicket>{ {} }), scratch).IsValid());
        auto next = std::make_shared<TerrainPreparationDependency>();
        EXPECT_TRUE(TerrainPreparationAdmission(std::make_shared<const TerrainPreparationDependencySet>(
            AZStd::vector<TerrainPreparationDependencyTicket>{ { next, 0 } }), scratch).IsValid());
        next->Invalidate(); // Scratch holds no locks after admission ends.
    }

    TEST(TerrainSectorSchedulingTests, ReusedCoverageMatchesMapOracleAcrossChangingLayouts)
    {
        TerrainSectorCoverageScratch scratch;
        uint32_t random = 0xabc123;
        const auto next = [&]() { random = random * 1664525u + 1013904223u; return random; };
        for (size_t run = 0; run < 300; ++run)
        {
            const size_t lodCount = run % 8;
            AZStd::vector<TerrainSectorCoverageClaim> claims;
            for (size_t i = 0, count = next() % 200; i < count; ++i)
            {
                const int32_t x = int32_t(next() % 65) - 32, y = int32_t(next() % 65) - 32;
                const uint32_t lod = next() % 9;
                claims.push_back({ x, y, lod, (next() & 0x100) != 0 });
                if (i % 11 == 0) claims.push_back({ x, y, lod, !claims.back().m_hasData });
            }
            claims.push_back({ INT32_MIN, INT32_MAX, 0, true });
            claims.push_back({ INT32_MAX, INT32_MIN, 3, false });
            const auto reference = ReferenceTerrainCoverage(claims, lodCount);
            const auto& actual = SelectTerrainSectorCoverage(claims, lodCount, scratch);
            EXPECT_EQ(actual.m_duplicateClaims, reference.m_duplicateClaims);
            EXPECT_EQ(actual.m_unrepresentableChildren, reference.m_unrepresentableChildren);
            ASSERT_EQ(actual.m_draws.size(), reference.m_draws.size());
            for (size_t i = 0; i < actual.m_draws.size(); ++i)
            {
                EXPECT_EQ(actual.m_draws[i].m_claim, reference.m_draws[i].m_claim);
                EXPECT_EQ(actual.m_draws[i].m_quadrants, reference.m_draws[i].m_quadrants);
            }
        }
        const AZStd::vector<TerrainSectorCoverageClaim> claims{ { -1, 0, 0, true }, { -1, 0, 1, true } };
        SelectTerrainSectorCoverage(claims, 8, scratch);
        const auto* levelStorage = scratch.m_levels.data();
        const auto* nodes = scratch.m_levels[0].data();
        const auto* draws = scratch.m_selection.m_draws.data();
        SelectTerrainSectorCoverage({}, 0, scratch);
        EXPECT_TRUE(scratch.m_selection.m_draws.empty());
        SelectTerrainSectorCoverage(claims, 8, scratch);
        EXPECT_EQ(scratch.m_levels.data(), levelStorage);
        EXPECT_EQ(scratch.m_levels[0].data(), nodes);
        EXPECT_EQ(scratch.m_selection.m_draws.data(), draws);
    }

    TEST(TerrainSectorSchedulingTests, VisibilityObservationsSeeImmediateInvalidationAndPermanentRetirement)
    {
        auto dependency = std::make_shared<TerrainPreparationDependency>();
        auto set = std::make_shared<TerrainPreparationDependencySet>(
            AZStd::vector<TerrainPreparationDependencyTicket>{ { dependency, dependency->Capture() } });
        EXPECT_TRUE(set->IsCurrent());
        std::thread invalidate([&]() { dependency->Invalidate(); });
        invalidate.join();
        EXPECT_FALSE(set->IsCurrent());
        set = std::make_shared<TerrainPreparationDependencySet>(
            AZStd::vector<TerrainPreparationDependencyTicket>{ { dependency, dependency->Capture() } });
        EXPECT_TRUE(set->IsCurrent());
        dependency->Retire();
        EXPECT_FALSE(set->IsCurrent());
        EXPECT_FALSE(dependency->IsCurrent(dependency->Capture()));
        dependency->Invalidate();
        EXPECT_FALSE(dependency->IsCurrent(dependency->Capture()));
        EXPECT_FALSE(TerrainPreparationDependencySet(AZStd::vector<TerrainPreparationDependencyTicket>{ {} }).IsCurrent());
        auto other = std::make_shared<TerrainPreparationDependency>();
        EXPECT_FALSE(TerrainPreparationDependencySet(
            AZStd::vector<TerrainPreparationDependencyTicket>{ { other, 0 }, { other, 1 } }).IsCurrent());
    }

    TEST(TerrainSectorSchedulingTests, DISABLED_MeasureCoverageSelection)
    {
        AZStd::vector<TerrainSectorCoverageClaim> claims;
        for (uint32_t lod = 0; lod < 7; ++lod)
            for (int32_t x = -5; x < 5; ++x)
                for (int32_t y = -5; y < 5; ++y) claims.push_back({ x, y, lod, (x + y) % 7 != 0 });
        TerrainSectorCoverageScratch scratch;
        SelectTerrainSectorCoverage(claims, 7, scratch);
        size_t draws = 0;
        for (bool reuse : { false, true })
        {
            const auto start = std::chrono::steady_clock::now();
            for (size_t repeat = 0; repeat < 500; ++repeat)
                if (reuse) draws += SelectTerrainSectorCoverage(claims, 7, scratch).m_draws.size();
                else draws += ReferenceTerrainCoverage(claims, 7).m_draws.size();
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 500;
            std::printf("TerrainCoverage reuse=%d claims=%zu selection-us=%.3f draws=%zu\n", reuse, claims.size(), us, draws);
        }
        EXPECT_GT(draws, 0);
    }
}
