#include <AzTest/AzTest.h>
#include <TerrainCompositor/Components/HeightmapStampConfig.h>
#include "ComponentConfiguration.h"
#include "RegistrationStateReference.h"
#include "PublicationState.h"
#include "CompositionInvalidation.h"
#include <TerrainCompositor/Internal/PreparedComposition.h>
#include <TerrainCompositor/SurfaceStampSampling.h>
#include "StampMath.h"
#include "ImageSampling.h"
#include <cstring>
#include <TerrainCompositor/TerrainDetailMaterial.h>

namespace TerrainCompositor
{
    TEST(ImageSamplingTests, BilinearMatchesIndependentRowsAtEdgesAndDegenerateDimensions)
    {
        const float values[] = { 0.13f, 0.79f, 0.23f, 0.97f, 0.37f, 0.61f };
        for (AZ::u32 width : { 1u, 2u, 3u })
            for (AZ::u32 height : { 1u, 2u })
                for (double u : { -0.5, 0.0, 0.125, 0.5, 0.999999999, 1.0, 1.5 })
                    for (double v : { -0.5, 0.0, 0.125, 0.5, 0.999999999, 1.0, 1.5 })
                    {
                        // Frozen pre-extraction arithmetic, independent of the production coordinates.
                        const double x = std::clamp(u, 0.0, 1.0) * (width - 1);
                        const double y = std::clamp(1.0 - v, 0.0, 1.0) * (height - 1);
                        const size_t x0 = size_t(x), y0 = size_t(y);
                        const size_t x1 = std::min(x0 + 1, size_t(width - 1));
                        const size_t y1 = std::min(y0 + 1, size_t(height - 1));
                        const double tx = x - double(x0), ty = y - double(y0);
                        const double top = values[y0 * width + x0] * (1.0 - tx) + values[y0 * width + x1] * tx;
                        const double bottom = values[y1 * width + x0] * (1.0 - tx) + values[y1 * width + x1] * tx;
                        const double expected = top * (1.0 - ty) + bottom * ty;
                        const double actual = Internal::SampleBilinear(values, width, height, u, v);
                        EXPECT_EQ(std::memcmp(&expected, &actual, sizeof(double)), 0);
                    }
    }

