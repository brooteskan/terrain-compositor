#include <AzTest/AzTest.h>
#include <AzCore/IO/ByteContainerStream.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/Utils.h>
#include <TerrainCompositor/Components/HeightmapStampConfig.h>
#include <TerrainCompositor/Components/TerrainCompositionGradientComponent.h>
#include <TerrainCompositor/TerrainQuality.h>
#include <Tests/Mocks/Terrain/MockTerrainDataRequestBus.h>

#include <limits>

namespace TerrainCompositor
{
    using ::testing::NiceMock;

    namespace
    {
        struct HeightmapStampConfigV6
        {
            AZ_TYPE_INFO(HeightmapStampConfigV6, "{B8F5F014-E660-419C-8D7F-1B083C4EE514}");

            float m_strength = 0.25f;
        };

        struct TerrainCompositionConfigV2
        {
            AZ_TYPE_INFO(TerrainCompositionConfigV2, "{3E33FAE3-51F5-4E56-97AE-02E857E7257B}");
        };
    } // namespace

    TEST(TerrainQualityConfigurationTests, NewFieldsHaveCompatibilityDefaultsAndAreReflectedAtCurrentVersions)
    {
        HeightmapStampConfig stamp;
        EXPECT_EQ(stamp.m_samplingMode, HeightmapSamplingMode::Bilinear);
        EXPECT_FLOAT_EQ(stamp.m_reconstructionRadius, 0.0f);

        TerrainCompositionConfig composition;
        EXPECT_FALSE(composition.m_terrainQuality.m_overrideTerrainQuality);
        EXPECT_FLOAT_EQ(composition.m_terrainQuality.m_heightQueryResolution, 1.0f);
        EXPECT_FALSE(composition.m_terrainQuality.m_renderer.m_overrideMeshSettings);

        AZ::SerializeContext serializeContext;
        HeightmapStampConfig::Reflect(&serializeContext);
        TerrainCompositionConfig::Reflect(&serializeContext);
        const AZ::SerializeContext::ClassData* stampClass =
            serializeContext.FindClassData(azrtti_typeid<HeightmapStampConfig>());
        const AZ::SerializeContext::ClassData* compositionClass =
            serializeContext.FindClassData(azrtti_typeid<TerrainCompositionConfig>());
        ASSERT_NE(stampClass, nullptr);
        ASSERT_NE(compositionClass, nullptr);
        const unsigned int stampVersion = stampClass->m_version;
        const unsigned int compositionVersion = compositionClass->m_version;

        serializeContext.EnableRemoveReflection();
        TerrainCompositionConfig::Reflect(&serializeContext);
        HeightmapStampConfig::Reflect(&serializeContext);
        serializeContext.DisableRemoveReflection();

        EXPECT_EQ(stampVersion, 7);
        EXPECT_EQ(compositionVersion, 3);
    }

    TEST(TerrainQualityConfigurationTests, LegacyConfigurationsLoadWithCompatibilityDefaults)
    {
        AZ::SerializeContext currentContext;
        HeightmapStampConfig::Reflect(&currentContext);
        TerrainCompositionConfig::Reflect(&currentContext);

        AZ::SerializeContext legacyContext;
        legacyContext.Class<HeightmapStampConfigV6>()
            ->Version(6)
            ->Field("Strength", &HeightmapStampConfigV6::m_strength);
        legacyContext.Class<TerrainCompositionConfigV2>()->Version(2);

        HeightmapStampConfigV6 legacyStamp;
        AZStd::vector<char> stampBuffer;
        AZ::IO::ByteContainerStream<AZStd::vector<char>> stampStream(&stampBuffer);
        ASSERT_TRUE(AZ::Utils::SaveObjectToStream(
            stampStream, AZ::ObjectStream::ST_XML, &legacyStamp, &legacyContext));
        AZStd::unique_ptr<HeightmapStampConfig> stamp(
            AZ::Utils::LoadObjectFromBuffer<HeightmapStampConfig>(stampBuffer.data(), stampBuffer.size(), &currentContext));
        ASSERT_NE(stamp, nullptr);
        EXPECT_FLOAT_EQ(stamp->m_strength, 0.25f);
        EXPECT_EQ(stamp->m_samplingMode, HeightmapSamplingMode::Bilinear);
        EXPECT_FLOAT_EQ(stamp->m_reconstructionRadius, 0.0f);

        TerrainCompositionConfigV2 legacyComposition;
        AZStd::vector<char> compositionBuffer;
        AZ::IO::ByteContainerStream<AZStd::vector<char>> compositionStream(&compositionBuffer);
        ASSERT_TRUE(AZ::Utils::SaveObjectToStream(
            compositionStream, AZ::ObjectStream::ST_XML, &legacyComposition, &legacyContext));
        AZStd::unique_ptr<TerrainCompositionConfig> composition(
            AZ::Utils::LoadObjectFromBuffer<TerrainCompositionConfig>(
                compositionBuffer.data(), compositionBuffer.size(), &currentContext));
        ASSERT_NE(composition, nullptr);
        EXPECT_FALSE(composition->m_proceduralSourceEntityId.IsValid());
        EXPECT_FALSE(composition->m_terrainQuality.m_overrideTerrainQuality);
        EXPECT_FALSE(composition->m_terrainQuality.m_renderer.m_overrideMeshSettings);

        currentContext.EnableRemoveReflection();
        TerrainCompositionConfig::Reflect(&currentContext);
        HeightmapStampConfig::Reflect(&currentContext);
        currentContext.DisableRemoveReflection();
    }

