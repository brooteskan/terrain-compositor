#include <AzTest/AzTest.h>
#include <TerrainCompositor/TerrainSectorScheduling.h>

namespace TerrainCompositor
{
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
}
