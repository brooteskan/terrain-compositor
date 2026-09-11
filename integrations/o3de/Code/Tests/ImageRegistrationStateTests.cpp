#include <AzTest/AzTest.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include "RegistrationStateReference.h"

namespace TerrainCompositor::Internal
{
    class ImageRegistrationStateTests : public ::testing::Test
    {
    protected:
        using Record = HeightmapStampRegistrationData;
        static HeightmapDataSnapshot Snapshot(AZ::Data::AssetId asset, AZ::u64 revision,
            HeightmapDataStatus status = HeightmapDataStatus::Ready)
        {
            auto data = AZStd::make_shared<HeightmapData>();
            data->m_assetId = asset;
            data->m_revision = revision;
            return { status, revision, status == HeightmapDataStatus::Ready ? data : HeightmapDataPtr{}, asset };
        }
        Record Make(AZ::u64 id, size_t role, const HeightmapDataSnapshot& snapshot)
        {
            Record record;
            record.m_stampEntityId = AZ::EntityId(id);
            record.*ImageAssetRoles[role].m_snapshot = snapshot;
            return record;
        }
        void Apply(const Record& record, AZ::u8 directDirty = 0)
        {
            m_state.Apply(record, m_dirty, [directDirty](const auto*, const auto&) { return directDirty; });
        }
        const Record& Get(AZ::u64 id) const { return m_state.GetRegistrations().at(AZ::EntityId(id)); }
        static void ExpectSnapshot(const HeightmapDataSnapshot& actual, const HeightmapDataSnapshot& expected)
        {
            EXPECT_EQ(actual.m_assetId, expected.m_assetId);
            EXPECT_EQ(actual.m_revision, expected.m_revision);
            EXPECT_EQ(actual.m_status, expected.m_status);
            EXPECT_EQ(actual.m_data, expected.m_data);
        }
        void ExpectIndexConsistent()
        {
            size_t expectedClaims = 0, indexedClaims = 0;
            for (const auto& [id, record] : m_state.m_records)
            {
                for (size_t role = 0; role < AZ_ARRAY_SIZE(ImageAssetRoles); ++role)
                {
                    const auto& snapshot = record.*ImageAssetRoles[role].m_snapshot;
                    if (!snapshot.m_assetId.IsValid()) { continue; }
                    ++expectedClaims;
                    const auto found = m_state.m_assets.find(snapshot.m_assetId);
                    ASSERT_NE(found, m_state.m_assets.end());
                    EXPECT_EQ(AZStd::count_if(found->second.m_claims.begin(), found->second.m_claims.end(), [&](const auto& claim)
                        { return claim.m_entityId == id && claim.m_role == role; }), 1);
                }
            }
            for (const auto& [id, asset] : m_state.m_assets)
            {
                ASSERT_TRUE(id.IsValid());
                ASSERT_FALSE(asset.m_claims.empty());
                indexedClaims += asset.m_claims.size();
                bool conflicting = false, representativePresent = false;
                for (const auto& claim : asset.m_claims)
                {
                    const auto record = m_state.m_records.find(claim.m_entityId);
                    ASSERT_NE(record, m_state.m_records.end());
                    ASSERT_LT(claim.m_role, AZ_ARRAY_SIZE(ImageAssetRoles));
                    const auto& snapshot = record->second.*ImageAssetRoles[claim.m_role].m_snapshot;
                    EXPECT_EQ(snapshot.m_assetId, id);
                    EXPECT_EQ(snapshot.m_revision, asset.m_latest.m_revision);
                    const bool same = snapshot.m_status == asset.m_latest.m_status && snapshot.m_data == asset.m_latest.m_data;
                    representativePresent |= same;
                    conflicting |= !same;
                }
                EXPECT_TRUE(representativePresent);
                EXPECT_EQ(asset.m_latest.m_assetId, id);
                EXPECT_EQ(asset.m_conflictingTie, conflicting);
            }
            EXPECT_EQ(indexedClaims, expectedClaims);
        }
        ImageRegistrationState m_state;
        AZStd::unordered_map<AZ::EntityId, AZ::u8> m_dirty;
        const AZ::Data::AssetId m_asset{ AZ::Uuid::CreateRandom(), 1 }, m_otherAsset{ AZ::Uuid::CreateRandom(), 1 };
    };