    TEST(TerrainQualityControllerTests, DisabledAndInvalidConfigurationsAreReportedWithoutApplying)
    {
        TerrainQualityController controller;
        controller.Activate(AzFramework::EntityContextId::CreateRandom(), AZ::EntityId(1001));
        EXPECT_EQ(controller.GetStatus(), TerrainQualityStatus::Disabled);

        TerrainQualityConfig invalid;
        invalid.m_overrideTerrainQuality = true;
        invalid.m_heightQueryResolution = std::numeric_limits<float>::quiet_NaN();
        controller.Update(invalid);

        EXPECT_EQ(controller.GetStatus(), TerrainQualityStatus::Invalid);
        EXPECT_FALSE(controller.ConsumeHeightSettingsChanged());
        controller.Deactivate();
    }

    TEST(TerrainQualityControllerTests, AppliesUpdatesAndRestoresHeightResolutionWhenDisabled)
    {
        float activeResolution = 1.0f;
        NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        ON_CALL(terrain, GetTerrainHeightQueryResolution).WillByDefault([&activeResolution]()
        {
            return activeResolution;
        });
        ON_CALL(terrain, SetTerrainHeightQueryResolution).WillByDefault([&activeResolution](float value)
        {
            activeResolution = value;
        });

        TerrainQualityController controller;
        controller.Activate(AzFramework::EntityContextId::CreateRandom(), AZ::EntityId(1001));
        TerrainQualityConfig enabled;
        enabled.m_overrideTerrainQuality = true;
        enabled.m_heightQueryResolution = 0.5f;
        controller.Update(enabled);
        EXPECT_EQ(controller.GetStatus(), TerrainQualityStatus::Applied);
        EXPECT_FLOAT_EQ(activeResolution, 0.5f);

        enabled.m_heightQueryResolution = 0.25f;
        controller.Update(enabled);
        EXPECT_EQ(controller.GetStatus(), TerrainQualityStatus::Applied);
        EXPECT_FLOAT_EQ(activeResolution, 0.25f);

        enabled.m_overrideTerrainQuality = false;
        controller.Update(enabled);
        EXPECT_EQ(controller.GetStatus(), TerrainQualityStatus::Disabled);
        EXPECT_FLOAT_EQ(activeResolution, 1.0f);
        EXPECT_TRUE(controller.ConsumeHeightSettingsChanged());
        EXPECT_FALSE(controller.ConsumeHeightSettingsChanged());
        controller.Deactivate();
    }

    TEST(TerrainQualityControllerTests, MultipleClaimsSuppressEveryOverrideUntilOneRemains)
    {
        TerrainQualityController first;
        TerrainQualityController second;
        TerrainQualityConfig enabled;
        enabled.m_overrideTerrainQuality = true;

        first.Activate(AzFramework::EntityContextId::CreateRandom(), AZ::EntityId(1001));
        first.Update(enabled);
        EXPECT_EQ(first.GetStatus(), TerrainQualityStatus::Pending);

        second.Activate(AzFramework::EntityContextId::CreateRandom(), AZ::EntityId(1002));
        second.Update(enabled);
        EXPECT_EQ(first.GetStatus(), TerrainQualityStatus::Conflict);
        EXPECT_EQ(second.GetStatus(), TerrainQualityStatus::Conflict);

        second.Deactivate();
        EXPECT_EQ(first.GetStatus(), TerrainQualityStatus::Pending);
        first.Deactivate();
    }
} // namespace TerrainCompositor