    TEST(StampMathTests, TransformedBoundsContainCornersAndRejectOverflow)
    {
        const auto local = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-7, -3, 0), AZ::Vector3(2, 5, 0));
        for (double yaw : { -3.0, -0.1, 0.0, 0.7, 3.0 })
            for (double scale : { 0.001, 1.0, 1000000.0 })
            {
                const auto bounds = Internal::TransformStampXYBounds(local, 0.123, -456.789, scale, std::cos(yaw), std::sin(yaw));
                ASSERT_TRUE(bounds.IsRepresentable());
                const auto rounded = bounds.ToAabb();
                for (double x : { -7.0, 2.0 })
                    for (double y : { -3.0, 5.0 })
                    {
                        const double worldX = 0.123 + scale * (std::cos(yaw) * x - std::sin(yaw) * y);
                        const double worldY = -456.789 + scale * (std::sin(yaw) * x + std::cos(yaw) * y);
                        EXPECT_LE(rounded.GetMin().GetX(), worldX);
                        EXPECT_GE(rounded.GetMax().GetX(), worldX);
                        EXPECT_LE(rounded.GetMin().GetY(), worldY);
                        EXPECT_GE(rounded.GetMax().GetY(), worldY);
                    }
            }
        EXPECT_FALSE(Internal::TransformStampXYBounds(local, 0, 0, double(std::numeric_limits<float>::max()), 1, 0).IsRepresentable());
    }

    TEST(TerrainDetailMaterialTests, UniformPixelsRespectRegionOrderMappingsAndEveryPixelByte)
    {
        const auto box = [](float x0, float y0, float x1, float y1)
        { return AZ::Aabb::CreateFromMinMax(AZ::Vector3(x0, y0, -10), AZ::Vector3(x1, y1, 10)); };
        const auto bounds = box(-2, -2, 2, 2);
        AZStd::vector<TerrainDetailMaterialRegion> regions;
        auto pixel = FindUniformTerrainDetailMaterial(bounds, regions, 7);
        ASSERT_TRUE(pixel);
        EXPECT_EQ(pixel->m_material1, 7);
        EXPECT_EQ(pixel->m_material2, 255);
        EXPECT_EQ(pixel->m_blend, 0);
        EXPECT_EQ(pixel->m_padding, 0);
        regions.push_back({ box(-4, -4, 4, 4), 3, false });
        EXPECT_EQ(FindUniformTerrainDetailMaterial(bounds, regions, 7)->m_material1, 3);
        regions.push_back({ bounds, 9, true }); // A later region cannot overrule the first owner.
        EXPECT_EQ(FindUniformTerrainDetailMaterial(bounds, regions, 7)->m_material1, 3);
        regions[0].m_hasSurfaceMappings = true;
        EXPECT_FALSE(FindUniformTerrainDetailMaterial(bounds, regions, 7));
        regions[0] = { box(2, -4, 4, 4), 3, false }; // Inclusive touching edge has a different owner.
        EXPECT_FALSE(FindUniformTerrainDetailMaterial(bounds, regions, 7));
        regions[0].m_bounds = box(3, -4, 4, 4);
        regions[1].m_hasSurfaceMappings = false;
        EXPECT_EQ(FindUniformTerrainDetailMaterial(bounds, regions, 7)->m_material1, 9);
        regions.clear(); // Removal and movement must reassess immediately.
        EXPECT_EQ(FindUniformTerrainDetailMaterial(bounds, regions, 7)->m_material1, 7);
        EXPECT_FALSE(FindUniformTerrainDetailMaterial(AZ::Aabb::CreateNull(), regions, 7));
    }

    TEST(TerrainDetailMaterialTests, UniformCertificateMatchesIndependentPointwiseRegionOracle)
    {
        const auto box = [](float x0, float y0, float x1, float y1)
        { return AZ::Aabb::CreateFromMinMax(AZ::Vector3(x0, y0, 0), AZ::Vector3(x1, y1, 0)); };
        for (int offset = -4; offset <= 4; ++offset)
            for (int width = 1; width <= 4; ++width)
                for (bool mapped : { false, true })
                {
                    const AZStd::vector<TerrainDetailMaterialRegion> regions{
                        { box(float(offset), -2, float(offset + width), 2), 3, mapped },
                        { box(-8, -8, 8, 8), 5, false } };
                    for (int x0 = -4; x0 <= 3; ++x0)
                    {
                        const auto certificate = FindUniformTerrainDetailMaterial(box(float(x0), -1, float(x0 + 1), 1), regions, 7);
                        if (!certificate) continue;
                        for (int sample = 0; sample <= 8; ++sample)
                        {
                            const float x = float(x0) + float(sample) / 8;
                            const bool first = x >= offset && x <= offset + width;
                            EXPECT_FALSE(first && mapped);
                            EXPECT_EQ(certificate->m_material1, first ? 3 : 5);
                        }
                    }
                }
    }

    TEST(TerrainDetailMaterialTests, QueryBoundsProveJobSubdivisionCoordinatesAcrossSignedBoundaries)
    {
        for (float spacing : { 0.5f, 1.0f, 2.0f })
            for (int origin : { -4096, -16, -1, 0, 1, 16, 4096 })
            {
                const float start = origin * spacing;
                const auto bounds = ExactTerrainDetailQueryBounds(AZ::Vector3(start, start, 0), 131, 67, spacing);
                ASSERT_TRUE(bounds);
                for (int split = 0; split < 131; ++split)
                    for (int x = split; x < 131; ++x)
                    {
                        const float subdivided = (start + split * spacing) + (x - split) * spacing;
                        EXPECT_EQ(subdivided, start + x * spacing);
                        EXPECT_GE(subdivided, bounds->GetMin().GetX());
                        EXPECT_LE(subdivided, bounds->GetMax().GetX());
                    }
            }
        EXPECT_FALSE(ExactTerrainDetailQueryBounds(AZ::Vector3(0), 4, 4, 0.3f));
        EXPECT_FALSE(ExactTerrainDetailQueryBounds(AZ::Vector3(0.1f), 4, 4, 0.5f));
        EXPECT_FALSE(ExactTerrainDetailQueryBounds(AZ::Vector3(4194304.0f), 4, 4, 1));
        EXPECT_FALSE(ExactTerrainDetailQueryBounds(AZ::Vector3(0), 0, 4, 1));
    }

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

    class CompositionInvalidationTests : public ::testing::Test
    {
    protected:
        struct State : Internal::PreparedComposition
        {
            State() { m_regionBounds = AZ::Aabb::CreateFromMinMaxValues(-100, -100, -20, 100, 100, 80); }
            TerrainCompositionAddress m_address{ AZ::Uuid::CreateRandom(), AZ::EntityId(9001) };
            AZ::Uuid m_session = AZ::Uuid::CreateRandom();
            AZ::u64 m_revision = 10;
            AZ::EntityId m_sourceEntityId{ 9002 }, m_regionEntityId{ 9003 };
        };

        static AZ::Aabb Bounds(float x1, float y1, float x2, float y2)
        {
            return AZ::Aabb::CreateFromMinMaxValues(x1, y1, 0, x2, y2, 0);
        }
        static AZ::Aabb Expanded(const AZ::Aabb& bounds)
        {
            return AZ::Aabb::CreateFromMinMaxValues(bounds.GetMin().GetX() - 2, bounds.GetMin().GetY() - 2, -20,
                bounds.GetMax().GetX() + 2, bounds.GetMax().GetY() + 2, 80);
        }
        static void Height(State& state, AZ::EntityId id, const AZ::Aabb& bounds)
        {
            PreparedHeightContributor height;
            height.m_image.m_placement.m_stampEntityId = id;
            height.m_image.m_placement.m_worldBounds = bounds;
            state.m_heightContributors.push_back(height);
        }
        static void Cutout(State& state, AZ::EntityId id, const AZ::Aabb& bounds)
        {
            PreparedTerrainExistenceContributor cutout;
            cutout.m_type = PreparedTerrainExistenceContributor::Type::MeshCutout;
            cutout.m_meshCutout.m_entityId = id;
            cutout.m_meshCutout.m_collisionWorldBounds = bounds;
            state.m_existenceContributors.push_back(cutout);
        }
        const AZ::EntityId m_stamp{ 9010 }, m_cutout{ 9011 };
    };

    TEST_F(CompositionInvalidationTests, UsesPublishedOldAndUnsortedCandidateNewFirstClaims)
    {
        State previous;
        State current = previous;
        ++current.m_revision;
        const auto oldFirst = Bounds(-40, -10, -30, 0);
        const auto newFirst = Bounds(10, 0, 20, 10);
        const auto alternate = Bounds(30, 0, 50, 20);
        Height(previous, m_stamp, oldFirst);
        Height(previous, m_stamp, alternate);
        Height(current, m_stamp, newFirst);
        Height(current, m_stamp, alternate);
        const Internal::PublicationFootprints candidate(current);
        AZStd::reverse(current.m_heightContributors.begin(), current.m_heightContributors.end());
        const auto result = Internal::PlanCompositionInvalidation(previous, current, candidate,
            { { m_stamp, Internal::DirtyHeight } }, {});
        ASSERT_EQ(result.m_changes.size(), 1);
        const auto& change = result.m_changes[0];
        EXPECT_EQ(change.m_previousBounds, oldFirst);
        EXPECT_EQ(change.m_currentBounds, newFirst);
        EXPECT_EQ(change.m_address, current.m_address);
        EXPECT_EQ(change.m_compositionSession, current.m_session);
        EXPECT_EQ(change.m_snapshotRevision, current.m_revision);
        EXPECT_EQ(change.m_previousRegionEntityId, previous.m_regionEntityId);
        EXPECT_EQ(change.m_currentRegionBounds, current.m_regionBounds);
        EXPECT_EQ(current.m_heightContributors[0].GetWorldBounds(), alternate);
        EXPECT_EQ(candidate.m_height.at(m_stamp), newFirst);
        const AZStd::vector<AZ::Aabb> expected{ Expanded(oldFirst), Expanded(newFirst) };
        EXPECT_EQ(result.m_heightTerrain.BuildRegions(1.0f), expected);
        EXPECT_TRUE(result.m_surfaceTerrain.IsEmpty());
    }

    TEST_F(CompositionInvalidationTests, MembershipRemovalPreservesSurfaceMaskCutoutAndGapRouting)
    {
        const auto logical = Bounds(10, 20, 12, 24);
        const auto collision = Bounds(8, 18, 16, 28);
        for (int role = 0; role < 6; ++role)
        {
            SCOPED_TRACE(role); // surface, mask, cutout, query gap, render gap, coupled gap
            State previous;
            const State current = previous;
            if (role == 0)
            {
                PreparedSurfaceStamp surface;
                surface.m_placement.m_stampEntityId = m_stamp;
                surface.m_placement.m_worldBounds = logical;
                previous.m_surfaceStamps.push_back(surface);
            }
            else if (role == 1)
            {
                PreparedTerrainExistenceContributor mask;
                mask.m_imageMask.m_placement.m_stampEntityId = m_stamp;
                mask.m_imageMask.m_placement.m_worldBounds = logical;
                previous.m_existenceContributors.push_back(mask);
            }
            else if (role == 2)
                Cutout(previous, m_stamp, collision);
            else
            {
                PreparedTerrainMeshHeightGap gap;
                gap.m_entityId = m_stamp;
                gap.m_worldBounds = logical;
                gap.m_collisionWorldBounds = collision;
                gap.m_affectTerrainRendering = role != 3;
                gap.m_affectTerrainCollisionQueries = role != 4;
                previous.m_meshHeightGaps.push_back(gap);
                if (gap.m_affectTerrainCollisionQueries)
                {
                    PreparedTerrainExistenceContributor query;
                    query.m_type = PreparedTerrainExistenceContributor::Type::MeshHeightGap;
                    query.m_meshHeightGap = gap;
                    previous.m_existenceContributors.push_back(query);
                }
            }
            const auto result = Internal::PlanCompositionInvalidation(previous, current,
                Internal::PublicationFootprints(current), {}, {});
            const AZStd::vector<AZ::Aabb> height = role == 0 || role == 4 ? AZStd::vector<AZ::Aabb>{}
                : AZStd::vector<AZ::Aabb>{ Expanded(role == 1 ? logical : collision) };
            const AZStd::vector<AZ::Aabb> surface = role == 3 ? AZStd::vector<AZ::Aabb>{}
                : AZStd::vector<AZ::Aabb>{ Expanded(role == 2 ? collision : logical) };
            EXPECT_EQ(result.m_heightTerrain.BuildRegions(1.0f), height);
            EXPECT_EQ(result.m_surfaceTerrain.BuildRegions(1.0f), surface);
            EXPECT_TRUE(result.m_changes.empty());
            const auto repeated = Internal::PlanCompositionInvalidation(current, current,
                Internal::PublicationFootprints(current), { { m_stamp, Internal::DirtyAll } }, {});
            EXPECT_TRUE(repeated.m_heightTerrain.IsEmpty());
            EXPECT_TRUE(repeated.m_surfaceTerrain.IsEmpty());
            EXPECT_TRUE(repeated.m_changes.empty());
        }
    }

    TEST_F(CompositionInvalidationTests, HeightChangesInvalidateOnlyTheirCutoutIntersectionOnEachSide)
    {
        State previous;
        State current = previous;
        Height(previous, m_stamp, Bounds(-20, -5, -10, 5));
        Height(current, m_stamp, Bounds(10, -5, 20, 5));
        Cutout(previous, m_cutout, Bounds(-15, -20, 20, 20));
        Cutout(current, m_cutout, Bounds(-15, -20, 20, 20));
        const auto result = Internal::PlanCompositionInvalidation(previous, current,
            Internal::PublicationFootprints(current), { { m_stamp, Internal::DirtyHeight } }, {});
        const AZStd::vector<AZ::Aabb> expected{ Expanded(Bounds(-15, -5, -10, 5)), Expanded(Bounds(10, -5, 20, 5)) };
        EXPECT_EQ(result.m_surfaceTerrain.BuildRegions(1.0f), expected);
        EXPECT_EQ(result.m_heightTerrain.BuildRegions(1.0f).size(), 2);
        ASSERT_EQ(result.m_changes.size(), 1);
        EXPECT_EQ(result.m_changes[0].m_stampEntityId, m_stamp);
    }

    TEST_F(CompositionInvalidationTests, FoldsIntoExistingQueuesWithoutRegroupingOrMutatingInputs)
    {
        State previous;
        State current = previous;
        Height(previous, m_stamp, Bounds(1, 0, 2, 2));
        Height(current, m_stamp, Bounds(2, 0, 5, 3));
        Internal::PendingCompositionInvalidation pending;
        pending.m_heightTerrain.AddFootprint(previous.m_regionEntityId, previous.m_regionBounds, Bounds(0, 0, 1, 2));
        HeightmapStampFootprintChange earlier;
        earlier.m_snapshotRevision = 3;
        pending.m_changes.push_back(earlier);
        const auto retainedRegions = pending.m_heightTerrain.BuildRegions(0.01f);
        const AZStd::unordered_map<AZ::EntityId, AZ::u8> dirty{ { m_stamp, Internal::DirtyHeight } };
        const auto result = Internal::PlanCompositionInvalidation(previous, current,
            Internal::PublicationFootprints(current), dirty, pending);
        // Pending A merges with old B; their union cannot merge with new C under the 1.1 area bound.
        // Coalescing B+C first would instead allow all three to merge into one larger rectangle.
        const auto regions = result.m_heightTerrain.BuildRegions(0.01f);
        ASSERT_EQ(regions.size(), 2);
        EXPECT_FLOAT_EQ(regions[0].GetMin().GetX(), -0.02f);
        EXPECT_FLOAT_EQ(regions[0].GetMax().GetX(), 2.02f);
        EXPECT_FLOAT_EQ(regions[1].GetMin().GetX(), 1.98f);
        EXPECT_FLOAT_EQ(regions[1].GetMax().GetY(), 3.02f);
        ASSERT_EQ(result.m_changes.size(), 2);
        EXPECT_EQ(result.m_changes[0].m_snapshotRevision, 3);
        EXPECT_EQ(result.m_changes[1].m_snapshotRevision, current.m_revision);
        EXPECT_EQ(pending.m_changes.size(), 1);
        EXPECT_EQ(pending.m_heightTerrain.BuildRegions(0.01f), retainedRegions);
        EXPECT_EQ(dirty.size(), 1);
        EXPECT_EQ(dirty.at(m_stamp), Internal::DirtyHeight);
    }

    TEST_F(CompositionInvalidationTests, StableMembershipHonorsOnlyTheRequestedDirtyChannel)
    {
        State previous;
        Height(previous, m_stamp, Bounds(-30, 0, -20, 10));
        PreparedSurfaceStamp surface;
        surface.m_placement.m_stampEntityId = m_stamp;
        surface.m_placement.m_worldBounds = Bounds(10, 0, 20, 10);
        previous.m_surfaceStamps.push_back(surface);
        State current = previous;
        current.m_heightContributors[0].m_image.m_placement.m_worldBounds = Bounds(30, 0, 40, 10);
        const auto result = Internal::PlanCompositionInvalidation(previous, current,
            Internal::PublicationFootprints(current), { { m_stamp, Internal::DirtySurface } }, {});
        EXPECT_TRUE(result.m_changes.empty());
        EXPECT_TRUE(result.m_heightTerrain.IsEmpty());
        EXPECT_EQ(result.m_surfaceTerrain.BuildRegions(1.0f), AZStd::vector<AZ::Aabb>{ Expanded(surface.m_placement.m_worldBounds) });
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