    TEST_F(ImageRegistrationStateTests, EveryImageRoleSharesRevisionAuthorityAndAccumulatesOnlyDependentDirtyBits)
    {
        for (size_t source = 0; source < AZ_ARRAY_SIZE(ImageAssetRoles); ++source)
        {
            m_state.Clear();
            const auto old = Snapshot(m_asset, 1, HeightmapDataStatus::Loading);
            for (size_t target = 0; target < AZ_ARRAY_SIZE(ImageAssetRoles); ++target) { Apply(Make(100 + target, target, old)); }
            const auto unrelated = Make(200, 0, Snapshot(m_otherAsset, 1));
            Apply(unrelated);
            m_dirty.clear();
            m_dirty[AZ::EntityId(100)] = DirtySurface;
            const auto update = Snapshot(m_asset, 2, HeightmapDataStatus::Missing);
            Apply(Make(300, source, update));
            for (size_t target = 0; target < AZ_ARRAY_SIZE(ImageAssetRoles); ++target)
            {
                ExpectSnapshot(Get(100 + target).*ImageAssetRoles[target].m_snapshot, update);
                EXPECT_EQ(m_dirty.at(AZ::EntityId(100 + target)), ImageAssetRoles[target].m_dirty | (target == 0 ? DirtySurface : 0));
            }
            EXPECT_EQ(m_dirty.size(), 5);
            ExpectSnapshot(Get(200).m_heightmap, unrelated.m_heightmap);
        }
    }

    TEST_F(ImageRegistrationStateTests, ReconcilesPreviousClaimBeforeClassificationWithoutMutatingCaller)
    {
        const auto newest = Snapshot(m_asset, 9, HeightmapDataStatus::Error);
        Apply(Make(1, 0, newest));
        const auto stale = Make(1, 0, Snapshot(m_asset, 1));
        m_dirty[AZ::EntityId(1)] = DirtyExistence;
        int classifications = 0;
        m_state.Apply(stale, m_dirty, [&](const auto* previous, const auto& current)
        {
            ++classifications;
            EXPECT_NE(previous, nullptr);
            ExpectSnapshot(current.m_heightmap, newest);
            return DirtySurface;
        });
        EXPECT_EQ(classifications, 1);
        EXPECT_EQ(stale.m_heightmap.m_revision, 1);
        EXPECT_EQ(m_dirty.at(AZ::EntityId(1)), DirtySurface | DirtyExistence);
    }

    TEST_F(ImageRegistrationStateTests, EqualRevisionPayloadsSurviveAndOlderClaimsFollowExistingMapOrder)
    {
        const auto first = Make(1, 0, Snapshot(m_asset, 9));
        const auto second = Make(2, 0, Snapshot(m_asset, 9));
        Apply(first);
        Apply(second);
        ExpectSnapshot(Get(1).m_heightmap, first.m_heightmap);
        ExpectSnapshot(Get(2).m_heightmap, second.m_heightmap);
        const auto expected = m_state.GetRegistrations().begin()->second.m_heightmap;
        Apply(Make(3, 0, Snapshot(m_asset, 1)));
        ExpectSnapshot(Get(3).m_heightmap, expected);
        EXPECT_TRUE(m_dirty.empty());
    }

    TEST_F(ImageRegistrationStateTests, RemovingTheRepresentativeRetainsTheSurvivingEqualRevisionPayload)
    {
        Apply(Make(1, 0, Snapshot(m_asset, 9)));
        Apply(Make(2, 0, Snapshot(m_asset, 9, HeightmapDataStatus::Error)));
        const auto removed = m_state.GetRegistrations().begin()->first;
        ASSERT_TRUE(m_state.Remove(removed));
        const auto surviving = m_state.GetRegistrations().begin()->second.m_heightmap;
        Apply(Make(3, 4, Snapshot(m_asset, 1)));
        ExpectSnapshot(Get(3).m_holeMask, surviving);
    }

    TEST_F(ImageRegistrationStateTests, ConflictingRolesWithinOneInputPreserveClassificationAndSourceOrder)
    {
        auto input = Make(1, 0, Snapshot(m_asset, 1));
        input.m_surfaceIdA = Snapshot(m_asset, 3);
        input.m_surfaceIdB = Snapshot(m_asset, 2);
        input.m_surfaceBlend = Snapshot(m_asset, 3);
        input.m_holeMask = Snapshot(m_asset, 0);
        m_state.Apply(input, m_dirty, [&](const auto* previous, const auto& current)
        {
            EXPECT_EQ(previous, nullptr);
            ExpectSnapshot(current.m_heightmap, input.m_heightmap);
            ExpectSnapshot(current.m_surfaceIdB, input.m_surfaceIdB);
            return AZ::u8{ 0 };
        });
        for (size_t role : { 0, 1, 2, 4 }) { ExpectSnapshot(Get(1).*ImageAssetRoles[role].m_snapshot, input.m_surfaceIdA); }
        ExpectSnapshot(Get(1).m_surfaceBlend, input.m_surfaceBlend);
        EXPECT_EQ(m_dirty.at(AZ::EntityId(1)), DirtyAll);
        Apply(Make(2, 4, Snapshot(m_asset, 0)));
        ExpectSnapshot(Get(2).m_holeMask, input.m_surfaceIdA);
    }

