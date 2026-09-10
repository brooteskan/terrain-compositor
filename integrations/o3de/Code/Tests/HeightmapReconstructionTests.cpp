#include <AzTest/AzTest.h>
#include <TerrainCompositor/HeightmapStampSampling.h>
#include "TerrainTestFixtures.h"

#include <algorithm>

namespace TerrainCompositor
{
    namespace
    {
        using TestSupport::MakeImage;

        PreparedHeightmapStamp MakeStamp(const HeightmapDataPtr& image, HeightmapReconstructionDataPtr reconstruction = {})
        {
            PreparedHeightmapStamp stamp;
            stamp.m_image = image;
            stamp.m_reconstruction = AZStd::move(reconstruction);
            stamp.m_placement.m_worldBounds = AZ::Aabb::CreateFromMinMax(
                AZ::Vector3(-32.0f, -32.0f, 0.0f), AZ::Vector3(32.0f, 32.0f, 0.0f));
            stamp.m_placement.m_halfWidth = 32.0;
            stamp.m_placement.m_halfDepth = 32.0;
            stamp.m_heightRange = 1.0;
            stamp.m_strength = 1.0;
            stamp.m_relativeEdgeBlend = false;
            return stamp;
        }

        float SampleStamp(const PreparedHeightmapStamp& stamp, float x, float y)
        {
            return ComposeHeightmapStamps(
                AZ::Vector3(x, y, 0.0f), 0.0f, HeightmapRegionMapping{ 0.0, 1.0 },
                AZStd::span<const PreparedHeightmapStamp>(&stamp, 1));
        }
    }

    TEST(HeightmapReconstructionTests, RadiusZeroAndInvalidSourcesDoNotAllocateAReplacement)
    {
        const HeightmapData image = MakeImage(2, 2, { 0.0f, 0.25f, 0.75f, 1.0f });
        EXPECT_FALSE(CreateHeightmapReconstruction(image, 0.0f));
        EXPECT_FALSE(CreateHeightmapReconstruction(image, -1.0f));
        EXPECT_FALSE(CreateHeightmapReconstruction(image, 9.0f));

        HeightmapData malformed = image;
        malformed.m_samples.pop_back();
        EXPECT_FALSE(CreateHeightmapReconstruction(malformed, 1.0f));
    }

    TEST(HeightmapReconstructionTests, ConstantFieldsAndPlateausArePreservedExactly)
    {
        const HeightmapData image = MakeImage(7, 5, AZStd::vector<float>(35, 0.375f));
        for (const float radius : { 0.25f, 1.0f, 2.0f, 8.0f })
        {
            const auto reconstructed = CreateHeightmapReconstruction(image, radius);
            ASSERT_TRUE(reconstructed);
            ASSERT_EQ(reconstructed->m_samples.size(), image.m_samples.size());
            EXPECT_TRUE(AZStd::all_of(reconstructed->m_samples.begin(), reconstructed->m_samples.end(),
                [](float value) { return value == 0.375f; }));
        }
    }

    TEST(HeightmapReconstructionTests, SharpStepRemainsMonotonicAndInsideSourceBounds)
    {
        const HeightmapData image = MakeImage(11, 1,
            { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f });
        const auto reconstructed = CreateHeightmapReconstruction(image, 1.5f);
        ASSERT_TRUE(reconstructed);
        ASSERT_EQ(reconstructed->m_samples.size(), image.m_samples.size());
        for (size_t index = 0; index < reconstructed->m_samples.size(); ++index)
        {
            EXPECT_GE(reconstructed->m_samples[index], 0.0f);
            EXPECT_LE(reconstructed->m_samples[index], 1.0f);
            if (index > 0)
            {
                EXPECT_LE(reconstructed->m_samples[index - 1], reconstructed->m_samples[index]);
            }
        }
        EXPECT_EQ(reconstructed->m_samples.front(), 0.0f);
        EXPECT_EQ(reconstructed->m_samples.back(), 1.0f);
        EXPECT_GT(reconstructed->m_samples[4], 0.0f);
        EXPECT_LT(reconstructed->m_samples[5], 1.0f);
    }

    TEST(HeightmapReconstructionTests, SeparableFilterDoesNotCreateNewTwoDimensionalExtrema)
    {
        const HeightmapData image = MakeImage(5, 5,
            { 0.2f, 0.2f, 0.2f, 0.2f, 0.2f,
              0.2f, 0.4f, 0.4f, 0.4f, 0.2f,
              0.2f, 0.4f, 0.9f, 0.4f, 0.2f,
              0.2f, 0.4f, 0.4f, 0.4f, 0.2f,
              0.2f, 0.2f, 0.2f, 0.2f, 0.2f });
        const auto reconstructed = CreateHeightmapReconstruction(image, 2.0f);
        ASSERT_TRUE(reconstructed);
        for (const float value : reconstructed->m_samples)
        {
            EXPECT_GE(value, 0.2f);
            EXPECT_LE(value, 0.9f);
        }
        EXPECT_LT(reconstructed->m_samples[12], 0.9f);
    }

