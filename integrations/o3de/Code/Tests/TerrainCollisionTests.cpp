#include <AzTest/AzTest.h>
#include <AzCore/Serialization/Json/JsonSerialization.h>
#include <AzCore/Serialization/Json/RegistrationContext.h>
#include <AzCore/Serialization/Json/JsonSystemComponent.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <TerrainCompositor/Components/TerrainCompositionHeightProviderComponent.h>
#include <TerrainCompositor/Internal/TerrainCollision.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>
#include <limits>
#include <cmath>

namespace TerrainCompositor
{
    TEST(TerrainCollision, HoleCellsSurviveCoarserAndFinerPhysicsGridsWithoutRemovingAdjacentCells)
    {
        auto cells = std::make_shared<PreparedTerrainHeightfieldCellMask>();
        cells->m_gridSpacing = 1.0f;
        cells->m_cells = {{-1, -1}, {2, 3}};
        PreparedTerrainMeshHeightGap gap;
        gap.m_affectTerrainCollisionQueries = true;
        gap.m_collisionCells = cells;
        EXPECT_TRUE(IsTerrainMeshHeightGapCollisionCell(AZ::Vector2(-2,-2), AZ::Vector2(2), gap));
        EXPECT_TRUE(IsTerrainMeshHeightGapCollisionCell(AZ::Vector2(2,2), AZ::Vector2(2), gap));
        EXPECT_TRUE(IsTerrainMeshHeightGapCollisionCell(AZ::Vector2(2.5f,3.5f), AZ::Vector2(.5f), gap));
        EXPECT_FALSE(IsTerrainMeshHeightGapCollisionCell(AZ::Vector2(3,3), AZ::Vector2(.5f), gap));
        EXPECT_FALSE(IsTerrainMeshHeightGapCollisionCell(AZ::Vector2(-2,0), AZ::Vector2(2), gap));
        gap.m_affectTerrainCollisionQueries = false;
        EXPECT_FALSE(IsTerrainMeshHeightGapCollisionCell(AZ::Vector2(-2,-2), AZ::Vector2(2), gap));
    }
    TEST(TerrainCollision, ExistingProviderDocumentsEnableCollisionWhenNewFieldsAreAbsent)
    {
        AZ::SerializeContext serialize;
        AZ::JsonRegistrationContext json;
        AZ::JsonSystemComponent::Reflect(&json);
        TerrainCompositionHeightProviderConfig::Reflect(&serialize);
        rapidjson::Document legacy;
        legacy.SetObject();
        AZ::JsonDeserializerSettings settings;
        settings.m_serializeContext = &serialize;
        settings.m_registrationContext = &json;
        TerrainCompositionHeightProviderConfig config;
        const auto result = AZ::JsonSerialization::Load(config, legacy, settings);
        EXPECT_NE(result.GetProcessing(), AZ::JsonSerializationResult::Processing::Halted);
        EXPECT_TRUE(config.m_enableCollisions);
        EXPECT_EQ(config.m_collisionGridSpacing, .5f);
        EXPECT_EQ(config.m_collisionRange, 128.f);
        legacy.AddMember("EnableCollisions", false, legacy.GetAllocator());
        EXPECT_NE(AZ::JsonSerialization::Load(config, legacy, settings).GetProcessing(),
            AZ::JsonSerializationResult::Processing::Halted);
        EXPECT_FALSE(config.m_enableCollisions);
        json.EnableRemoveReflection();
        AZ::JsonSystemComponent::Reflect(&json);
        json.DisableRemoveReflection();
    }