    TEST_F(ImageRegistrationStateTests, AdvancingSourceRolesCollectFinalTiesWithoutASeparateRebuild)
    {
        const struct { AZ::u64 m_revisions[5]; size_t m_passes; bool m_tie; } cases[] = {
            { { 3, 3, 5, 4, 2 }, 2, false }, { { 5, 3, 3, 4, 2 }, 1, false },
            { { 3, 3, 4, 2, 5 }, 3, false }, { { 3, 5, 4, 5, 2 }, 2, true },
            { { 5, 3, 5, 4, 2 }, 1, true }, { { 3, 3, 4, 5, 5 }, 3, true }
        };
        for (const auto& test : cases)
        {
            SCOPED_TRACE(test.m_revisions[0]);
            m_state.Clear();
            m_dirty.clear();
            AZStd::unordered_map<AZ::EntityId, Record> reference;
            AZStd::unordered_map<AZ::EntityId, AZ::u8> expectedDirty;
            const auto classify = [](const auto*, const auto&) { return DirtySurface; };
            const auto apply = [&](const Record& record, RegistrationTraversal* traversal = nullptr)
            {
                ApplyRegistrationState(record, record.m_stampEntityId, reference, expectedDirty, ImageAssetRoles, classify);
                m_state.Apply(record, m_dirty, classify, traversal);
                ASSERT_EQ(m_dirty.size(), expectedDirty.size());
                for (const auto& [id, mask] : expectedDirty) { EXPECT_EQ(m_dirty.at(id), mask); }
                for (const auto& [id, expected] : reference)
                    for (const auto& role : ImageAssetRoles)
                        ExpectSnapshot(m_state.GetRegistrations().at(id).*role.m_snapshot, expected.*role.m_snapshot);
                ExpectIndexConsistent();
            };
            // Start with a conflicting cache; a unique higher maximum must clear that tie.
            apply(Make(10, 0, Snapshot(m_asset, 2)));
            apply(Make(11, 4, Snapshot(m_asset, 2, HeightmapDataStatus::Error)));
            auto input = Make(1, 0, {});
            for (size_t role = 0; role < AZ_ARRAY_SIZE(ImageAssetRoles); ++role)
                input.*ImageAssetRoles[role].m_snapshot = Snapshot(m_asset, test.m_revisions[role]);
            RegistrationTraversal traversal;
            apply(input, &traversal);
            EXPECT_EQ(traversal.m_claimsVisited, test.m_passes * 7);
            EXPECT_EQ(traversal.m_fallbackRegistrationsVisited, 0);
            for (size_t role = 0; role < AZ_ARRAY_SIZE(ImageAssetRoles); ++role)
                EXPECT_EQ((input.*ImageAssetRoles[role].m_snapshot).m_revision, test.m_revisions[role]);
            traversal = {};
            apply(Make(2, 4, Snapshot(m_asset, 0)), &traversal);
            EXPECT_EQ(traversal.m_fallbackRegistrationsVisited > 0, test.m_tie);
        }
    }

    TEST_F(ImageRegistrationStateTests, RetargetingTheLastRoleRetiresHistoryButRetargetingOneOfSeveralDoesNot)
    {
        auto record = Make(1, 0, Snapshot(m_asset, 12));
        record.m_holeMask = record.m_heightmap;
        Apply(record);
        record.m_heightmap = Snapshot(m_otherAsset, 1);
        Apply(record);
        Apply(Make(2, 2, Snapshot(m_asset, 1)));
        ExpectSnapshot(Get(2).m_surfaceIdB, record.m_holeMask);
        ASSERT_TRUE(m_state.Remove(AZ::EntityId(2)));
        record.m_holeMask = {};
        Apply(record);
        const auto fresh = Make(3, 0, Snapshot(m_asset, 1));
        Apply(fresh);
        ExpectSnapshot(Get(3).m_heightmap, fresh.m_heightmap);
    }

    TEST_F(ImageRegistrationStateTests, RemovingLastClaimAndReusingItsEntityStartsFreshHistory)
    {
        Apply(Make(1, 0, Snapshot(m_asset, 20)));
        ASSERT_TRUE(m_state.Remove(AZ::EntityId(1)));
        EXPECT_FALSE(m_state.Remove(AZ::EntityId(1)));
        const auto fresh = Make(1, 0, Snapshot(m_asset, 1));
        Apply(fresh);
        ExpectSnapshot(Get(1).m_heightmap, fresh.m_heightmap);
    }

    TEST_F(ImageRegistrationStateTests, ClearingSessionRemovesEveryRoleAndItsRevisionHistory)
    {
        auto old = Make(1, 0, Snapshot(m_asset, 20));
        for (const auto& role : ImageAssetRoles) { old.*role.m_snapshot = old.m_heightmap; }
        Apply(old);
        m_state.Clear();
        EXPECT_TRUE(m_state.GetRegistrations().empty());
        const auto fresh = Make(1, 4, Snapshot(m_asset, 1));
        Apply(fresh);
        ExpectSnapshot(Get(1).m_holeMask, fresh.m_holeMask);
    }