    TEST(HeightmapReconstructionTests, BilinearModePreservesExistingSamplesExactly)
    {
        const auto image = AZStd::make_shared<HeightmapData>(
            MakeImage(2, 2, { 0.0f, 1.0f, 0.0f, 1.0f }));
        const PreparedHeightmapStamp stamp = MakeStamp(image);

        EXPECT_FLOAT_EQ(SampleStamp(stamp, -32.0f, 0.0f), 0.0f);
        EXPECT_FLOAT_EQ(SampleStamp(stamp, -16.0f, 0.0f), 0.25f);
        EXPECT_FLOAT_EQ(SampleStamp(stamp, 0.0f, 0.0f), 0.5f);
        EXPECT_FLOAT_EQ(SampleStamp(stamp, 32.0f, 0.0f), 1.0f);
    }

    TEST(HeightmapReconstructionTests, ScalarAndBatchCompositionLoopsAgree)
    {
        const auto image = AZStd::make_shared<HeightmapData>(
            MakeImage(3, 3, { 0.0f, 0.2f, 0.4f, 0.1f, 0.5f, 0.8f, 0.3f, 0.7f, 1.0f }));
        const PreparedHeightmapStamp stamp = MakeStamp(image, CreateHeightmapReconstruction(*image, 1.5f));
        const AZStd::vector<AZ::Vector3> positions{
            AZ::Vector3(-31.0f, -17.0f, 0.0f), AZ::Vector3(-8.0f, 13.0f, 0.0f),
            AZ::Vector3(0.0f, 0.0f, 0.0f), AZ::Vector3(21.0f, -4.0f, 0.0f),
            AZ::Vector3(31.0f, 31.0f, 0.0f) };
        AZStd::vector<float> batch(positions.size(), 0.0f);

        for (size_t index = 0; index < positions.size(); ++index)
        {
            const float scalar = SampleStamp(stamp, positions[index].GetX(), positions[index].GetY());
            batch[index] = ComposeHeightmapStamps(
                positions[index], 0.0f, HeightmapRegionMapping{ 0.0, 1.0 },
                AZStd::span<const PreparedHeightmapStamp>(&stamp, 1));
            EXPECT_FLOAT_EQ(batch[index], scalar);
        }
    }

    TEST(HeightmapReconstructionTests, RadialTerraceTransitionSpreadsAtMultipleQueryResolutions)
    {
        constexpr AZ::u32 dimension = 65;
        AZStd::vector<float> samples;
        samples.reserve(size_t(dimension) * dimension);
        for (AZ::u32 y = 0; y < dimension; ++y)
        {
            for (AZ::u32 x = 0; x < dimension; ++x)
            {
                const float dx = static_cast<float>(x) - 32.0f;
                const float dy = static_cast<float>(y) - 32.0f;
                samples.push_back(dx * dx + dy * dy < 16.0f * 16.0f ? 0.0f : 1.0f);
            }
        }
        const auto image = AZStd::make_shared<HeightmapData>(MakeImage(dimension, dimension, AZStd::move(samples)));
        const PreparedHeightmapStamp bilinear = MakeStamp(image);
        const PreparedHeightmapStamp smooth = MakeStamp(image, CreateHeightmapReconstruction(*image, 1.5f));

        for (const float spacing : { 1.0f, 0.5f, 0.25f })
        {
            size_t bilinearTransitionSamples = 0;
            size_t smoothTransitionSamples = 0;
            for (float x = 12.0f; x <= 20.0f; x += spacing)
            {
                const float bilinearValue = SampleStamp(bilinear, x, 0.0f);
                const float smoothValue = SampleStamp(smooth, x, 0.0f);
                bilinearTransitionSamples += bilinearValue > 0.0f && bilinearValue < 1.0f;
                smoothTransitionSamples += smoothValue > 0.0f && smoothValue < 1.0f;
                EXPECT_GE(smoothValue, 0.0f);
                EXPECT_LE(smoothValue, 1.0f);
                EXPECT_FLOAT_EQ(smoothValue, SampleStamp(smooth, 0.0f, x));
            }
            EXPECT_GT(smoothTransitionSamples, bilinearTransitionSamples);
        }
    }
} // namespace TerrainCompositor
