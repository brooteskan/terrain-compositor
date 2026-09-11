#include <AzTest/AzTest.h>
#include <TerrainCompositor/Components/HeightmapStampConfig.h>
#include "ComponentConfiguration.h"
#include <TerrainCompositor/Internal/CompositionRegistrationState.h>
#include "PublicationState.h"
#include <TerrainCompositor/SurfaceStampSampling.h>
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

    TEST(PublicationFootprintsTests, PreservesFirstClaimAndDistinctImageCutoutRenderAndQueryCoverage)
    {
        struct State
        {
            AZStd::vector<PreparedHeightContributor> m_heightContributors;
            AZStd::vector<PreparedSurfaceStamp> m_surfaceStamps;
            AZStd::vector<PreparedTerrainExistenceContributor> m_existenceContributors;
            AZStd::vector<PreparedTerrainMeshHeightGap> m_meshHeightGaps;
        } state;
        const AZ::EntityId shared(8400), cutoutId(8401), gapId(8402), imageId(8403);
        const auto small = AZ::Aabb::CreateFromMinMaxValues(1, 2, 0, 3, 4, 0);
        const auto large = AZ::Aabb::CreateFromMinMaxValues(-1, -2, -3, 5, 6, 7);
        PreparedHeightContributor image, mesh;
        image.m_image.m_placement.m_stampEntityId = shared;
        image.m_image.m_placement.m_worldBounds = small;
        mesh.m_type = PreparedHeightContributor::Type::Mesh;
        mesh.m_mesh.m_stampEntityId = shared;
        mesh.m_mesh.m_worldBounds = large;
        state.m_heightContributors = { image, mesh };
        PreparedSurfaceStamp surface;
        surface.m_placement = image.m_image.m_placement;
        state.m_surfaceStamps.push_back(surface);
        PreparedTerrainExistenceContributor cutout, gap, mask;
        cutout.m_type = PreparedTerrainExistenceContributor::Type::MeshCutout;
        cutout.m_meshCutout.m_entityId = cutoutId;
        cutout.m_meshCutout.m_collisionWorldBounds = large;
        cutout.m_meshCutout.m_renderWorldBounds = small;
        gap.m_type = PreparedTerrainExistenceContributor::Type::MeshHeightGap;
        gap.m_meshHeightGap.m_entityId = gapId;
        gap.m_meshHeightGap.m_worldBounds = small;
        gap.m_meshHeightGap.m_collisionWorldBounds = large;
        gap.m_meshHeightGap.m_affectTerrainRendering = true;
        mask.m_imageMask.m_placement.m_stampEntityId = imageId;
        mask.m_imageMask.m_placement.m_worldBounds = small;
        state.m_existenceContributors = { cutout, gap, mask };
        state.m_meshHeightGaps.push_back(gap.m_meshHeightGap);
        const Internal::PublicationFootprints bounds(state);
        EXPECT_EQ(bounds.m_height.at(shared), small);
        EXPECT_EQ(bounds.m_surface.at(shared), small);
        EXPECT_EQ(bounds.m_existence.at(cutoutId), large);
        EXPECT_EQ(bounds.m_existence.at(imageId), small);
        EXPECT_EQ(bounds.m_cutouts.size(), 1);
        EXPECT_EQ(bounds.m_gapRendering.at(gapId), small);
        EXPECT_EQ(bounds.m_gapQueries.at(gapId), large);
        EXPECT_FALSE(bounds.m_existence.contains(gapId));
        state.m_meshHeightGaps[0].m_affectTerrainRendering = false;
        const Internal::PublicationFootprints queryOnly(state);
        EXPECT_TRUE(queryOnly.m_gapRendering.empty());
        EXPECT_EQ(queryOnly.m_gapQueries.at(gapId), large);
    }

    TEST(CompositionRegistrationStateTests, ReconcilesBeforeClassificationAndPreservesCallerAndPendingDirtyBits)
    {
        HeightmapStampRegistrationData incoming;
        incoming.m_stampEntityId = AZ::EntityId(8100);
        const AZ::Data::AssetId asset(AZ::Uuid::CreateRandom(), 1);
        incoming.m_heightmap = { HeightmapDataStatus::Loading, 1, {}, asset };
        auto previous = incoming;
        previous.m_heightmap = { HeightmapDataStatus::Missing, 10, {}, asset };
        AZStd::unordered_map<AZ::EntityId, HeightmapStampRegistrationData> registrations{ { incoming.m_stampEntityId, previous } };
        AZStd::unordered_map<AZ::EntityId, AZ::u8> affected{ { incoming.m_stampEntityId, Internal::DirtyExistence } };
        int classifications = 0;
        Internal::ApplyRegistrationState(incoming, incoming.m_stampEntityId, registrations, affected, Internal::ImageAssetRoles,
            [&](const auto* old, const auto& current)
            {
                ++classifications;
                EXPECT_NE(old, nullptr);
                EXPECT_EQ(current.m_heightmap.m_revision, 10);
                EXPECT_EQ(current.m_heightmap.m_status, HeightmapDataStatus::Missing);
                return Internal::DirtySurface;
            });
        EXPECT_EQ(classifications, 1);
        EXPECT_EQ(incoming.m_heightmap.m_revision, 1);
        EXPECT_EQ(incoming.m_heightmap.m_status, HeightmapDataStatus::Loading);
        EXPECT_EQ(registrations.at(incoming.m_stampEntityId).m_heightmap.m_revision, 10);
        ASSERT_EQ(affected.size(), 1);
        EXPECT_EQ(affected.at(incoming.m_stampEntityId), Internal::DirtySurface | Internal::DirtyExistence);
    }

    TEST(CompositionRegistrationStateTests, EverySourceRoleFansOutOnlyToOlderMatchingAssetsWithTargetDirtyBits)
    {
        const auto check = [](const auto& roles, auto prototype)
        {
            using Registration = decltype(prototype);
            using Status = decltype((prototype.*roles[0].m_snapshot).m_status);
            for (const auto& sourceRole : roles)
            {
                AZStd::unordered_map<AZ::EntityId, Registration> registrations;
                AZStd::unordered_map<AZ::EntityId, AZ::u8> affected;
                const AZ::Data::AssetId asset(AZ::Uuid::CreateRandom(), 1);
                AZ::u64 targetId = 8200;
                for (const auto& targetRole : roles)
                {
                    auto& target = registrations[AZ::EntityId(targetId++)].*targetRole.m_snapshot;
                    target.m_assetId = asset;
                    target.m_revision = 1;
                    target.m_status = Status::Loading;
                }
                auto& equal = registrations[AZ::EntityId(8300)].*roles[0].m_snapshot;
                equal.m_assetId = asset;
                equal.m_revision = 20;
                equal.m_status = Status::Loading;
                auto& unrelated = registrations[AZ::EntityId(8301)].*roles[0].m_snapshot;
                unrelated.m_assetId = AZ::Data::AssetId(AZ::Uuid::CreateRandom(), 1);
                unrelated.m_revision = 1;
                unrelated.m_status = Status::Loading;
                auto incoming = prototype;
                auto& source = incoming.*sourceRole.m_snapshot;
                source.m_assetId = asset;
                source.m_revision = 20;
                source.m_status = Status::Missing;
                affected[AZ::EntityId(8200)] = Internal::DirtySurface;
                Internal::ApplyRegistrationState(incoming, AZ::EntityId(8100), registrations, affected, roles,
                    [](const auto*, const auto&) { return AZ::u8{ 0 }; });
                targetId = 8200;
                for (const auto& targetRole : roles)
                {
                    const AZ::EntityId id(targetId++);
                    const auto& target = registrations.at(id).*targetRole.m_snapshot;
                    EXPECT_EQ(target.m_revision, 20);
                    EXPECT_EQ(target.m_status, Status::Missing);
                    EXPECT_EQ(affected.at(id), targetRole.m_dirty | (id == AZ::EntityId(8200) ? Internal::DirtySurface : 0));
                }
                EXPECT_EQ(affected.size(), AZ_ARRAY_SIZE(roles));
                EXPECT_EQ(equal.m_status, Status::Loading);
                EXPECT_EQ(unrelated.m_revision, 1);
            }
        };
        check(Internal::ImageAssetRoles, HeightmapStampRegistrationData{});
        check(Internal::CutoutAssetRoles, TerrainMeshCutoutRegistrationData{});
        check(Internal::MeshHeightAssetRoles, TerrainMeshHeightStampRegistrationData{});
    }
}