    TEST_F(ImageRegistrationStateTests, UnassignedIdsStayLocalAndCannotContaminateAssignedHistory)
    {
        const auto first = Make(1, 0, Snapshot({}, 100));
        const auto second = Make(2, 4, Snapshot({}, 1));
        Apply(first);
        Apply(second);
        ExpectSnapshot(Get(1).m_heightmap, first.m_heightmap);
        ExpectSnapshot(Get(2).m_holeMask, second.m_holeMask);
        auto assigned = first;
        assigned.m_heightmap = Snapshot(m_asset, 1);
        Apply(assigned);
        ExpectSnapshot(Get(1).m_heightmap, assigned.m_heightmap);
        EXPECT_TRUE(m_dirty.empty());
    }

    TEST_F(ImageRegistrationStateTests, FailureAndRecoveryPropagateWithoutMutatingRetainedImageData)
    {
        const auto ready = Snapshot(m_asset, 1);
        Apply(Make(1, 0, ready));
        Apply(Make(2, 4, ready));
        const auto retained = Get(2).m_holeMask.m_data;
        const auto failure = Snapshot(m_asset, 2, HeightmapDataStatus::Missing);
        Apply(Make(1, 0, failure));
        ExpectSnapshot(Get(2).m_holeMask, failure);
        const auto recovered = Snapshot(m_asset, 3);
        Apply(Make(2, 4, recovered));
        ExpectSnapshot(Get(1).m_heightmap, recovered);
        EXPECT_EQ(retained->m_revision, 1);
        EXPECT_NE(retained, recovered.m_data);
    }

    TEST_F(ImageRegistrationStateTests, CanonicalSnapshotIdsDetermineSharingIndependentlyOfConfiguredProductIds)
    {
        auto record = Make(1, 0, Snapshot(m_asset, 7));
        record.m_configuration.m_heightmapAsset = { m_otherAsset, AZ::Uuid::CreateRandom(), {} };
        Apply(record);
        Apply(Make(2, 4, Snapshot(m_asset, 1)));
        ExpectSnapshot(Get(2).m_holeMask, record.m_heightmap);
        const auto other = Make(3, 0, Snapshot(m_otherAsset, 1));
        Apply(other);
        ExpectSnapshot(Get(3).m_heightmap, other.m_heightmap);
    }

    TEST_F(ImageRegistrationStateTests, UnrelatedRegistrationCountDoesNotIncreaseIndexedTraversal)
    {
        for (size_t unrelated : { 32, 2048 })
        {
            m_state.Clear();
            for (size_t i = 0; i < unrelated; ++i)
            {
                Apply(Make(100 + i, 0, Snapshot({ AZ::Uuid::CreateRandom(), 1 }, 1)));
            }
            const auto old = Snapshot(m_asset, 1, HeightmapDataStatus::Loading);
            Apply(Make(1, 0, old));
            Apply(Make(2, 4, old));
            const auto newest = Snapshot(m_asset, 2);
            auto update = Make(3, 0, newest);
            for (const auto& role : ImageAssetRoles) { update.*role.m_snapshot = newest; }
            RegistrationTraversal traversal;
            m_state.Apply(update, m_dirty, [](const auto*, const auto&) { return AZ::u8{ 0 }; }, &traversal);
            EXPECT_EQ(traversal.m_claimsVisited, 7);
            EXPECT_EQ(traversal.m_fallbackRegistrationsVisited, 0);
            ExpectSnapshot(Get(1).m_heightmap, newest);
            ExpectSnapshot(Get(2).m_holeMask, newest);
            ExpectIndexConsistent();
            AZ_Printf("ImageRegistrationState", "%zu unrelated records: %zu claim visits, %zu fallback record visits.\n",
                unrelated, traversal.m_claimsVisited, traversal.m_fallbackRegistrationsVisited);
            RecordProperty(AZStd::string::format("assigned_revision_%zu", unrelated).c_str(),
                AZStd::string::format("claims=%zu fallback=%zu", traversal.m_claimsVisited,
                    traversal.m_fallbackRegistrationsVisited).c_str());
            for (bool stale : { false, true })
            {
                auto placement = update;
                placement.m_configuration.m_priority = stale ? 4 : 3;
                if (stale) { for (const auto& role : ImageAssetRoles) { placement.*role.m_snapshot = old; } }
                m_dirty.clear();
                m_dirty[placement.m_stampEntityId] = DirtyHeight;
                traversal = {};
                int classified = 0;
                m_state.Apply(placement, m_dirty, [&](const auto* previous, const auto& current)
                {
                    ++classified;
                    EXPECT_NE(previous, nullptr);
                    EXPECT_EQ(current.m_configuration.m_priority, placement.m_configuration.m_priority);
                    for (const auto& role : ImageAssetRoles) { ExpectSnapshot(current.*role.m_snapshot, newest); }
                    return DirtySurface;
                }, &traversal);
                EXPECT_EQ(classified, 1);
                EXPECT_EQ(traversal.m_claimsVisited, 0);
                EXPECT_EQ(traversal.m_fallbackRegistrationsVisited, 0);
                EXPECT_EQ(m_dirty.size(), 1);
                EXPECT_EQ(m_dirty.at(placement.m_stampEntityId), DirtyHeight | DirtySurface);
                EXPECT_EQ(Get(3).m_configuration.m_priority, placement.m_configuration.m_priority);
                ExpectSnapshot(placement.m_heightmap, stale ? old : newest);
                ExpectIndexConsistent();
                RecordProperty(AZStd::string::format("%s_placement_%zu", stale ? "stale" : "current", unrelated).c_str(),
                    AZStd::string::format("claims=%zu fallback=%zu", traversal.m_claimsVisited,
                        traversal.m_fallbackRegistrationsVisited).c_str());
            }
        }
    }