    TEST(TerrainCollision, LargeWorldAllocatesOnlyNearbyCoverageAndDeduplicatesObservers)
    {
        const auto region = AZ::Aabb::CreateFromMinMaxValues(-32000,-32000,-512,32000,32000,512);
        const auto tiles = Internal::PlanTerrainCollisionTiles(region, {AZ::Vector3(576,867,0)}, 128, .5f);
        EXPECT_LE(tiles.size(), 25);
        EXPECT_GE(tiles.size(), 16);
        const auto duplicate = Internal::PlanTerrainCollisionTiles(region, {AZ::Vector3(576,867,0), AZ::Vector3(576,867,200)}, 128, .5f);
        EXPECT_EQ(tiles.size(), duplicate.size());
        for (const auto& [key, bounds] : tiles)
        {
            EXPECT_LE(bounds.GetXExtent(), 64.f);
            EXPECT_LE(bounds.GetYExtent(), 64.f);
            EXPECT_FLOAT_EQ(bounds.GetMin().GetZ(), -512.f);
            if (const auto adjacent = tiles.find({key.first + 1, key.second}); adjacent != tiles.end())
                EXPECT_FLOAT_EQ(bounds.GetMax().GetX(), adjacent->second.GetMin().GetX());
            if (const auto adjacent = tiles.find({key.first, key.second + 1}); adjacent != tiles.end())
                EXPECT_FLOAT_EQ(bounds.GetMax().GetY(), adjacent->second.GetMin().GetY());
        }
    }

    TEST(TerrainCollision, MovingObserverReusesWorldAlignedTilesAndRetiresFarCoverage)
    {
        const auto region = AZ::Aabb::CreateFromMinMaxValues(-32000,-32000,-20,32000,32000,20);
        const auto before = Internal::PlanTerrainCollisionTiles(region, {AZ::Vector3(0,0,0)}, 128, .5f);
        const auto after = Internal::PlanTerrainCollisionTiles(region, {AZ::Vector3(64,0,0)}, 128, .5f);
        size_t reused = 0;
        for (const auto& [key, bounds] : before)
            if (const auto found = after.find(key); found != after.end())
            {
                ++reused;
                EXPECT_EQ(bounds.GetMin(), found->second.GetMin());
                EXPECT_EQ(bounds.GetMax(), found->second.GetMax());
            }
        EXPECT_EQ(reused, 12);
        EXPECT_EQ(before.size(), after.size());
    }

    TEST(TerrainCollision, RegionBoundaryClipsToWholeCellsAndNegativeCoordinatesKeepSharedEdges)
    {
        const auto region = AZ::Aabb::CreateFromMinMaxValues(-100.1f,-30.2f,-5,100.1f,90.2f,5);
        const auto tiles = Internal::PlanTerrainCollisionTiles(region, {AZ::Vector3::CreateZero()}, 128, .75f);
        ASSERT_FALSE(tiles.empty());
        for (const auto& [key, bounds] : tiles)
        {
            EXPECT_TRUE(region.Contains(bounds));
            EXPECT_NEAR(bounds.GetMin().GetX() / .75f, std::round(bounds.GetMin().GetX() / .75f), .0001f);
            EXPECT_NEAR(bounds.GetMax().GetY() / .75f, std::round(bounds.GetMax().GetY() / .75f), .0001f);
            EXPECT_GT(bounds.GetXExtent(), 0.f);
        }
    }

    TEST(TerrainCollision, InvalidInputCannotCreateAnUnboundedPhysicsGrid)
    {
        const auto region = AZ::Aabb::CreateFromMinMaxValues(-32000,-32000,-5,32000,32000,5);
        const AZStd::vector<AZ::Vector3> observers{AZ::Vector3::CreateZero()};
        EXPECT_TRUE(Internal::PlanTerrainCollisionTiles(AZ::Aabb::CreateNull(), observers, 128, .5f).empty());
        EXPECT_TRUE(Internal::PlanTerrainCollisionTiles(region, observers, 1.e9f, .5f).empty());
        EXPECT_TRUE(Internal::PlanTerrainCollisionTiles(region, observers, 128, 0).empty());
        EXPECT_TRUE(Internal::PlanTerrainCollisionTiles(region, observers, 128, std::numeric_limits<float>::quiet_NaN()).empty());
        EXPECT_TRUE(Internal::PlanTerrainCollisionTiles(region, {AZ::Vector3(1.e30f,0,0)}, 128, .5f).empty());
        EXPECT_TRUE(Internal::PlanTerrainCollisionTiles(region, {}, 128, .5f).empty());
    }
}
