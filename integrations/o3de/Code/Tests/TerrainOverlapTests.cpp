#include <AzTest/AzTest.h>
#include <TerrainCompositor/TerrainOverlap.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <limits>
#include <cstring>

namespace TerrainCompositor
{
    namespace
    {
        PreparedHeightContributor Source(AZ::u64 id)
        {
            PreparedHeightContributor c;
            c.m_image.m_placement.m_stampEntityId = AZ::EntityId(id);
            c.m_image.m_placement.m_halfWidth = c.m_image.m_placement.m_halfDepth = 10;
            c.m_image.m_strength = 1;
            auto image = AZStd::make_shared<HeightmapData>();
            image->m_width = image->m_height = 2; image->m_samples = {0, 1, 2, 3};
            c.m_image.m_image = image;
            return c;
        }
        TerrainOverlapConfig Config()
        {
            TerrainOverlapConfig c; c.m_enabled = true; c.m_flatGround = true;
            c.m_sources.push_back({AZ::EntityId(10), 2}); return c;
        }
        AZ::Aabb Bounds() { return AZ::Aabb::CreateFromMinMax(AZ::Vector3(-100,-100,-10),AZ::Vector3(100,100,100)); }
        float Float(AZ::u32 v) { float f; memcpy(&f,&v,4); return f; }
    }
    TEST(TerrainOverlap, DisabledAndZeroWidthNeedNoSources)
    {
        TerrainOverlapConfig c; EXPECT_FALSE(PrepareTerrainOverlap(c,{}).IsActive());
        c.m_enabled=true;c.m_width=0;
        EXPECT_TRUE(PrepareTerrainOverlap(c,{}).m_diagnostic.empty());
        c.m_width=std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(PrepareTerrainOverlap(c,{}).m_diagnostic.empty());
    }
    TEST(TerrainOverlap, MissingDuplicateAndOverCapacityInputsFailAsAWhole)
    {
        AZStd::vector<PreparedHeightContributor> sources{Source(10)};
        auto c=Config(); EXPECT_TRUE(PrepareTerrainOverlap(c,sources).IsActive());
        EXPECT_FALSE(PrepareTerrainOverlap(c,{}).IsActive());
        c.m_sources.push_back({AZ::EntityId(10),3}); EXPECT_FALSE(PrepareTerrainOverlap(c,sources).IsActive());
        c=Config();c.m_sources.front().m_materialSlot=1;EXPECT_FALSE(PrepareTerrainOverlap(c,sources).IsActive());
        c=Config();c.m_sources.resize(4);EXPECT_FALSE(PrepareTerrainOverlap(c,sources).IsActive());
    }
    TEST(TerrainOverlap, CanonicalSourceOrderDoesNotFollowAuthoringListOrder)
    {
        AZStd::vector<PreparedHeightContributor> sources{Source(10),Source(11)};
        auto c=Config();c.m_sources.insert(c.m_sources.begin(),{AZ::EntityId(11),3});
        auto prepared=PrepareTerrainOverlap(c,sources);ASSERT_TRUE(prepared.IsActive());
        AZStd::vector<AZ::u32> words(8);
        ASSERT_TRUE(AppendTerrainOverlapRegion(words,0,Bounds(),prepared,sources));
        EXPECT_EQ(words[5],3); EXPECT_EQ(words[8+1],0);EXPECT_EQ(words[8+24+1],1);EXPECT_EQ(words[8+48+1],2);
        EXPECT_FLOAT_EQ(Float(words[8+24+9]),-10);
        const auto payload=words[8+24+19]; EXPECT_FLOAT_EQ(Float(words[payload+3]),3);
    }
    TEST(TerrainOverlap, ResourceFailureLeavesLegacyWordsUntouched)
    {
        AZStd::vector<PreparedHeightContributor> sources{Source(10)};
        auto prepared=PrepareTerrainOverlap(Config(),sources);
        AZStd::vector<AZ::u32> words(18,0);words[0]=0x12345678;
        const auto before=words;
        EXPECT_FALSE(AppendTerrainOverlapRegion(words,10,Bounds(),prepared,sources,20));
        EXPECT_EQ(words,before);
    }
    TEST(TerrainOverlap, ChangedPlacementAndReconstructedHeightPublishTogether)
    {
        AZStd::vector<PreparedHeightContributor> sources{Source(10)};
        auto reconstruction=AZStd::make_shared<HeightmapReconstructionData>();
        reconstruction->m_width=reconstruction->m_height=2;reconstruction->m_samples={4,5,6,7};
        sources[0].m_image.m_reconstruction=reconstruction;
        sources[0].m_image.m_placement.m_centerX=31;sources[0].m_image.m_heightOrigin=23;
        AZStd::vector<AZ::u32> words(8);
        ASSERT_TRUE(AppendTerrainOverlapRegion(words,0,Bounds(),PrepareTerrainOverlap(Config(),sources),sources));
        EXPECT_FLOAT_EQ(Float(words[8+24+4]),31); EXPECT_FLOAT_EQ(Float(words[8+24+13]),23);
        EXPECT_FLOAT_EQ(Float(words[words[8+24+19]+3]),7);
    }
}