    TEST_F(ImageRegistrationStateTests, BulkMatchingInsertionsVisitNoDependentClaims)
    {
        for (size_t count : { 32, 2048 })
        for (bool shared : { false, true })
        for (AZ::u64 revision : { 0, 9 })
        {
            m_state.Clear();
            m_dirty.clear();
            AZStd::unordered_map<AZ::EntityId, Record> reference;
            AZStd::unordered_map<AZ::EntityId, AZ::u8> expectedDirty;
            const auto classify = [](const auto* old, const auto&) { EXPECT_EQ(old, nullptr); return DirtySurface; };
            auto input = Make(1, 0, Snapshot(m_asset, revision));
            for (const auto& role : ImageAssetRoles)
                input.*role.m_snapshot = shared ? input.m_heightmap : Snapshot({ AZ::Uuid::CreateRandom(), 1 }, revision);
            m_state.Apply(input, m_dirty, classify);
            ApplyRegistrationState(input, input.m_stampEntityId, reference, expectedDirty, ImageAssetRoles, classify);
            RegistrationTraversal traversal;
            size_t scans = 0;
            for (AZ::u64 id = 2; id <= count + 1; ++id)
            {
                auto added = input;
                added.m_stampEntityId = AZ::EntityId(id);
                // Alternate exact and stale caller snapshots; reconciliation must precede the fast path.
                if (revision && id % 2 == 0)
                    for (const auto& role : ImageAssetRoles) { (added.*role.m_snapshot).m_revision = 0; }
                m_state.Apply(added, m_dirty, classify, &traversal);
                ApplyRegistrationState(added, added.m_stampEntityId, reference, expectedDirty, ImageAssetRoles, classify, &scans);
                EXPECT_EQ(added.m_heightmap.m_revision, revision && id % 2 == 0 ? 0 : revision);
            }
            EXPECT_EQ(traversal.m_claimsVisited, 0);
            EXPECT_EQ(traversal.m_fallbackRegistrationsVisited, 0);
            EXPECT_EQ(scans, 5 * count * (count + 2));
            EXPECT_EQ(m_dirty, expectedDirty);
            ASSERT_EQ(m_state.GetRegistrations().size(), reference.size());
            for (const auto& [id, expected] : reference)
                for (const auto& role : ImageAssetRoles)
                    ExpectSnapshot(m_state.GetRegistrations().at(id).*role.m_snapshot, expected.*role.m_snapshot);
            ExpectIndexConsistent();
            RecordProperty(AZStd::string::format("bulk_%zu_%s_rev%llu", count, shared ? "shared" : "distinct", revision).c_str(),
                AZStd::string::format("claims=%zu fallback=%zu scan=%zu", traversal.m_claimsVisited,
                    traversal.m_fallbackRegistrationsVisited, scans).c_str());
        }
    }

    TEST_F(ImageRegistrationStateTests, MatchingAdditionsPreserveTiesAndPendingMaintenance)
    {
        AZStd::unordered_map<AZ::EntityId, Record> reference;
        AZStd::unordered_map<AZ::EntityId, AZ::u8> expectedDirty;
        const auto classify = [](const auto*, const auto&) { return DirtySurface; };
        const auto check = [&]
        {
            EXPECT_EQ(m_dirty, expectedDirty);
            ASSERT_EQ(m_state.GetRegistrations().size(), reference.size());
            for (const auto& [id, expected] : reference)
                for (const auto& role : ImageAssetRoles)
                    ExpectSnapshot(m_state.GetRegistrations().at(id).*role.m_snapshot, expected.*role.m_snapshot);
            ExpectIndexConsistent();
        };
        const auto apply = [&](const Record& record)
        {
            RegistrationTraversal traversal;
            m_state.Apply(record, m_dirty, classify, &traversal);
            ApplyRegistrationState(record, record.m_stampEntityId, reference, expectedDirty, ImageAssetRoles, classify);
            check();
            return traversal;
        };
        const auto first = Snapshot(m_asset, 9), other = Snapshot(m_otherAsset, 9);
        const auto conflict = Snapshot(m_asset, 9, HeightmapDataStatus::Error);
        auto input = Make(1, 0, first);
        input.m_surfaceIdA = other;
        apply(input);
        apply(Make(2, 0, conflict));
        EXPECT_EQ(apply(Make(3, 4, first)).m_claimsVisited, 0); // Keep the existing conflicting tie.
        EXPECT_GT(apply(Make(4, 0, Snapshot(m_asset, 0))).m_fallbackRegistrationsVisited, 0);
        input.m_surfaceIdB = first; // A new role on an existing record.
        EXPECT_EQ(apply(input).m_claimsVisited, 0);
        input.m_heightmap = other;
        input.m_surfaceIdA = first; // Swap assets: matching additions must not cancel either removal's rebuild.
        EXPECT_GT(apply(input).m_claimsVisited, 0);
        input.m_surfaceIdA = conflict;
        input.m_surfaceBlend = first; // An earlier changed role still requires rebuilding this asset.
        EXPECT_GT(apply(input).m_claimsVisited, 0);
        for (AZ::u64 id : { 1, 2, 3, 4 })
        {
            EXPECT_EQ(m_state.Remove(AZ::EntityId(id)), reference.erase(AZ::EntityId(id)) != 0);
            check();
        }
        apply(Make(1, 0, Snapshot(m_asset, 0))); // Final removal retires the cached revision.
    }

