#include <TerrainCompositor/TerrainSectorSampling.h>
#include "ProceduralSnapshotTestSupport.h"

namespace TerrainCompositor
{
    namespace SectorSamplingTestSupport
    {
        using Sampler = AzFramework::Terrain::TerrainDataRequests::Sampler;
        using Reason = TerrainRenderFallback;
        struct Fixture
        {
            TerrainSectorSamplingPlan m_plan;
            std::shared_ptr<TerrainMeshCutoutRenderSnapshot> m_publication = std::make_shared<TerrainMeshCutoutRenderSnapshot>();
            TerrainMeshCutoutRenderChannelPtr m_channel = std::make_shared<TerrainMeshCutoutRenderChannel>();
            std::shared_ptr<TerrainPreparationDependency> m_dependency = std::make_shared<TerrainPreparationDependency>();
            AZStd::vector<TerrainPreparationDependencyTicket> m_tickets{ { m_dependency, m_dependency->Capture() } };
            size_t m_calls = 0;
            Fixture()
            {
                m_channel->m_snapshot.store(m_publication);
                m_plan.m_regular = { AZ::Vector2(0), 1, 3, 3 };
                m_plan.m_clod = { AZ::Vector2(0), 2, 2, 2 };
                m_plan.m_clodEnabled = true;
                m_plan.m_area.m_captured = m_plan.m_area.m_exists = true;
                m_publication->m_renderGeometryQueries.push_back(Owner());
            }
            TerrainRenderGeometryQuery Owner()
            {
                TerrainRenderGeometryQuery query;
                query.m_regionBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-100), AZ::Vector3(100));
                query.m_getHeight = [this](const auto&) { ++m_calls; return 2.0f; };
                query.m_getTerrainExists = [this](const auto&) { ++m_calls; return true; };
                query.m_getGeometry = [this](auto, auto, auto) { ++m_calls; };
                query.m_acquireSources = [this]() { ++m_calls; return std::shared_ptr<const TerrainRenderGeometryQuery>{}; };
                auto& capability = query.m_capability;
                capability.m_declared = true;
                capability.m_coordinates = TerrainRenderCoordinates::WorldXYOrdinarySurfaceZ;
                capability.m_acceptsExplicitPositions = capability.m_acceptsRegularGrid = true;
                capability.m_exact = capability.m_clamp = capability.m_bilinear = true;
                capability.m_height.m_source = TerrainRenderSource::RetainedAvailable;
                capability.m_height.m_inputZ = TerrainRenderInputZ::Independent;
                capability.m_height.m_requiresOrdinaryResult = false;
                capability.m_height.m_sampling = { true, true, true, true, true, true, 1, 65535, 10000 };
                // Synthetic oracle contract only. Production adapters must not copy
                // these declarations without establishing the documented proofs.
                capability.m_height.m_ordinaryEquivalence = { true, true, true, true, true, true };
                capability.m_existence = capability.m_height;
                return query;
            }
            void Assess()
            {
                m_plan.m_publication = m_publication;
                m_plan.m_channel = m_channel;
                m_plan.m_dependencies = m_tickets;
                m_plan.Assess();
                EXPECT_EQ(m_calls, 0); // Planning never acquires, probes, or samples.
            }
            bool Has(Reason reason) { return (m_plan.m_ordinaryFallbacks & TerrainRenderFallbackBit(reason)) != 0; }
        };
    }
    using namespace SectorSamplingTestSupport;

    TEST(TerrainSectorSamplingTests, CompleteEligibilityAndExecutionPolicyAreIndependent)
    {
        Fixture f;
        for (auto sampler : { Sampler::EXACT, Sampler::CLAMP, Sampler::BILINEAR })
            for (bool batch : { false, true })
            {
                f.m_plan.m_regular.m_sampler = f.m_plan.m_clod.m_sampler = sampler;
                f.m_plan.m_batchQueries = batch;
                f.Assess();
                EXPECT_TRUE(f.m_plan.CanAvoidOrdinaryResults());
                EXPECT_FALSE(f.m_plan.CanExecuteAcrossFrames());
                EXPECT_EQ(f.m_plan.m_queryPolicy, TerrainSectorQueryPolicy::OrdinaryThenOverlay);
                EXPECT_EQ(f.m_plan.m_schedulePolicy, TerrainSectorSchedulePolicy::Synchronous);
                EXPECT_EQ(f.m_plan.m_regularReadiness.m_eligibleSamples, 25);
                EXPECT_EQ(f.m_plan.m_clodReadiness.m_eligibleSamples, 16);
                EXPECT_EQ(f.m_plan.m_acrossFramesFallbacks, TerrainRenderFallbackBit(Reason::AreaLifetimeUnproven) |
                    TerrainRenderFallbackBit(Reason::OrdinaryQueryLifetimeUnproven));
            }
        f.m_plan.m_area.m_retainedAcrossFrames = true;
        f.Assess();
        EXPECT_FALSE(f.m_plan.CanExecuteAcrossFrames()); // Enabled ordinary queries still depend on live terrain.
        f.m_plan.m_ordinaryQueriesRetainedAcrossFrames = true;
        f.Assess();
        EXPECT_TRUE(f.m_plan.CanExecuteAcrossFrames()); // Capability only; policy remains synchronous.
    }

    TEST(TerrainSectorSamplingTests, EveryHaloPositionIncludingCornersAndCoarserSpacingIsRequired)
    {
        Fixture f;
        EXPECT_EQ(f.m_plan.m_regular.Position(0, 0), AZ::Vector3(-1,-1,0));
        EXPECT_EQ(f.m_plan.m_clod.Position(0, 0), AZ::Vector3(-2,-2,0));
        EXPECT_EQ(f.m_plan.m_clod.Position(3, 3), AZ::Vector3(4,4,0));
        // A one-point first owner with a live existence channel must prevent
        // whole-sector eligibility at EVERY position, including all halo corners.
        for (const auto layout : { f.m_plan.m_regular, f.m_plan.m_clod })
            for (size_t y = 0; y < layout.Height(); ++y)
                for (size_t x = 0; x < layout.Width(); ++x)
                {
                    auto live = f.Owner();
                    const auto p = layout.Position(x, y);
                    live.m_regionBounds = AZ::Aabb::CreateFromMinMax(p, p);
                    live.m_capability.m_existence.m_source = TerrainRenderSource::Live;
                    f.m_publication->m_renderGeometryQueries = { live, f.Owner() };
                    f.Assess();
                    EXPECT_FALSE(f.m_plan.CanAvoidOrdinaryResults());
                    EXPECT_TRUE(f.Has(Reason::LiveSource));
                }
        auto& owner = f.m_publication->m_renderGeometryQueries;
        owner = { f.Owner() };
        owner[0].m_regionBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-1), AZ::Vector3(3));
        f.Assess();
        EXPECT_EQ(f.m_plan.m_regularReadiness.m_eligibleSamples, 25);
        EXPECT_LT(f.m_plan.m_clodReadiness.m_bothOwned, 16);
        EXPECT_TRUE(f.Has(Reason::UnownedHeight));
        EXPECT_TRUE(f.Has(Reason::UnownedExistence));
    }

    TEST(TerrainSectorSamplingTests, InclusiveFirstMatchAndSplitChannelOwnersAreNotWholeOwnership)
    {
        Fixture f;
        auto left = f.Owner(), right = f.Owner();
        left.m_regionBounds = AZ::Aabb::CreateFromMinMax({-10,-10,-1}, {0,10,1});
        right.m_regionBounds = AZ::Aabb::CreateFromMinMax({0,-10,-1}, {10,10,1});
        right.m_capability.m_height.m_source = TerrainRenderSource::Unavailable;
        f.m_publication->m_renderGeometryQueries = { left, right };
        f.Assess();
        EXPECT_EQ(f.m_plan.m_regularReadiness.m_eligibleSamples, 10); // X=-1 and inclusive X=0 use left.
        EXPECT_TRUE(f.Has(Reason::UnavailableSource));
        left = f.Owner(); right = f.Owner();
        left.m_getTerrainExists = {};
        right.m_getHeight = {};
        f.m_publication->m_renderGeometryQueries = { left, right };
        f.Assess();
        EXPECT_EQ(f.m_plan.m_regularReadiness.m_bothOwned, 25);
        EXPECT_EQ(f.m_plan.m_regularReadiness.m_eligibleSamples, 0);
        EXPECT_TRUE(f.Has(Reason::SplitOwners));
        f.m_publication->m_renderGeometryQueries.clear();
        f.Assess();
        EXPECT_EQ(f.m_plan.m_regularReadiness.m_bothOwned, 0);
        EXPECT_TRUE(f.Has(Reason::UnownedHeight));
    }

    TEST(TerrainSectorSamplingTests, EachMissingEquivalenceProofHasAnExplicitFallback)
    {
        for (auto reason : { Reason::CoordinatesUnproven, Reason::SamplerEquivalenceUnproven,
            Reason::RenderValueUnproven, Reason::CollisionFallbackUnproven, Reason::InputZ,
            Reason::OrdinaryDependency, Reason::LegacyContract, Reason::UnknownSource })
        {
            Fixture f;
            auto& cap = f.m_publication->m_renderGeometryQueries[0].m_capability;
            auto& proof = cap.m_existence.m_ordinaryEquivalence;
            if (reason == Reason::CoordinatesUnproven) proof.m_coordinates = false;
            if (reason == Reason::SamplerEquivalenceUnproven) proof.m_clamp = false;
            if (reason == Reason::RenderValueUnproven) proof.m_renderValue = false;
            if (reason == Reason::CollisionFallbackUnproven) proof.m_collisionFallback = false;
            if (reason == Reason::InputZ) cap.m_existence.m_inputZ = TerrainRenderInputZ::OrdinarySurface;
            if (reason == Reason::OrdinaryDependency) cap.m_existence.m_requiresOrdinaryResult = true;
            if (reason == Reason::LegacyContract) cap.m_declared = false;
            if (reason == Reason::UnknownSource) cap.m_existence.m_source = TerrainRenderSource::Unknown;
            f.Assess();
            EXPECT_TRUE(f.Has(reason));
            EXPECT_FALSE(f.m_plan.CanAvoidOrdinaryResults());
        }
    }

    TEST(TerrainSectorSamplingTests, DispatchSamplerGridAndSampleLimitsAreAssessedWithoutCallingSources)
    {
        for (bool batch : { false, true })
            for (size_t limit : { 0, 1, 15, 16, 24, 25, 255, 256, 257 })
            {
                Fixture f;
                f.m_plan.m_batchQueries = batch;
                f.m_publication->m_renderGeometryQueries[0].m_capability.m_height.m_sampling.m_maxSamples = limit;
                f.Assess();
                EXPECT_EQ(f.m_plan.CanAvoidOrdinaryResults(), limit >= (batch ? 25 : 1));
            }
        for (int unsupported = 0; unsupported < 7; ++unsupported)
        {
            Fixture f;
            auto& cap = f.m_publication->m_renderGeometryQueries[0].m_capability;
            if (unsupported == 0) cap.m_height.m_sampling.m_clamp = false;
            if (unsupported == 1) cap.m_existence.m_sampling.m_regularGrid = false;
            if (unsupported == 2) f.m_plan.m_regular.m_sampler = static_cast<Sampler>(99);
            if (unsupported == 3) cap.m_height.m_sampling.m_maxAbsXY = 1;
            if (unsupported == 4) cap.m_minSamples = 26;
            if (unsupported == 5) cap.m_acceptsRegularGrid = false;
            if (unsupported == 6)
            {
                f.m_publication->m_renderGeometryQueries[0].m_getGeometry = {};
                cap.m_height.m_sampling.m_minSamples = 2; // Scalar-only provider cannot accept a one-point invocation.
            }
            f.Assess();
            EXPECT_TRUE(f.Has(Reason::UnsupportedRequest));
        }
        Fixture scalar;
        scalar.m_publication->m_renderGeometryQueries[0].m_getGeometry = {};
        scalar.Assess();
        EXPECT_TRUE(scalar.m_plan.CanAvoidOrdinaryResults());
    }

    TEST(TerrainSectorSamplingTests, InvalidLayoutsAndAreaCaptureCannotSilentlyBecomeEligible)
    {
        for (int invalid = 0; invalid < 5; ++invalid)
        {
            Fixture f;
            if (invalid == 0) f.m_plan.m_regular.m_samplesX = 0;
            if (invalid == 1) f.m_plan.m_clod.m_samplesX = std::numeric_limits<size_t>::max();
            if (invalid == 2) f.m_plan.m_regular.m_samplesX = f.m_plan.m_regular.m_samplesY = 256;
            if (invalid == 3) f.m_plan.m_clod.m_spacing = 0;
            if (invalid == 4) f.m_plan.m_regular.m_start.SetX(std::numeric_limits<float>::infinity());
            f.Assess();
            EXPECT_TRUE(f.Has(Reason::InvalidLayout));
        }
        Fixture f;
        f.m_plan.m_area.m_captured = false;
        f.Assess();
        EXPECT_TRUE(f.Has(Reason::AreaDecisionMissing));
        f.m_plan.m_area.m_captured = true;
        f.m_plan.m_area.m_exists = false;
        f.m_publication->m_renderGeometryQueries.clear();
        f.Assess();
        EXPECT_TRUE(f.Has(Reason::UnownedHeight)); // Captured empty is not proof of retained ownership.
    }

    TEST(TerrainSectorSamplingTests, PublicationAndDependencyIdentityAreNotRefreshedByAssessment)
    {
        Fixture f;
        f.m_dependency->Invalidate();
        f.Assess();
        EXPECT_TRUE(f.Has(Reason::StaleDependency));
        EXPECT_EQ(f.m_tickets.front().m_revision, 0);
        f.m_channel->m_snapshot.store(std::make_shared<TerrainMeshCutoutRenderSnapshot>());
        f.Assess();
        EXPECT_TRUE(f.Has(Reason::StalePublication));
        f.m_channel->m_snapshot.store(f.m_publication);
        f.m_channel->m_active = false;
        f.Assess();
        EXPECT_TRUE(f.Has(Reason::StalePublication));
        f.m_tickets.clear();
        f.Assess();
        EXPECT_TRUE(f.Has(Reason::MissingInvalidation));
        auto mismatched = std::make_shared<TerrainRenderQuerySources>();
        mismatched->m_publication = std::make_shared<TerrainMeshCutoutRenderSnapshot>();
        f.m_plan.m_sources = mismatched;
        f.m_plan.Assess();
        EXPECT_TRUE(f.Has(Reason::StalePublication));
    }

    TEST(TerrainSectorSamplingTests, BuiltInDirectSamplersMasksAndDisappearingSourcesRemainConservative)
    {
        SnapshotTestSupport::Composition scene;
        Fixture f;
        const auto assess = [&]
        {
            const auto sources = scene.Capture();
            f.m_plan.m_publication = scene.Publication();
            f.m_plan.m_sources = sources;
            f.m_plan.m_channel = scene.Channel();
            f.m_plan.m_dependencies = sources->m_dependencies;
            f.m_plan.Assess();
        };
        assess();
        EXPECT_EQ(f.m_plan.m_regularReadiness.m_bothOwned, 25);
        EXPECT_TRUE(f.m_plan.CanAvoidOrdinaryResults());
        for (auto sampler : { Sampler::EXACT, Sampler::BILINEAR })
        {
            f.m_plan.m_clod.m_sampler = sampler;
            assess();
            EXPECT_FALSE(f.m_plan.CanAvoidOrdinaryResults());
            EXPECT_TRUE(f.Has(Reason::SamplerEquivalenceUnproven));
        }
        f.m_plan.m_clod.m_sampler = Sampler::CLAMP;
        for (auto mask : { AZ::EntityId(99), scene.m_sourceId })
        {
            scene.m_config.m_holeMask.m_gradientId = mask;
            scene.m_source->ReadInConfig(&scene.m_config);
            assess();
            EXPECT_TRUE(f.Has(Reason::ExternalMask));
            EXPECT_TRUE(f.Has(Reason::LiveSource));
            EXPECT_TRUE(f.Has(Reason::InputZ));
        }
        scene.StopSource();
        assess();
        EXPECT_TRUE(f.Has(Reason::UnavailableSource));
        EXPECT_FALSE(f.m_plan.CanExecuteAcrossFrames());
    }
}
