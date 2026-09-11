#include <AzTest/AzTest.h>
#include <AzCore/std/containers/array.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include "RegistrationStateReference.h"

namespace TerrainCompositor::Internal
{
    struct CutoutIndexKind
    {
        using Record = TerrainMeshCutoutRegistrationData;
        using Data = TerrainMeshCutoutData;
        using State = CutoutRegistrationState;
        static constexpr auto& Roles = CutoutAssetRoles;
        static constexpr auto Entity = &Record::m_cutoutEntityId;
    };
    struct MeshHeightIndexKind
    {
        using Record = TerrainMeshHeightStampRegistrationData;
        using Data = TerrainMeshHeightData;
        using State = MeshHeightRegistrationState;
        static constexpr auto& Roles = MeshHeightAssetRoles;
        static constexpr auto Entity = &Record::m_stampEntityId;
    };

    template<class Kind>
    class MeshRegistrationStateTests : public ::testing::Test
    {
    protected:
        using Record = typename Kind::Record;
        using Snapshot = decltype(Record{}.m_mesh);
        using Status = decltype(Snapshot{}.m_status);
        using State = typename Kind::State;
        static Snapshot MakeSnapshot(AZ::Data::AssetId asset, AZ::u64 revision, Status status = Status::Ready)
        {
            Snapshot result;
            result.m_assetId = asset;
            result.m_revision = revision;
            result.m_status = status;
            if (status == Status::Ready)
            {
                auto data = AZStd::make_shared<typename Kind::Data>();
                data->m_assetId = asset;
                data->m_revision = revision;
                result.m_data = data;
            }
            return result;
        }
        static Record Make(AZ::u64 id, const Snapshot& snapshot)
        {
            Record record;
            record.*Kind::Entity = AZ::EntityId(id);
            record.m_mesh = snapshot;
            return record;
        }
        static void ExpectSnapshot(const Snapshot& actual, const Snapshot& expected)
        {
            EXPECT_EQ(actual.m_assetId, expected.m_assetId);
            EXPECT_EQ(actual.m_revision, expected.m_revision);
            EXPECT_EQ(actual.m_status, expected.m_status);
            EXPECT_EQ(actual.m_data, expected.m_data);
            EXPECT_EQ(actual.m_validation, expected.m_validation);
            if constexpr (requires { actual.m_diagnostics; })
            {
                EXPECT_EQ(actual.m_modelValidation, expected.m_modelValidation);
                const auto& a = actual.m_diagnostics;
                const auto& b = expected.m_diagnostics;
                EXPECT_EQ(a.m_totalOffenseCount, b.m_totalOffenseCount);
                EXPECT_EQ(a.m_localBounds, b.m_localBounds);
                EXPECT_EQ(a.m_localOrigin, b.m_localOrigin);
                EXPECT_EQ(a.m_gridSpacing, b.m_gridSpacing);
                EXPECT_EQ(a.m_gridWidth, b.m_gridWidth);
                EXPECT_EQ(a.m_gridHeight, b.m_gridHeight);
                ASSERT_EQ(a.m_details.size(), b.m_details.size());
                for (size_t i = 0; i < a.m_details.size(); ++i)
                {
                    EXPECT_EQ(a.m_details[i].m_validation, b.m_details[i].m_validation);
                    EXPECT_EQ(a.m_details[i].m_gridX, b.m_details[i].m_gridX);
                    EXPECT_EQ(a.m_details[i].m_gridY, b.m_details[i].m_gridY);
                    EXPECT_EQ(a.m_details[i].m_triangleIndex, b.m_details[i].m_triangleIndex);
                    EXPECT_EQ(a.m_details[i].m_relatedTriangleIndex, b.m_details[i].m_relatedTriangleIndex);
                }
            }
        }
        void Apply(const Record& record, AZ::u8 direct = 0)
        {
            m_state.Apply(record, m_dirty, [direct](const auto*, const auto&) { return direct; });
        }
        const Snapshot& Get(AZ::u64 id) const { return m_state.GetRegistrations().at(AZ::EntityId(id)).m_mesh; }
        void ExpectDirty(const AZStd::unordered_map<AZ::EntityId, AZ::u8>& expected) const
        {
            // AZStd map equality compares iteration order; dirty masks are keyed by entity.
            ASSERT_EQ(m_dirty.size(), expected.size());
            for (const auto& [id, mask] : expected)
            {
                const auto found = m_dirty.find(id);
                ASSERT_NE(found, m_dirty.end());
                EXPECT_EQ(found->second, mask);
            }
        }
        void ExpectIndexConsistent()
        {
            size_t claims = 0;
            for (const auto& [id, record] : m_state.m_records)
            {
                const auto found = m_state.m_assets.find(record.m_mesh.m_assetId);
                ASSERT_NE(found, m_state.m_assets.end());
                EXPECT_EQ(AZStd::count_if(found->second.m_claims.begin(), found->second.m_claims.end(),
                    [&](const auto& claim) { return claim.m_entityId == id && claim.m_role == 0; }), 1);
            }
            for (const auto& [id, asset] : m_state.m_assets)
            {
                ASSERT_FALSE(asset.m_claims.empty());
                claims += asset.m_claims.size();
                AZ::u64 maximum = 0;
                for (const auto& claim : asset.m_claims)
                {
                    const auto found = m_state.m_records.find(claim.m_entityId);
                    ASSERT_NE(found, m_state.m_records.end());
                    EXPECT_EQ(claim.m_role, 0);
                    EXPECT_EQ(found->second.m_mesh.m_assetId, id);
                    maximum = AZStd::max(maximum, found->second.m_mesh.m_revision);
                    if (id.IsValid()) { EXPECT_EQ(found->second.m_mesh.m_revision, asset.m_latest.m_revision); }
                }
                EXPECT_EQ(asset.m_latest.m_assetId, id);
                EXPECT_EQ(asset.m_latest.m_revision, maximum);
            }
            EXPECT_EQ(claims, m_state.m_records.size());
        }
        State m_state;
        AZStd::unordered_map<AZ::EntityId, AZ::u8> m_dirty;
        const AZ::Data::AssetId m_asset{ AZ::Uuid::CreateRandom(), 1 }, m_otherAsset{ AZ::Uuid::CreateRandom(), 1 };
    };
    using MeshIndexKinds = ::testing::Types<CutoutIndexKind, MeshHeightIndexKind>;
    TYPED_TEST_SUITE(MeshRegistrationStateTests, MeshIndexKinds);

