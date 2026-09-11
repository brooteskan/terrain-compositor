#include <AzTest/AzTest.h>
#include <Atom/RPI.Reflect/Image/StreamingImageAsset.h>
#include <TerrainCompositor/Components/ProceduralGroundGradientComponent.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>
#include <TerrainCompositor/TerrainInvalidation.h>
#include "TerrainTestFixtures.h"

namespace TerrainCompositor
{
    namespace
    {
        using TestSupport::MakeMask;
        using TestSupport::MakePreparedMask;
        using TestSupport::ConstantGradient;

        class BatchHoleGradient final : public GradientSignal::GradientRequestBus::Handler
        {
        public:
            explicit BatchHoleGradient(AZ::EntityId id)
            {
                BusConnect(id);
            }
            ~BatchHoleGradient() override
            {
                BusDisconnect();
            }
            float GetValue(const GradientSignal::GradientSampleParams& params) const override
            {
                ++m_scalarCalls;
                return 0.5f + params.m_position.GetX() * 0.001f;
            }
            void GetValues(AZStd::span<const AZ::Vector3> positions, AZStd::span<float> values) const override
            {
                ++m_batchCalls;
                for (size_t i = 0; i < positions.size(); ++i)
                    values[i] = 0.5f + positions[i].GetX() * 0.001f;
            }
            mutable size_t m_scalarCalls = 0;
            mutable size_t m_batchCalls = 0;
        };
    } // namespace

    TEST(TerrainExistenceSamplingTests, AbsentMaskIsNeutral)
    {
        HeightmapStampRegistrationData registration;
        PreparedTerrainExistenceStamp prepared;
        EXPECT_EQ(PrepareTerrainExistenceStamp(registration, false, prepared),
            TerrainExistenceStampValidation::Absent);
    }

    TEST(TerrainExistenceSamplingTests, HoleOnlyStampIgnoresHeightStrengthAndFeather)
    {
        HeightmapStampRegistrationData registration;
        registration.m_configuration.m_footprintWidth = 4.0f;
        registration.m_configuration.m_footprintDepth = 6.0f;
        registration.m_configuration.m_strength = 0.0f;
        registration.m_configuration.m_featherWidth = 1000.0f;
        registration.m_configuration.m_holeMask.m_maskAsset =
            AZ::Data::Asset<AZ::RPI::StreamingImageAsset>(
                AZ::Data::AssetId(AZ::Uuid::CreateRandom()),
                azrtti_typeid<AZ::RPI::StreamingImageAsset>(), {});
        registration.m_holeMask.m_status = HeightmapDataStatus::Ready;
        registration.m_holeMask.m_data = MakeMask(1.0f);

        PreparedTerrainExistenceStamp prepared;
        EXPECT_EQ(PrepareTerrainExistenceStamp(registration, false, prepared),
            TerrainExistenceStampValidation::Valid);
        EXPECT_DOUBLE_EQ(prepared.m_placement.m_halfWidth, 2.0);
        EXPECT_DOUBLE_EQ(prepared.m_placement.m_halfDepth, 3.0);
    }

    TEST(TerrainExistenceSamplingTests, ThresholdIsInclusiveAndNeutralSamplesPreserveLowerState)
    {
        bool exists = true;
        auto active = MakePreparedMask(0.5f, TerrainExistenceOperation::RemoveTerrain);
        EXPECT_TRUE(SampleTerrainExistenceStamp(AZ::Vector3::CreateZero(), active, exists));
        EXPECT_FALSE(exists);

        exists = false;
        auto neutral = MakePreparedMask(0.499f, TerrainExistenceOperation::RestoreTerrain);
        EXPECT_FALSE(SampleTerrainExistenceStamp(AZ::Vector3::CreateZero(), neutral, exists));
        EXPECT_FALSE(exists);
    }

    TEST(TerrainExistenceSamplingTests, LaterActiveStampOverwritesEarlierExistence)
    {
        AZStd::vector<PreparedTerrainExistenceStamp> stamps;
        stamps.push_back(MakePreparedMask(1.0f, TerrainExistenceOperation::RemoveTerrain));
        stamps.push_back(MakePreparedMask(1.0f, TerrainExistenceOperation::RestoreTerrain));
        EXPECT_TRUE(ComposeTerrainExists(AZ::Vector3::CreateZero(), true, stamps));

        AZStd::swap(stamps[0], stamps[1]);
        EXPECT_FALSE(ComposeTerrainExists(AZ::Vector3::CreateZero(), true, stamps));
    }

