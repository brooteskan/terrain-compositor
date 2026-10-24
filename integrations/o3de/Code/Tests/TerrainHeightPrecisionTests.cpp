#include <AzTest/AzTest.h>
#include <TerrainCompositor/TerrainHeightPrecision.h>
#include <TerrainCompositor/TerrainOverlap.h>
#include <TerrainCompositor/TerrainMeshHeightData.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <limits>

namespace TerrainCompositor
{
    TEST(TerrainHeightPrecision, HalfPackingHasIeeeEndpointsSubnormalsAndEvenRounding)
    {
        EXPECT_EQ(PackTerrainHeight16(0.0f), 0x0000);
        EXPECT_EQ(PackTerrainHeight16(-0.0f), 0x8000);
        EXPECT_EQ(PackTerrainHeight16(-1.0f), 0xbc00); // same missing-terrain sentinel
        EXPECT_EQ(PackTerrainHeight16(1.0f), 0x3c00);
        EXPECT_EQ(PackTerrainHeight16(65504.0f), 0x7bff);
        EXPECT_EQ(PackTerrainHeight16(0.000000059604644775390625f), 1);
        EXPECT_EQ(PackTerrainHeight16(0.0000000298023223876953125f), 0);
        EXPECT_EQ(PackTerrainHeight16(1.00048828125f), 0x3c00);
        EXPECT_EQ(PackTerrainHeight16(1.00146484375f), 0x3c02);
        EXPECT_EQ(PackTerrainHeight16(std::numeric_limits<float>::infinity()), 0x7c00);
    }

    TEST(TerrainHeightPrecision, ImageAndMeshSamplesPackInSelectedImmutablePublication)
    {
        for (bool mesh : {false, true})
        {
            PreparedHeightContributor c;
            c.m_type = mesh ? PreparedHeightContributor::Type::Mesh : PreparedHeightContributor::Type::Image;
            if (mesh)
            {
                c.m_mesh.m_stampEntityId = AZ::EntityId(10);
                c.m_mesh.m_affectTerrainRendering = true;
                auto data = AZStd::make_shared<TerrainMeshHeightData>();
                data->m_width = data->m_height = 2;
                data->m_localHeights = {0.0f, 1.0f, -1.0f, 0.5f};
                c.m_mesh.m_data = data;
            }
            else
            {
                c.m_image.m_placement.m_stampEntityId = AZ::EntityId(10);
                auto data = AZStd::make_shared<HeightmapData>();
                data->m_width = data->m_height = 2;
                data->m_samples = {0.0f, 1.0f, -1.0f, 0.5f};
                c.m_image.m_image = data;
            }
            TerrainOverlapConfig config; config.m_enabled = true; config.m_flatGround = true;
            config.m_sources.push_back({AZ::EntityId(10), 2});
            AZStd::vector<PreparedHeightContributor> contributors{c};
            auto prepared = PrepareTerrainOverlap(config, contributors);
            ASSERT_TRUE(prepared.IsActive());
            AZStd::vector<AZ::u32> full(8), half(8);
            auto bounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-10,-10,-10), AZ::Vector3(10,10,10));
            ASSERT_TRUE(AppendTerrainOverlapRegion(full, 0, bounds, prepared, contributors));
            prepared.m_heightFp16 = true;
            ASSERT_TRUE(AppendTerrainOverlapRegion(half, 0, bounds, prepared, contributors));
            EXPECT_EQ(half.size() + 2, full.size());
            EXPECT_EQ(half[7] & 2u, 2u);
            const auto samples = half[8 + 24 + 19];
            EXPECT_EQ(half[samples], 0x3c000000u);
            EXPECT_EQ(half[samples + 1], 0x3800bc00u);
        }
    }
}