    TYPED_TEST(MeshRegistrationStateTests, ReconcilesBeforeClassificationAndPreservesCallerAndDirtyBits)
    {
        const auto newest = this->MakeSnapshot(this->m_asset, 9, TestFixture::Status::Error);
        this->Apply(this->Make(1, newest));
        const auto stale = this->Make(1, this->MakeSnapshot(this->m_asset, 1));
        this->m_dirty[AZ::EntityId(1)] = DirtySurface;
        int classified = 0;
        this->m_state.Apply(stale, this->m_dirty, [&](const auto* old, const auto& current)
        {
            ++classified;
            EXPECT_NE(old, nullptr);
            this->ExpectSnapshot(current.m_mesh, newest);
            return TypeParam::Roles[0].m_dirty;
        });
        EXPECT_EQ(classified, 1);
        EXPECT_EQ(stale.m_mesh.m_revision, 1);
        EXPECT_EQ(this->m_dirty.at(AZ::EntityId(1)), DirtySurface | TypeParam::Roles[0].m_dirty);
    }

    TYPED_TEST(MeshRegistrationStateTests, FailureAndRecoveryFanOutOnlyWithinTheCanonicalAsset)
    {
        const auto retained = this->MakeSnapshot(this->m_asset, 1);
        this->Apply(this->Make(1, retained));
        this->Apply(this->Make(2, retained));
        const auto unrelated = this->MakeSnapshot(this->m_otherAsset, 1);
        this->Apply(this->Make(3, unrelated));
        AZ::u64 revision = 1;
        for (auto status : { TestFixture::Status::Loading, TestFixture::Status::Missing, TestFixture::Status::Error,
            TestFixture::Status::Unsupported, TestFixture::Status::InvalidGeometry, TestFixture::Status::Ready })
        {
            const auto update = this->MakeSnapshot(this->m_asset, ++revision, status);
            this->m_dirty.clear();
            this->m_dirty[AZ::EntityId(2)] = DirtySurface;
            this->Apply(this->Make(1, update));
            this->ExpectSnapshot(this->Get(1), update);
            this->ExpectSnapshot(this->Get(2), update);
            this->ExpectSnapshot(this->Get(3), unrelated);
            ASSERT_EQ(this->m_dirty.size(), 1);
            EXPECT_EQ(this->m_dirty.at(AZ::EntityId(2)), DirtySurface | TypeParam::Roles[0].m_dirty);
            EXPECT_EQ(retained.m_data->m_revision, 1);
        }
    }