    TEST(ProceduralGroundExistenceTests, ScalarAndBatchUseTheSameOptionalHoleMask)
    {
        AZStd::unique_ptr<AZ::ComponentDescriptor> descriptor(ProceduralGroundGradientComponent::CreateDescriptor());
        const AZ::EntityId maskEntityId(91'023);
        ConstantGradient mask(maskEntityId, 0.75f);
        ProceduralGroundGradientConfig configuration;
        configuration.m_holeMask.m_gradientId = maskEntityId;
        configuration.m_holeThreshold = 0.5f;

        AZ::Entity entity("Procedural existence source");
        entity.CreateComponent<ProceduralGroundGradientComponent>(configuration);
        entity.Init();
        entity.Activate();

        bool scalarExists = true;
        TerrainExistenceSourceRequestBus::EventResult(
            scalarExists, entity.GetId(), &TerrainExistenceSourceRequestBus::Events::GetTerrainExists,
            AZ::Vector3::CreateZero());
        AZ::Vector3 positions[2] = { AZ::Vector3::CreateZero(), AZ::Vector3(10.0f, 20.0f, 0.0f) };
        bool batchExists[2] = { true, true };
        TerrainExistenceSourceRequestBus::Event(
            entity.GetId(), &TerrainExistenceSourceRequestBus::Events::GetTerrainExistsFromList,
            AZStd::span<const AZ::Vector3>(positions, 2), AZStd::span<bool>(batchExists, 2));

        EXPECT_FALSE(scalarExists);
        EXPECT_FALSE(batchExists[0]);
        EXPECT_FALSE(batchExists[1]);
        entity.Deactivate();
    }

    TEST(ProceduralGroundExistenceTests, BatchedMaskUsesBulkSamplerAcrossScratchBoundaries)
    {
        AZStd::unique_ptr<AZ::ComponentDescriptor> descriptor(ProceduralGroundGradientComponent::CreateDescriptor());
        const AZ::EntityId maskId(91'024);
        BatchHoleGradient mask(maskId);
        ProceduralGroundGradientConfig configuration;
        configuration.m_holeMask.m_gradientId = maskId;
        configuration.m_holeMask.m_invertInput = true;
        configuration.m_holeMask.m_opacity = 0.8f;
        configuration.m_holeThreshold = 0.4f;
        AZ::Entity entity("Batched hole source");
        entity.CreateComponent<ProceduralGroundGradientComponent>(configuration);
        entity.Init();
        entity.Activate();
        AZStd::vector<AZ::Vector3> positions;
        for (int i = -256; i <= 256; ++i)
            positions.emplace_back(float(i), 0.0f, -1000.0f);
        bool exists[513]{};
        TerrainExistenceSourceRequestBus::Event(
            entity.GetId(),
            &TerrainExistenceSourceRequestBus::Events::GetTerrainExistsFromList,
            AZStd::span<const AZ::Vector3>(positions),
            AZStd::span<bool>(exists));
        EXPECT_EQ(mask.m_batchCalls, 3);
        EXPECT_EQ(mask.m_scalarCalls, 0);
        EXPECT_FALSE(exists[256]); // Threshold equality removes terrain, including after sampler transforms.
        for (size_t i = 0; i < positions.size(); ++i)
        {
            bool scalar = true;
            TerrainExistenceSourceRequestBus::EventResult(
                scalar, entity.GetId(), &TerrainExistenceSourceRequestBus::Events::GetTerrainExists, positions[i]);
            EXPECT_EQ(exists[i], scalar);
        }
        entity.Deactivate();
    }

    TEST(ProceduralGroundExistenceTests, BatchedMissingAndInvalidMaskFailOpen)
    {
        AZStd::unique_ptr<AZ::ComponentDescriptor> descriptor(ProceduralGroundGradientComponent::CreateDescriptor());
        const AZ::EntityId maskId(91'025);
        BatchHoleGradient mask(maskId);
        for (int mode = 0; mode < 3; ++mode)
        {
            ProceduralGroundGradientConfig configuration;
            configuration.m_holeMask.m_gradientId = mode == 0 ? AZ::EntityId() : maskId;
            configuration.m_holeThreshold = mode == 1 ? -0.1f : (mode == 2 ? 1.1f : 0.5f);
            AZ::Entity entity("Invalid hole source");
            entity.CreateComponent<ProceduralGroundGradientComponent>(configuration);
            entity.Init();
            entity.Activate();
            AZ::Vector3 position[] = { AZ::Vector3::CreateZero() };
            bool exists[] = { false };
            TerrainExistenceSourceRequestBus::Event(
                entity.GetId(),
                &TerrainExistenceSourceRequestBus::Events::GetTerrainExistsFromList,
                AZStd::span<const AZ::Vector3>(position),
                AZStd::span<bool>(exists));
            EXPECT_TRUE(exists[0]);
            entity.Deactivate();
        }
        EXPECT_EQ(mask.m_batchCalls, 0);
        EXPECT_EQ(mask.m_scalarCalls, 0);
    }

    TEST(TerrainExistenceInvalidationTests, OldAndNewHoleFootprintsRemainGranular)
    {
        TerrainInvalidation invalidation;
        const AZ::EntityId regionId(55'001);
        const AZ::Aabb region = AZ::Aabb::CreateFromMinMax(
            AZ::Vector3(-100.0f, -100.0f, -10.0f), AZ::Vector3(100.0f, 100.0f, 10.0f));
        invalidation.AddFootprint(regionId, region, AZ::Aabb::CreateFromMinMax(
            AZ::Vector3(-20.0f, -20.0f, 0.0f), AZ::Vector3(-10.0f, -10.0f, 0.0f)));
        invalidation.AddFootprint(regionId, region, AZ::Aabb::CreateFromMinMax(
            AZ::Vector3(30.0f, 30.0f, 0.0f), AZ::Vector3(40.0f, 40.0f, 0.0f)));

        const auto regions = invalidation.BuildRegions(1.0f);
        ASSERT_EQ(regions.size(), 2);
        EXPECT_EQ(regions[0], AZ::Aabb::CreateFromMinMax(
            AZ::Vector3(-22.0f, -22.0f, -10.0f), AZ::Vector3(-8.0f, -8.0f, 10.0f)));
        EXPECT_EQ(regions[1], AZ::Aabb::CreateFromMinMax(
            AZ::Vector3(28.0f, 28.0f, -10.0f), AZ::Vector3(42.0f, 42.0f, 10.0f)));
    }
} // namespace TerrainCompositor

