#include <AzTest/AzTest.h>
#include <TerrainCompositor/Components/HeightmapStampConfig.h>
#include "ComponentConfiguration.h"
#include "StampMath.h"

namespace TerrainCompositor
{
    TEST(ComponentConfigurationTests, CopiesMatchingTypesAndRejectsNullOrWrongTypesWithoutApplying)
    {
        HeightmapStampConfig input, output;
        AZ::ComponentConfig wrongType;
        input.m_footprintWidth = 37.0f;
        int applications = 0;
        const auto apply = [&](const auto& value) { output = value; ++applications; };
        EXPECT_FALSE(Internal::ReadConfiguration<HeightmapStampConfig>(nullptr, apply));
        EXPECT_FALSE(Internal::ReadConfiguration<HeightmapStampConfig>(&wrongType, apply));
        EXPECT_EQ(applications, 0);
        EXPECT_TRUE(Internal::ReadConfiguration<HeightmapStampConfig>(&input, apply));
        EXPECT_EQ(applications, 1);
        EXPECT_EQ(output.m_footprintWidth, 37.0f);
        output.m_footprintWidth = 12.0f;
        EXPECT_FALSE(Internal::WriteConfiguration(nullptr, input));
        EXPECT_FALSE(Internal::WriteConfiguration(&wrongType, input));
        EXPECT_TRUE(Internal::WriteConfiguration(&output, input));
        EXPECT_EQ(output.m_footprintWidth, 37.0f);
    }

    TEST(StampMathTests, OutwardRoundingUsesTheNearestConservativeFloat)
    {
        const float infinity = std::numeric_limits<float>::infinity();
        for (const double value : { 0.0, 1.0, -1.0, 0.1, -0.1, 1.0e40, -1.0e40 })
        {
            const float lower = Internal::RoundOutward(value, true);
            const float upper = Internal::RoundOutward(value, false);
            EXPECT_LE(double(lower), value);
            EXPECT_GE(double(upper), value);
            EXPECT_GT(double(std::nextafter(lower, infinity)), value);
            EXPECT_LT(double(std::nextafter(upper, -infinity)), value);
        }
    }

    TEST(StampMathTests, SmoothStepClampsAndPreservesTheCubicCurve)
    {
        const double values[] = { -0.5, 0.0, 0.25, 0.5, 0.75, 1.0, 1.5 };
        const double expected[] = { 0.0, 0.0, 0.15625, 0.5, 0.84375, 1.0, 1.0 };
        for (size_t index = 0; index < AZ_ARRAY_SIZE(values); ++index)
        {
            EXPECT_DOUBLE_EQ(Internal::SmoothStep01(values[index]), expected[index]);
        }
    }
}