    TYPED_TEST(MeshRegistrationStateTests, EqualRevisionsRetainEveryPayloadAndStaleInputsUseMapOrder)
    {
        for (bool unassigned : { false, true })
        {
            this->m_state.Clear();
            const auto asset = unassigned ? AZ::Data::AssetId{} : this->m_asset;
            const auto first = this->MakeSnapshot(asset, 9);
            auto second = first;
            // Even a validation-only difference must preserve the scan's choice.
            second.m_validation = decltype(second.m_validation)::Empty;
            this->Apply(this->Make(1, first));
            this->Apply(this->Make(2, second));
            this->ExpectSnapshot(this->Get(1), first);
            this->ExpectSnapshot(this->Get(2), second);
            for (AZ::u64 id = 100; id < 2148; ++id)
                this->Apply(this->Make(id, this->MakeSnapshot(this->m_otherAsset, 1, TestFixture::Status::Loading)));
            typename TestFixture::Snapshot expected;
            AZ::EntityId representative;
            for (const auto& [id, record] : this->m_state.GetRegistrations())
            {
                if (record.m_mesh.m_assetId == asset)
                {
                    expected = record.m_mesh;
                    representative = id;
                    break;
                }
            }
            this->Apply(this->Make(3, this->MakeSnapshot(asset, 1)));
            this->ExpectSnapshot(this->Get(3), expected);
            EXPECT_TRUE(this->m_state.Remove(representative));
            EXPECT_TRUE(this->m_state.Remove(AZ::EntityId(3)));
            const auto survivor = representative == AZ::EntityId(1) ? second : first;
            this->Apply(this->Make(4, this->MakeSnapshot(asset, 1)));
            this->ExpectSnapshot(this->Get(4), survivor);
            EXPECT_TRUE(this->m_dirty.empty());
        }
    }

    TYPED_TEST(MeshRegistrationStateTests, UnassignedClaimsReconcileButNeverFanOutAndMaximumCanDecrease)
    {
        const auto low = this->MakeSnapshot({}, 1);
        const auto high = this->MakeSnapshot({}, 9, TestFixture::Status::Missing);
        this->Apply(this->Make(1, low));
        this->Apply(this->Make(2, high));
        this->ExpectSnapshot(this->Get(1), low);
        this->Apply(this->Make(3, this->MakeSnapshot({}, 0)));
        this->ExpectSnapshot(this->Get(3), high);
        EXPECT_TRUE(this->m_state.Remove(AZ::EntityId(2)));
        this->Apply(this->Make(3, this->MakeSnapshot(this->m_asset, 2)));
        this->Apply(this->Make(4, this->MakeSnapshot({}, 0)));
        this->ExpectSnapshot(this->Get(4), low);
        EXPECT_TRUE(this->m_dirty.empty());
    }

    TYPED_TEST(MeshRegistrationStateTests, RetargetRemovalAndClearRetireOnlyTheFinalClaimHistory)
    {
        for (bool unassigned : { false, true })
        {
            this->m_state.Clear();
            const auto asset = unassigned ? AZ::Data::AssetId{} : this->m_asset;
            const auto old = this->MakeSnapshot(asset, 20);
            this->Apply(this->Make(1, old));
            this->Apply(this->Make(2, old));
            this->Apply(this->Make(1, this->MakeSnapshot(this->m_otherAsset, 1)));
            this->Apply(this->Make(3, this->MakeSnapshot(asset, 0)));
            this->ExpectSnapshot(this->Get(3), old);
            EXPECT_TRUE(this->m_state.Remove(AZ::EntityId(2)));
            EXPECT_TRUE(this->m_state.Remove(AZ::EntityId(3)));
            EXPECT_FALSE(this->m_state.Remove(AZ::EntityId(3)));
            const auto fresh = this->MakeSnapshot(asset, 1);
            this->Apply(this->Make(2, fresh));
            this->ExpectSnapshot(this->Get(2), fresh);
            this->m_state.Clear();
            EXPECT_TRUE(this->m_state.GetRegistrations().empty());
            const auto restarted = this->MakeSnapshot(asset, 0);
            this->Apply(this->Make(2, restarted));
            this->ExpectSnapshot(this->Get(2), restarted);
        }
    }