    TEST_F(ImageRegistrationStateTests, BulkRemovalsAndRetargetsSkipStableRebuildsButRetainSearchesAndShifts)
    {
        for (size_t count : { 32, 2048 })
        for (bool shared : { false, true })
        for (bool reverse : { false, true })
        for (bool retarget : { false, true })
        {
            m_state.Clear();
            m_dirty.clear();
            AZStd::unordered_map<AZ::EntityId, Record> reference;
            AZStd::unordered_map<AZ::EntityId, AZ::u8> expectedDirty;
            const auto classify = [](const auto* old, const auto&) { return old ? DirtySurface : DirtyHeight; };
            const auto apply = [&](const Record& record, RegistrationTraversal* traversal = nullptr)
            {
                m_state.Apply(record, m_dirty, classify, traversal);
                ApplyRegistrationState(record, record.m_stampEntityId, reference, expectedDirty, ImageAssetRoles, classify);
            };
            auto input = Make(1, 0, Snapshot(m_asset, 9));
            auto destination = Make(count + 1, 0, Snapshot(m_otherAsset, 9));
            for (const auto& role : ImageAssetRoles)
            {
                input.*role.m_snapshot = shared ? input.m_heightmap : Snapshot({ AZ::Uuid::CreateRandom(), 1 }, 9);
                destination.*role.m_snapshot = shared ? destination.m_heightmap : Snapshot({ AZ::Uuid::CreateRandom(), 1 }, 9);
            }
            for (AZ::u64 id = 1; id <= count; ++id) { input.m_stampEntityId = AZ::EntityId(id); apply(input); }
            apply(destination);
            RegistrationTraversal traversal;
            for (AZ::u64 step = 1; step <= count; ++step)
            {
                const AZ::EntityId id(reverse ? count + 1 - step : step);
                if (retarget)
                {
                    auto replacement = destination;
                    replacement.m_stampEntityId = id;
                    for (const auto& role : ImageAssetRoles) { (replacement.*role.m_snapshot).m_revision = 0; }
                    apply(replacement, &traversal);
                    for (const auto& role : ImageAssetRoles)
                    {
                        EXPECT_EQ((replacement.*role.m_snapshot).m_revision, 0);
                        ExpectSnapshot(m_state.GetRegistrations().at(id).*role.m_snapshot, reference.at(id).*role.m_snapshot);
                    }
                }
                else { EXPECT_EQ(m_state.Remove(id, &traversal), reference.erase(id) != 0); }
            }
            const size_t pairs = count * (count - 1) / 2;
            const size_t searches = 5 * count + (reverse ? (shared ? 25 : 5) * pairs : 0);
            const size_t shifts = reverse ? (shared ? 10 * count : 0) : (shared ? 5 * count * (5 * count - 1) / 2 : 5 * pairs);
            EXPECT_EQ(traversal.m_claimsSearched, searches);
            EXPECT_EQ(traversal.m_claimsRebuilt, 0);
            EXPECT_EQ(traversal.m_claimsShifted, shifts);
            EXPECT_EQ(traversal.m_claimsVisited, searches);
            EXPECT_EQ(traversal.m_fallbackRegistrationsVisited, 0);
            EXPECT_EQ(m_dirty, expectedDirty);
            ASSERT_EQ(m_state.GetRegistrations().size(), reference.size());
            for (const auto& [id, expected] : reference)
                for (const auto& role : ImageAssetRoles)
                    ExpectSnapshot(m_state.GetRegistrations().at(id).*role.m_snapshot, expected.*role.m_snapshot);
            ExpectIndexConsistent();
            RecordProperty(AZStd::string::format("bulk_%zu_%s_%s_%s", count, shared ? "shared" : "distinct",
                reverse ? "reverse" : "forward", retarget ? "retarget" : "remove").c_str(),
                AZStd::string::format("visits=%zu searches=%zu rebuilt=%zu shifts=%zu fallback=%zu", traversal.m_claimsVisited,
                    traversal.m_claimsSearched, traversal.m_claimsRebuilt, traversal.m_claimsShifted,
                    traversal.m_fallbackRegistrationsVisited).c_str());
            // Every old asset lost its final claim, so reusing its ID starts fresh revision history.
            input.m_stampEntityId = AZ::EntityId(count + 2);
            for (const auto& role : ImageAssetRoles) { (input.*role.m_snapshot).m_revision = 0; }
            apply(input);
            for (const auto& role : ImageAssetRoles) { ExpectSnapshot(Get(count + 2).*role.m_snapshot, input.*role.m_snapshot); }
            ExpectIndexConsistent();
        }
    }