    TYPED_TEST(MeshRegistrationStateTests, EveryEqualRevisionPayloadFieldPreservesTheFirstMaximum)
    {
        using Snapshot = typename TestFixture::Snapshot;
        const auto check = [&](auto edit)
        {
            this->m_state.Clear();
            const auto first = this->MakeSnapshot(this->m_asset, 9);
            auto second = first;
            edit(second);
            this->Apply(this->Make(1, first));
            this->Apply(this->Make(2, first));
            this->Apply(this->Make(2, second));
            const auto expected = this->m_state.GetRegistrations().begin()->second.m_mesh;
            RegistrationTraversal traversal;
            this->m_state.Apply(this->Make(3, this->MakeSnapshot(this->m_asset, 1)), this->m_dirty,
                [](const auto*, const auto&) { return AZ::u8{ 0 }; }, &traversal);
            EXPECT_GT(traversal.m_fallbackRegistrationsVisited, 0);
            this->ExpectSnapshot(this->Get(3), expected);
            ASSERT_TRUE(this->m_state.Remove(AZ::EntityId(3)));
            this->Apply(this->Make(2, first));
            traversal = {};
            this->m_state.Apply(this->Make(3, this->MakeSnapshot(this->m_asset, 1)), this->m_dirty,
                [](const auto*, const auto&) { return AZ::u8{ 0 }; }, &traversal);
            EXPECT_EQ(traversal.m_fallbackRegistrationsVisited, 0);
            this->ExpectSnapshot(this->Get(3), first);
        };
        check([](auto& s) { s.m_status = TestFixture::Status::Error; });
        check([](auto& s) { s.m_data.reset(); });
        check([](auto& s) { s.m_validation = decltype(s.m_validation)::Empty; });
        if constexpr (requires (Snapshot s) { s.m_diagnostics; })
        {
            check([](auto& s) { s.m_modelValidation = decltype(s.m_modelValidation)::Empty; });
            check([](auto& s) { s.m_diagnostics.m_totalOffenseCount = 1; });
            check([](auto& s) { s.m_diagnostics.m_localBounds = AZ::Aabb::CreateFromMinMaxValues(0, 0, 0, 1, 1, 1); });
            check([](auto& s) { s.m_diagnostics.m_localOrigin = AZ::Vector2(1, 2); });
            check([](auto& s) { s.m_diagnostics.m_gridSpacing = AZ::Vector2(1, 2); });
            check([](auto& s) { s.m_diagnostics.m_gridWidth = 1; });
            check([](auto& s) { s.m_diagnostics.m_gridHeight = 1; });
            check([](auto& s) { s.m_diagnostics.m_details.push_back({}); });
            // Equal-length detail lists can differ in any one field.
            for (size_t field = 0; field < 5; ++field)
            {
                this->m_state.Clear();
                auto first = this->MakeSnapshot(this->m_asset, 9);
                first.m_diagnostics.m_details.push_back({});
                auto second = first;
                auto& d = second.m_diagnostics.m_details.front();
                switch (field)
                {
                case 0: d.m_validation = TerrainMeshHeightValidation::Empty; break;
                case 1: d.m_gridX = 2; break;
                case 2: d.m_gridY = 3; break;
                case 3: d.m_triangleIndex = 4; break;
                case 4: d.m_relatedTriangleIndex = 5; break;
                }
                this->Apply(this->Make(1, first));
                this->Apply(this->Make(2, first));
                this->Apply(this->Make(2, second));
                const auto expected = this->m_state.GetRegistrations().begin()->second.m_mesh;
                RegistrationTraversal traversal;
                this->m_state.Apply(this->Make(3, this->MakeSnapshot(this->m_asset, 1)), this->m_dirty,
                    [](const auto*, const auto&) { return AZ::u8{ 0 }; }, &traversal);
                EXPECT_GT(traversal.m_fallbackRegistrationsVisited, 0);
                this->ExpectSnapshot(this->Get(3), expected);
            }
        }
    }

    TYPED_TEST(MeshRegistrationStateTests, IdenticalPayloadStillTracksRevisionChangesAndRetargeting)
    {
        for (bool unassigned : { false, true })
        {
            this->m_state.Clear();
            const auto asset = unassigned ? AZ::Data::AssetId{} : this->m_asset;
            const auto low = this->MakeSnapshot(asset, 1, TestFixture::Status::Loading);
            this->Apply(this->Make(1, low));
            this->Apply(this->Make(2, low));
            auto high = low;
            high.m_revision = 9;
            this->Apply(this->Make(1, high));
            this->ExpectSnapshot(this->Get(2), unassigned ? low : high);
            this->Apply(this->Make(2, low));
            this->ExpectSnapshot(this->Get(2), high);
            this->ExpectIndexConsistent();
            auto retargeted = high;
            retargeted.m_assetId = this->m_otherAsset;
            this->Apply(this->Make(1, retargeted));
            EXPECT_TRUE(this->m_state.Remove(AZ::EntityId(2)));
            this->Apply(this->Make(3, low));
            this->ExpectSnapshot(this->Get(3), low);
            this->ExpectIndexConsistent();
        }
    }

    TYPED_TEST(MeshRegistrationStateTests, MixedOperationsMatchTheOriginalScanAndMaintainExactIndexMembership)
    {
        using Record = typename TestFixture::Record;
        AZStd::unordered_map<AZ::EntityId, Record> reference;
        AZStd::unordered_map<AZ::EntityId, AZ::u8> expectedDirty;
        const AZStd::array assets{ this->m_asset, this->m_otherAsset, AZ::Data::AssetId{},
            AZ::Data::AssetId{ AZ::Uuid::CreateNull(), 1 }, AZ::Data::AssetId{ this->m_asset.m_guid, 2 } };
        const int testSeed = ::testing::UnitTest::GetInstance()->random_seed();
        AZ::u32 seed = testSeed ? static_cast<AZ::u32>(testSeed) : 173;
        SCOPED_TRACE(seed);
        const auto next = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        for (size_t operation = 0; operation < 1000; ++operation)
        {
            SCOPED_TRACE(operation);
            if (next() % 4 == 0) { this->m_dirty.clear(); expectedDirty.clear(); }
            const AZ::u64 entity = 1 + next() % 128;
            const AZ::EntityId id(entity);
            const AZ::u32 action = next() % 100;
            if (action == 0)
            {
                this->m_state.Clear();
                reference.clear();
                this->m_dirty.clear();
                expectedDirty.clear();
            }
            else if (action < 20) { EXPECT_EQ(this->m_state.Remove(id), reference.erase(id) != 0); }
            else
            {
                auto snapshot = this->MakeSnapshot(assets[next() % assets.size()], next() % 8,
                    static_cast<typename TestFixture::Status>(next() % (static_cast<int>(TestFixture::Status::Ready) + 1)));
                snapshot.m_validation = static_cast<decltype(snapshot.m_validation)>(next() % 4);
                if constexpr (requires { snapshot.m_diagnostics; })
                {
                    snapshot.m_modelValidation = static_cast<decltype(snapshot.m_modelValidation)>(next() % 3);
                    snapshot.m_diagnostics.m_totalOffenseCount = next() % 3;
                    snapshot.m_diagnostics.m_details.push_back({ TerrainMeshHeightValidation::Empty, next() % 3 });
                }
                auto record = this->Make(entity, snapshot);
                record.m_configuration.m_priority = next() % 8;
                Record observedOld, observedCurrent;
                bool hadOld = false;
                const AZ::u8 direct = static_cast<AZ::u8>(next() % 8);
                ApplyRegistrationState(record, id, reference, expectedDirty, TypeParam::Roles,
                    [&](const Record* old, const Record& current)
                    {
                        hadOld = old != nullptr;
                        if (old) { observedOld = *old; }
                        observedCurrent = current;
                        return direct;
                    });
                this->m_state.Apply(record, this->m_dirty, [&](const Record* old, const Record& current)
                {
                    EXPECT_EQ(old != nullptr, hadOld);
                    if (old) { this->ExpectSnapshot(old->m_mesh, observedOld.m_mesh); }
                    this->ExpectSnapshot(current.m_mesh, observedCurrent.m_mesh);
                    return direct;
                });
                this->ExpectSnapshot(record.m_mesh, snapshot);
            }
            ASSERT_EQ(this->m_state.GetRegistrations().size(), reference.size());
            auto actual = this->m_state.GetRegistrations().begin();
            for (const auto& [expectedId, expected] : reference)
            {
                ASSERT_EQ(actual->first, expectedId);
                EXPECT_EQ(actual->second.m_configuration.m_priority, expected.m_configuration.m_priority);
                this->ExpectSnapshot(actual->second.m_mesh, expected.m_mesh);
                ++actual;
            }
            this->ExpectDirty(expectedDirty);
            this->ExpectIndexConsistent();
        }
    }