    TEST_F(ImageRegistrationStateTests, StableRemovalsPreserveChangesToOtherRolesInEitherOrder)
    {
        for (bool reverse : { false, true })
        for (bool retarget : { false, true })
        for (AZ::u64 revision : { 9, 10 })
        {
            m_state.Clear();
            m_dirty.clear();
            AZStd::unordered_map<AZ::EntityId, Record> reference;
            AZStd::unordered_map<AZ::EntityId, AZ::u8> expectedDirty;
            const auto classify = [](const auto*, const auto&) { return DirtySurface; };
            const auto check = [&]
            {
                EXPECT_EQ(m_dirty, expectedDirty);
                ASSERT_EQ(m_state.GetRegistrations().size(), reference.size());
                for (const auto& [id, expected] : reference)
                    for (const auto& role : ImageAssetRoles)
                        ExpectSnapshot(m_state.GetRegistrations().at(id).*role.m_snapshot, expected.*role.m_snapshot);
                ExpectIndexConsistent();
            };
            const auto apply = [&](const Record& record)
            {
                RegistrationTraversal traversal;
                m_state.Apply(record, m_dirty, classify, &traversal);
                ApplyRegistrationState(record, record.m_stampEntityId, reference, expectedDirty, ImageAssetRoles, classify);
                check();
                return traversal;
            };
            const auto first = Snapshot(m_asset, 9), other = Snapshot(m_otherAsset, 9);
            auto input = Make(1, 0, first);
            input.m_surfaceIdA = first;
            input.m_holeMask = first;
            apply(input);
            apply(Make(2, 0, first));
            apply(Make(5, 0, other));
            input.*ImageAssetRoles[reverse ? 1 : 0].m_snapshot = Snapshot(m_asset, revision, HeightmapDataStatus::Error);
            input.*ImageAssetRoles[reverse ? 0 : 1].m_snapshot = retarget ? other : HeightmapDataSnapshot{};
            const auto changed = apply(input);
            EXPECT_EQ(changed.m_claimsSearched, reverse ? 1 : 2);
            EXPECT_EQ(changed.m_claimsRebuilt, revision == 9 ? 3 : 0);
            EXPECT_EQ(apply(Make(3, 0, Snapshot(m_asset, 0))).m_fallbackRegistrationsVisited > 0, revision == 9);
            EXPECT_EQ(m_state.Remove(AZ::EntityId(1)), reference.erase(AZ::EntityId(1)) != 0);
            EXPECT_EQ(m_state.Remove(AZ::EntityId(3)), reference.erase(AZ::EntityId(3)) != 0);
            check();
            EXPECT_EQ(apply(Make(4, 4, Snapshot(m_asset, 0))).m_fallbackRegistrationsVisited, 0);
        }
    }

    TEST_F(ImageRegistrationStateTests, ChangingOneImageRoleRebuildsOnlyItsAsset)
    {
        for (size_t changedRole = 0; changedRole < AZ_ARRAY_SIZE(ImageAssetRoles); ++changedRole)
        {
            m_state.Clear();
            m_dirty.clear();
            AZStd::unordered_map<AZ::EntityId, Record> reference;
            AZStd::unordered_map<AZ::EntityId, AZ::u8> expectedDirty;
            const auto classify = [](const auto*, const auto&) { return AZ::u8{ 0 }; };
            auto input = Make(1, 0, {});
            for (const auto& role : ImageAssetRoles) { input.*role.m_snapshot = Snapshot({ AZ::Uuid::CreateRandom(), 1 }, 1); }
            for (AZ::u64 id : { 1, 2 })
            {
                input.m_stampEntityId = AZ::EntityId(id);
                Apply(input);
                ApplyRegistrationState(input, input.m_stampEntityId, reference, expectedDirty, ImageAssetRoles, classify);
            }
            input.m_stampEntityId = AZ::EntityId(1);
            auto& changed = input.*ImageAssetRoles[changedRole].m_snapshot;
            changed = Snapshot(changed.m_assetId, 2);
            RegistrationTraversal traversal;
            ApplyRegistrationState(input, input.m_stampEntityId, reference, expectedDirty, ImageAssetRoles, classify);
            m_state.Apply(input, m_dirty, classify, &traversal);
            EXPECT_EQ(traversal.m_claimsVisited, 2);
            EXPECT_EQ(traversal.m_fallbackRegistrationsVisited, 0);
            EXPECT_EQ(m_dirty, expectedDirty);
            for (const auto& [id, expected] : reference)
            {
                for (const auto& role : ImageAssetRoles)
                    ExpectSnapshot(m_state.GetRegistrations().at(id).*role.m_snapshot, expected.*role.m_snapshot);
            }
            ExpectIndexConsistent();
        }
    }

    TEST_F(ImageRegistrationStateTests, ConflictingTiesRetainTheScanChoiceAcrossRehashAndRemoval)
    {
        const auto first = Make(1, 0, Snapshot(m_asset, 10));
        const auto second = Make(2, 0, Snapshot(m_asset, 10, HeightmapDataStatus::Error));
        Apply(first);
        Apply(second);
        for (AZ::u64 id = 100; id < 2148; ++id) { Apply(Make(id, 0, Snapshot(m_otherAsset, 1, HeightmapDataStatus::Loading))); }
        HeightmapDataSnapshot expected;
        AZ::EntityId representative;
        for (const auto& [id, record] : m_state.GetRegistrations())
        {
            if (record.m_heightmap.m_assetId == m_asset)
            {
                expected = record.m_heightmap;
                representative = id;
                break;
            }
        }
        RegistrationTraversal traversal;
        m_state.Apply(Make(3, 0, Snapshot(m_asset, 1)), m_dirty,
            [](const auto*, const auto&) { return AZ::u8{ 0 }; }, &traversal);
        EXPECT_GT(traversal.m_fallbackRegistrationsVisited, 0);
        ExpectSnapshot(Get(3).m_heightmap, expected);
        ASSERT_TRUE(m_state.Remove(representative == AZ::EntityId(1) ? AZ::EntityId(2) : AZ::EntityId(1)));
        traversal = {};
        m_state.Apply(Make(4, 4, Snapshot(m_asset, 1)), m_dirty,
            [](const auto*, const auto&) { return AZ::u8{ 0 }; }, &traversal);
        EXPECT_EQ(traversal.m_fallbackRegistrationsVisited, 0);
        ExpectSnapshot(Get(4).m_holeMask, expected);
        ExpectIndexConsistent();
    }

    TEST_F(ImageRegistrationStateTests, MixedOperationsMatchTheProductionScanAndMaintainExactIndexMembership)
    {
        AZStd::unordered_map<AZ::EntityId, Record> reference;
        AZStd::unordered_map<AZ::EntityId, AZ::u8> expectedDirty;
        AZStd::fixed_vector<AZ::Data::AssetId, 6> assets;
        for (size_t i = 0; i < 6; ++i) { assets.push_back({ AZ::Uuid::CreateRandom(), 1 }); }
        const int testSeed = ::testing::UnitTest::GetInstance()->random_seed();
        AZ::u32 seed = testSeed ? static_cast<AZ::u32>(testSeed) : 173;
        SCOPED_TRACE(seed);
        const auto next = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        const auto classify = [](const Record* old, const Record& current)
        {
            return old && old->m_configuration.m_priority == current.m_configuration.m_priority ? AZ::u8{ 0 } : DirtyHeight;
        };
        for (size_t operation = 0; operation < 1000; ++operation)
        {
            SCOPED_TRACE(operation);
            const AZ::EntityId id(1 + next() % 64);
            const AZ::u32 action = next() % 20;
            if (action == 0)
            {
                m_state.Clear();
                reference.clear();
                m_dirty.clear();
                expectedDirty.clear();
            }
            else if (action < 5) { EXPECT_EQ(m_state.Remove(id), reference.erase(id) != 0); }
            else
            {
                Record record;
                record.m_stampEntityId = id;
                record.m_configuration.m_priority = next() % 8;
                for (const auto& role : ImageAssetRoles)
                {
                    const AZ::Data::AssetId asset = next() % 8 == 0 ? AZ::Data::AssetId{} : assets[next() % assets.size()];
                    record.*role.m_snapshot = Snapshot(asset, next() % 8, static_cast<HeightmapDataStatus>(next() % 6));
                }
                ApplyRegistrationState(record, id, reference, expectedDirty, ImageAssetRoles, classify);
                m_state.Apply(record, m_dirty, classify);
            }
            ASSERT_EQ(m_state.GetRegistrations().size(), reference.size());
            auto actual = m_state.GetRegistrations().begin();
            for (const auto& [expectedId, expected] : reference)
            {
                ASSERT_EQ(actual->first, expectedId);
                EXPECT_EQ(actual->second.m_configuration.m_priority, expected.m_configuration.m_priority);
                for (const auto& role : ImageAssetRoles)
                {
                    ExpectSnapshot(actual->second.*role.m_snapshot, expected.*role.m_snapshot);
                }
                ++actual;
            }
            EXPECT_EQ(m_dirty, expectedDirty);
            ExpectIndexConsistent();
        }
    }
}