    TYPED_TEST(MeshRegistrationStateTests, AssignedInsertionAndRemovalDoNotVisitUnassignedClaims)
    {
        for (AZ::u64 id = 100; id < 2148; ++id)
            this->Apply(this->Make(id, this->MakeSnapshot({}, 1, TestFixture::Status::Loading)));
        RegistrationTraversal traversal;
        this->m_state.Apply(this->Make(1, this->MakeSnapshot(this->m_asset, 2)), this->m_dirty,
            [](const auto*, const auto&) { return AZ::u8{ 0 }; }, &traversal);
        EXPECT_EQ(traversal.m_claimsVisited, 2);
        EXPECT_EQ(traversal.m_fallbackRegistrationsVisited, 0);
        traversal = {};
        EXPECT_TRUE(this->m_state.Remove(AZ::EntityId(1), &traversal));
        EXPECT_EQ(traversal.m_claimsVisited, 1);
        EXPECT_EQ(traversal.m_fallbackRegistrationsVisited, 0);
        this->ExpectIndexConsistent();
    }

    TYPED_TEST(MeshRegistrationStateTests, UnrelatedRegistrationsDoNotIncreaseRevisionOrPlacementTraversal)
    {
        for (bool unassigned : { false, true })
        {
            for (size_t unrelated : { 32, 2048 })
            {
                this->m_state.Clear();
                this->m_dirty.clear();
                AZStd::unordered_map<AZ::EntityId, typename TestFixture::Record> reference;
                AZStd::unordered_map<AZ::EntityId, AZ::u8> expectedDirty;
                const auto classify = [](const auto* old, const auto& current)
                {
                    return old && old->m_configuration.m_priority != current.m_configuration.m_priority
                        ? TypeParam::Roles[0].m_dirty : AZ::u8{ 0 };
                };
                const auto seed = [&](const auto& record)
                {
                    this->Apply(record);
                    ApplyRegistrationState(record, record.*TypeParam::Entity, reference, expectedDirty, TypeParam::Roles, classify);
                };
                for (size_t i = 0; i < unrelated; ++i)
                    seed(this->Make(100 + i, this->MakeSnapshot({ AZ::Uuid::CreateRandom(), 1 }, 1)));
                const auto asset = unassigned ? AZ::Data::AssetId{} : this->m_asset;
                const auto old = this->MakeSnapshot(asset, 1);
                for (AZ::u64 i = 1; i <= 7; ++i) { seed(this->Make(i, old)); }
                for (bool placement : { false, true })
                {
                    auto record = this->Make(1, placement ? old : this->MakeSnapshot(asset, 2));
                    record.m_configuration.m_priority = placement ? 7 : 0;
                    RegistrationTraversal traversal;
                    size_t scans = 0;
                    ApplyRegistrationState(record, record.*TypeParam::Entity, reference, expectedDirty, TypeParam::Roles, classify, &scans);
                    this->m_state.Apply(record, this->m_dirty, classify, &traversal);
                    EXPECT_EQ(traversal.m_claimsVisited, placement ? 0 : (unassigned ? 7 : 14));
                    EXPECT_EQ(traversal.m_fallbackRegistrationsVisited, 0);
                    EXPECT_EQ(scans, (unassigned ? 1 : 2) * (unrelated + 7));
                    this->ExpectDirty(expectedDirty);
                    for (const auto& [id, expected] : reference)
                        this->ExpectSnapshot(this->m_state.GetRegistrations().at(id).m_mesh, expected.m_mesh);
                    this->ExpectIndexConsistent();
                    AZ_Printf("MeshRegistrationState", "%s %s, %zu unrelated: %zu claims, %zu fallback records, %zu scan records.\n",
                        unassigned ? "unassigned" : "assigned", placement ? "placement" : "revision", unrelated,
                        traversal.m_claimsVisited, traversal.m_fallbackRegistrationsVisited, scans);
                    this->RecordProperty(AZStd::string::format("%s_%s_%zu", unassigned ? "unassigned" : "assigned",
                        placement ? "placement" : "revision", unrelated).c_str(),
                        AZStd::string::format("claims=%zu fallback=%zu scan=%zu", traversal.m_claimsVisited,
                            traversal.m_fallbackRegistrationsVisited, scans).c_str());
                }
            }
        }
    }
}
