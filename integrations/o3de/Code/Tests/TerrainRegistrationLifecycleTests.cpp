#include <AzTest/AzTest.h>
#include <AzCore/std/containers/array.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzFramework/Scene/SceneSystemComponent.h>
#include <AzFramework/Asset/AssetCatalogBus.h>
#include <LmbrCentral/Shape/MockShapes.h>
#include <TerrainCompositor/Components/TerrainCompositionGradientComponent.h>
#include <TerrainCompositor/HeightmapStampRegistration.h>
#include <TerrainCompositor/TerrainMeshCutoutRegistration.h>
#include <TerrainCompositor/TerrainMeshHeightStampRegistration.h>
#include "TerrainTestFixtures.h"
#include "CompositionPreparation.h"
#include <type_traits>

namespace TerrainCompositor
{
    namespace RegistrationTestSupport
    {
        struct Image
        {
            using Record = HeightmapStampRegistrationData;
            using Lease = HeightmapStampRegistration;
            using Cache = HeightmapDataCache;
            static constexpr auto Register = &TerrainCompositionRequests::RegisterStamp;
            static constexpr auto Unregister = &TerrainCompositionRequests::UnregisterStamp;
            static constexpr auto Enumerate = &TerrainCompositionRequests::GetRegisteredStamps;
            static AZ::EntityId& Entity(Record& record) { return record.m_stampEntityId; }
            static auto& Snapshot(Record& record) { return record.m_heightmap; }
            static auto Assets(Record& record)
            {
                auto& config = record.m_configuration;
                return AZStd::array{ &config.m_heightmapAsset, &config.m_surfaceMaps.m_surfaceIdAAsset,
                    &config.m_surfaceMaps.m_surfaceIdBAsset, &config.m_surfaceMaps.m_blendMaskAsset,
                    &config.m_holeMask.m_maskAsset };
            }
            static auto Snapshots(Record& record)
            {
                return AZStd::array{ &record.m_heightmap, &record.m_surfaceIdA, &record.m_surfaceIdB,
                    &record.m_surfaceBlend, &record.m_holeMask };
            }
        };

        struct Cutout
        {
            using Record = TerrainMeshCutoutRegistrationData;
            using Lease = TerrainMeshCutoutRegistration;
            using Cache = TerrainMeshCutoutDataCache;
            static constexpr auto Register = &TerrainCompositionRequests::RegisterMeshCutout;
            static constexpr auto Unregister = &TerrainCompositionRequests::UnregisterMeshCutout;
            static constexpr auto Enumerate = &TerrainCompositionRequests::GetRegisteredMeshCutouts;
            static AZ::EntityId& Entity(Record& record) { return record.m_cutoutEntityId; }
            static auto& Snapshot(Record& record) { return record.m_mesh; }
            static auto Assets(Record& record) { return AZStd::array{ &record.m_configuration.m_cutoutMeshAsset }; }
            static auto Snapshots(Record& record) { return AZStd::array{ &record.m_mesh }; }
        };

        struct MeshHeight
        {
            using Record = TerrainMeshHeightStampRegistrationData;
            using Lease = TerrainMeshHeightStampRegistration;
            using Cache = TerrainMeshHeightDataCache;
            static constexpr auto Register = &TerrainCompositionRequests::RegisterMeshHeightStamp;
            static constexpr auto Unregister = &TerrainCompositionRequests::UnregisterMeshHeightStamp;
            static constexpr auto Enumerate = &TerrainCompositionRequests::GetRegisteredMeshHeightStamps;
            static AZ::EntityId& Entity(Record& record) { return record.m_stampEntityId; }
            static auto& Snapshot(Record& record) { return record.m_mesh; }
            static auto Assets(Record& record) { return AZStd::array{ &record.m_configuration.m_terrainMeshAsset }; }
            static auto Snapshots(Record& record) { return AZStd::array{ &record.m_mesh }; }
        };

        // Unsupported products exercise real cache notifications without loading files or preparing geometry.
        class Catalog : public AZ::Data::AssetCatalogRequestBus::Handler
        {
        public:
            Catalog() { BusConnect(); }
            ~Catalog() override { BusDisconnect(); }
            AZ::Data::AssetInfo GetAssetInfoById(const AZ::Data::AssetId& id) override
            {
                AZ::Data::AssetInfo info;
                if (!m_removed.contains(id))
                {
                    info.m_assetId = id;
                    info.m_assetType = AZ::Uuid::CreateNull();
                }
                return info;
            }
            void QueueRemoval(const AZ::Data::AssetId& id)
            {
                m_removed.insert(id);
                AzFramework::AssetCatalogEventBus::Broadcast(
                    &AzFramework::AssetCatalogEvents::OnCatalogAssetRemoved, id, AZ::Data::AssetInfo{});
            }
            AZStd::unordered_set<AZ::Data::AssetId> m_removed;
        };

        class ContextOwner : public AzFramework::EntityIdContextQueryBus::MultiHandler
        {
        public:
            ~ContextOwner() override { BusDisconnect(); }
            AzFramework::EntityContextId GetOwningContextId() override { return m_context; }
            AzFramework::EntityContextId m_context = AZ::Uuid::CreateRandom();
        };
    }

    template<class Kind>
    class TerrainRegistrationLifecycleTests : public ::testing::Test
    {
    protected:
        using Record = typename Kind::Record;

        void SetUp() override
        {
            for (const auto id : { m_owner, m_otherOwner, m_stamp, m_peer })
                m_context.BusConnect(id);
            m_address = { m_context.m_context, m_owner };
            StartComposition();
        }

        void TearDown() override
        {
            StopComposition();
            AZ::TickBus::ExecuteQueuedEvents();
            AZ::SystemTickBus::ExecuteQueuedEvents();
        }

        void StartComposition()
        {
            m_composition = AZStd::make_unique<TerrainCompositionGradientComponent>(m_configuration);
            m_composition->EditorActivate(m_owner);
        }

        void StopComposition()
        {
            if (m_composition)
            {
                m_composition->EditorDeactivate(m_owner);
                m_composition.reset();
            }
        }

        AZ::Uuid Session() const
        {
            AZ::Uuid session;
            TerrainCompositionRequestBus::EventResult(session, m_address, &TerrainCompositionRequests::GetCompositionSession);
            return session;
        }

        Record MakeRecord(AZ::EntityId entity) const
        {
            Record record;
            Kind::Entity(record) = entity;
            record.m_contextId = m_context.m_context;
            record.m_compositionSession = Session();
            record.m_registrationId = AZ::Uuid::CreateRandom();
            record.m_updateRevision = 1;
            record.m_configuration.m_targetCompositionEntityId = m_owner;
            record.m_configuration.AssignNewPersistentOrderingIdentity();
            return record;
        }

        bool Register(const Record& record)
        {
            bool accepted = false;
            TerrainCompositionRequestBus::EventResult(accepted, m_address, Kind::Register, record);
            return accepted;
        }

        void Unregister(Record record)
        {
            TerrainCompositionRequestBus::Event(
                m_address, Kind::Unregister, Kind::Entity(record), record.m_registrationId, record.m_compositionSession);
        }

        AZStd::vector<Record> Records() const { return RecordsAt(m_address); }
        auto& Diagnostics() { return m_composition->m_pendingDiagnostics; }
        auto& FootprintChanges() { return m_composition->m_pendingChanges; }
        void Republish() { m_composition->PublishStamps(); }
        AZ::u64 PublishedRevision() const { return m_composition->GetQueryState()->m_revision; }
        auto& DirtyStamps() { return m_composition->m_dirtyStamps; }
        auto QueryState() const { return m_composition->GetQueryState(); }
        auto Prepare(float gridSpacing = 0.0f, const AZStd::unordered_set<AZ::EntityId>& nonUniformScale = {}) const
        {
            return Internal::PrepareComposition(m_composition->m_registrations, m_configuration,
                AZ::Aabb::CreateFromMinMaxValues(-100, -100, 0, 100, 100, 100), Session(), gridSpacing, nonUniformScale);
        }

        static AZStd::vector<Record> RecordsAt(const TerrainCompositionAddress& address)
        {
            AZStd::vector<Record> records;
            TerrainCompositionRequestBus::EventResult(records, address, Kind::Enumerate);
            return records;
        }

        const AZ::EntityId m_owner{ 71001 }, m_otherOwner{ 71002 }, m_stamp{ 71003 }, m_peer{ 71004 };
        AzFramework::SceneSystemComponent m_sceneSystem;
        RegistrationTestSupport::ContextOwner m_context;
        TerrainCompositionAddress m_address;
        TerrainCompositionConfig m_configuration;
        AZStd::unique_ptr<TerrainCompositionGradientComponent> m_composition;
    };

    using RegistrationKinds = ::testing::Types<
        RegistrationTestSupport::Image, RegistrationTestSupport::Cutout, RegistrationTestSupport::MeshHeight>;
    TYPED_TEST_SUITE(TerrainRegistrationLifecycleTests, RegistrationKinds);

    TYPED_TEST(TerrainRegistrationLifecycleTests, RejectsWrongContextTargetSessionAndInvalidLease)
    {
        const auto valid = this->MakeRecord(this->m_stamp);
        const auto reject = [this, &valid](const char* reason, auto mutate)
        {
            SCOPED_TRACE(reason);
            auto invalid = valid;
            mutate(invalid);
            EXPECT_FALSE(this->Register(invalid));
            EXPECT_TRUE(this->Records().empty());
        };
        reject("wrong context", [](auto& r) { r.m_contextId = AZ::Uuid::CreateRandom(); });
        reject("wrong target", [this](auto& r) { r.m_configuration.m_targetCompositionEntityId = this->m_otherOwner; });
        reject("old session", [](auto& r) { r.m_compositionSession = AZ::Uuid::CreateRandom(); });
        reject("invalid lease", [](auto& r) { r.m_registrationId = {}; });
        reject("invalid revision", [](auto& r) { r.m_updateRevision = 0; });
        reject("unowned entity", [](auto& r) { TypeParam::Entity(r) = AZ::EntityId(71999); });
        EXPECT_TRUE(this->Register(valid));
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, ReplayIsIdempotentAndOlderRevisionsCannotOverwriteNewerData)
    {
        auto record = this->MakeRecord(this->m_stamp);
        record.m_updateRevision = 4;
        record.m_configuration.m_priority = 12;
        ASSERT_TRUE(this->Register(record));
        auto replay = record;
        replay.m_configuration.m_priority = 99;
        EXPECT_TRUE(this->Register(replay));
        ASSERT_EQ(this->Records().size(), 1);
        EXPECT_EQ(this->Records()[0].m_configuration.m_priority, 12);
        replay.m_updateRevision = 3;
        EXPECT_FALSE(this->Register(replay));
        replay.m_updateRevision = 5;
        EXPECT_TRUE(this->Register(replay));
        EXPECT_EQ(this->Records()[0].m_configuration.m_priority, 99);
        EXPECT_EQ(this->Records()[0].m_updateRevision, 5);
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, RetiredLeaseCannotResurrectAndWrongLeaseCannotRemoveItsReplacement)
    {
        auto record = this->MakeRecord(this->m_stamp);
        ASSERT_TRUE(this->Register(record));
        auto wrong = record;
        wrong.m_compositionSession = AZ::Uuid::CreateRandom();
        this->Unregister(wrong);
        ASSERT_EQ(this->Records().size(), 1);
        auto competing = record;
        competing.m_registrationId = AZ::Uuid::CreateRandom();
        EXPECT_FALSE(this->Register(competing));
        this->Unregister(record);
        EXPECT_TRUE(this->Records().empty());
        record.m_updateRevision += 100;
        EXPECT_FALSE(this->Register(record));
        ASSERT_TRUE(this->Register(competing));
        this->Unregister(record);
        ASSERT_EQ(this->Records().size(), 1);
        EXPECT_EQ(this->Records()[0].m_registrationId, competing.m_registrationId);
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, UnregisterBeforeDelayedFirstRegisterStillRetiresTheLease)
    {
        const auto record = this->MakeRecord(this->m_stamp);
        this->Unregister(record);
        EXPECT_FALSE(this->Register(record));
        EXPECT_TRUE(this->Records().empty());
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, CompositionRestartRejectsOldSessionMessages)
    {
        const auto old = this->MakeRecord(this->m_stamp);
        ASSERT_TRUE(this->Register(old));
        this->StopComposition();
        this->StartComposition();
        EXPECT_NE(this->Session(), old.m_compositionSession);
        EXPECT_FALSE(this->Register(old));
        const auto current = this->MakeRecord(this->m_stamp);
        ASSERT_TRUE(this->Register(current));
        this->Unregister(old);
        ASSERT_EQ(this->Records().size(), 1);
        EXPECT_EQ(this->Records()[0].m_registrationId, current.m_registrationId);
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, SharedAssetRevisionPropagatesAndStalePlacementUpdatesCannotRegressIt)
    {
        auto first = this->MakeRecord(this->m_stamp);
        auto second = this->MakeRecord(this->m_peer);
        const AZ::Data::AssetId assetId(AZ::Uuid::CreateRandom(), 1);
        TypeParam::Snapshot(first).m_assetId = assetId;
        TypeParam::Snapshot(first).m_revision = 10;
        TypeParam::Snapshot(second).m_assetId = assetId;
        TypeParam::Snapshot(second).m_revision = 20;
        using Status = decltype(TypeParam::Snapshot(first).m_status);
        TypeParam::Snapshot(second).m_status = Status::Error;
        ASSERT_TRUE(this->Register(first));
        ASSERT_TRUE(this->Register(second));
        ++first.m_updateRevision;
        first.m_worldTransform.SetTranslation(AZ::Vector3(5.0f));
        ASSERT_TRUE(this->Register(first));
        for (auto record : this->Records())
        {
            EXPECT_EQ(TypeParam::Snapshot(record).m_revision, 20);
            EXPECT_EQ(TypeParam::Snapshot(record).m_status, Status::Error);
        }
        ++second.m_updateRevision;
        TypeParam::Snapshot(second).m_revision = 21;
        TypeParam::Snapshot(second).m_status = Status::Missing;
        ASSERT_TRUE(this->Register(second));
        ASSERT_EQ(this->Records().size(), 2);
        for (auto record : this->Records())
        {
            EXPECT_EQ(TypeParam::Snapshot(record).m_revision, 21);
            EXPECT_EQ(TypeParam::Snapshot(record).m_status, Status::Missing);
        }
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, EqualAssetRevisionsKeepEachClaimsSnapshot)
    {
        auto first = this->MakeRecord(this->m_stamp);
        auto second = this->MakeRecord(this->m_peer);
        using Status = decltype(TypeParam::Snapshot(first).m_status);
        const AZ::Data::AssetId asset(AZ::Uuid::CreateRandom(), 1);
        TypeParam::Snapshot(first).m_assetId = TypeParam::Snapshot(second).m_assetId = asset;
        TypeParam::Snapshot(first).m_revision = TypeParam::Snapshot(second).m_revision = 10;
        TypeParam::Snapshot(first).m_status = Status::Loading;
        TypeParam::Snapshot(second).m_status = Status::Error;
        ASSERT_TRUE(this->Register(first));
        ASSERT_TRUE(this->Register(second));
        for (auto record : this->Records())
        {
            EXPECT_EQ(TypeParam::Snapshot(record).m_revision, 10);
            EXPECT_EQ(TypeParam::Snapshot(record).m_status,
                TypeParam::Entity(record) == this->m_stamp ? Status::Loading : Status::Error);
        }
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, UnassignedRevisionReconciliationPreservesRolePolicyWithoutFanout)
    {
        auto first = this->MakeRecord(this->m_stamp);
        auto second = this->MakeRecord(this->m_peer);
        TypeParam::Snapshot(first).m_revision = 10;
        TypeParam::Snapshot(second).m_revision = 1;
        ASSERT_TRUE(this->Register(first));
        ASSERT_TRUE(this->Register(second));
        for (auto record : this->Records())
        {
            const bool adoptsUnassigned = !std::is_same_v<TypeParam, RegistrationTestSupport::Image>;
            EXPECT_EQ(TypeParam::Snapshot(record).m_revision,
                TypeParam::Entity(record) == this->m_stamp || adoptsUnassigned ? 10 : 1);
        }
        ++second.m_updateRevision;
        TypeParam::Snapshot(second).m_revision = 20;
        ASSERT_TRUE(this->Register(second));
        for (auto record : this->Records())
        {
            EXPECT_EQ(TypeParam::Snapshot(record).m_revision, TypeParam::Entity(record) == this->m_stamp ? 10 : 20);
        }
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, RemovingLastAssetClaimDropsItsRevisionHistory)
    {
        auto first = this->MakeRecord(this->m_stamp);
        auto second = this->MakeRecord(this->m_peer);
        const AZ::Data::AssetId asset(AZ::Uuid::CreateRandom(), 1);
        TypeParam::Snapshot(first).m_assetId = TypeParam::Snapshot(second).m_assetId = asset;
        TypeParam::Snapshot(first).m_revision = 20;
        TypeParam::Snapshot(second).m_revision = 1;
        ASSERT_TRUE(this->Register(first));
        this->Unregister(first);
        ASSERT_TRUE(this->Register(second));
        auto records = this->Records();
        ASSERT_EQ(records.size(), 1);
        EXPECT_EQ(TypeParam::Snapshot(records[0]).m_revision, 1);
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, WaitingClientReplaysAfterCreationAndRestartButIgnoresOldNotifications)
    {
        auto config = this->MakeRecord(this->m_stamp).m_configuration;
        this->StopComposition();
        typename TypeParam::Lease lease;
        lease.Activate(this->m_stamp, config, AZ::Transform::CreateIdentity());
        EXPECT_FALSE(lease.IsRegistered());
        this->StartComposition();
        ASSERT_TRUE(lease.IsRegistered());
        ASSERT_EQ(this->Records().size(), 1);
        const auto previous = this->Records()[0];
        this->StopComposition();
        EXPECT_FALSE(lease.IsRegistered());
        this->StartComposition();
        ASSERT_TRUE(lease.IsRegistered());
        ASSERT_EQ(this->Records().size(), 1);
        const auto current = this->Records()[0];
        EXPECT_NE(current.m_compositionSession, previous.m_compositionSession);
        EXPECT_GT(current.m_updateRevision, previous.m_updateRevision);
        TerrainCompositionNotificationBus::Event(this->m_address,
            &TerrainCompositionNotifications::OnCompositionUnavailable, previous.m_compositionSession);
        TerrainCompositionNotificationBus::Event(this->m_address,
            &TerrainCompositionNotifications::OnCompositionAvailable, previous.m_compositionSession);
        EXPECT_TRUE(lease.IsRegistered());
        EXPECT_EQ(this->Records()[0].m_updateRevision, current.m_updateRevision);
        lease.Deactivate();
        EXPECT_TRUE(this->Records().empty());
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, RetargetingRetiresOldLeaseAndDeactivationPreventsFurtherReplay)
    {
        auto config = this->MakeRecord(this->m_stamp).m_configuration;
        typename TypeParam::Lease lease;
        lease.Activate(this->m_stamp, config, AZ::Transform::CreateIdentity());
        ASSERT_TRUE(lease.IsRegistered());
        const auto old = this->Records()[0];
        TerrainCompositionGradientComponent other;
        other.EditorActivate(this->m_otherOwner);
        const TerrainCompositionAddress address{ this->m_context.m_context, this->m_otherOwner };
        config.m_targetCompositionEntityId = this->m_otherOwner;
        lease.Update(config, AZ::Transform::CreateIdentity());
        EXPECT_TRUE(this->Records().empty());
        auto records = this->RecordsAt(address);
        EXPECT_EQ(records.size(), 1);
        if (!records.empty())
            EXPECT_NE(records[0].m_registrationId, old.m_registrationId);
        EXPECT_FALSE(this->Register(old));
        lease.Deactivate();
        TerrainCompositionNotificationBus::Event(address,
            &TerrainCompositionNotifications::OnCompositionAvailable, AZ::Uuid{});
        EXPECT_TRUE(this->RecordsAt(address).empty());
        EXPECT_FALSE(lease.IsRegistered());
        other.EditorDeactivate(this->m_otherOwner);
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, LateContextOwnershipRetriesTheLatestConfiguration)
    {
        auto config = this->MakeRecord(this->m_stamp).m_configuration;
        this->m_context.BusDisconnect(this->m_stamp);
        typename TypeParam::Lease lease;
        lease.Activate(this->m_stamp, config, AZ::Transform::CreateIdentity());
        EXPECT_FALSE(lease.IsRegistered());
        config.m_priority = 37;
        const auto placement = AZ::Transform::CreateTranslation(AZ::Vector3(3.0f, 4.0f, 5.0f));
        lease.Update(config, placement, false, true);
        this->m_context.BusConnect(this->m_stamp);
        AZ::SystemTickBus::Broadcast(&AZ::SystemTickEvents::OnSystemTick);
        ASSERT_TRUE(lease.IsRegistered());
        const auto records = this->Records();
        ASSERT_EQ(records.size(), 1);
        EXPECT_EQ(records[0].m_configuration.m_priority, 37);
        EXPECT_TRUE(records[0].m_worldTransform.IsClose(placement));
        EXPECT_FALSE(records[0].m_transformAvailable);
        EXPECT_TRUE(records[0].m_identityPending);
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, PlacementUpdatesKeepTheLeaseAndReactivationRetiresIt)
    {
        auto config = this->MakeRecord(this->m_stamp).m_configuration;
        typename TypeParam::Lease lease;
        lease.Activate(this->m_stamp, config, AZ::Transform::CreateIdentity());
        ASSERT_TRUE(lease.IsRegistered());
        const auto first = this->Records()[0];
        config.m_priority = 9;
        lease.Update(config, AZ::Transform::CreateTranslation(AZ::Vector3(8.0f)), false, true);
        auto records = this->Records();
        ASSERT_EQ(records.size(), 1);
        EXPECT_EQ(records[0].m_registrationId, first.m_registrationId);
        EXPECT_GT(records[0].m_updateRevision, first.m_updateRevision);
        EXPECT_EQ(records[0].m_configuration.m_priority, 9);
        lease.Activate(this->m_stamp, config, AZ::Transform::CreateIdentity());
        records = this->Records();
        ASSERT_EQ(records.size(), 1);
        EXPECT_NE(records[0].m_registrationId, first.m_registrationId);
        EXPECT_FALSE(this->Register(first));
        lease.Deactivate();
        lease.Deactivate();
        EXPECT_TRUE(this->Records().empty());
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, ClearingTargetRetiresTheLeaseAndDestructionStopsTickReplay)
    {
        auto config = this->MakeRecord(this->m_stamp).m_configuration;
        {
            typename TypeParam::Lease lease;
            lease.Activate(this->m_stamp, config, AZ::Transform::CreateIdentity());
            ASSERT_TRUE(lease.IsRegistered());
            const auto old = this->Records()[0];
            config.m_targetCompositionEntityId = AZ::EntityId{};
            lease.Update(config, AZ::Transform::CreateIdentity());
            EXPECT_FALSE(lease.IsRegistered());
            EXPECT_TRUE(this->Records().empty());
            config.m_targetCompositionEntityId = this->m_owner;
            lease.Update(config, AZ::Transform::CreateIdentity());
            ASSERT_TRUE(lease.IsRegistered());
            EXPECT_NE(this->Records()[0].m_registrationId, old.m_registrationId);
            EXPECT_FALSE(this->Register(old));
        }
        AZ::SystemTickBus::Broadcast(&AZ::SystemTickEvents::OnSystemTick);
        this->StopComposition();
        this->StartComposition();
        EXPECT_TRUE(this->Records().empty());
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, EveryAssetRoleResubscribesAfterCacheRestartAndIgnoresRetiredCallbacks)
    {
        using Cache = typename TypeParam::Cache;
        RegistrationTestSupport::Catalog catalog;
        auto cache = AZStd::make_unique<Cache>();
        auto input = this->MakeRecord(this->m_stamp);
        for (auto* asset : TypeParam::Assets(input))
        {
            *asset = { AZ::Data::AssetId(AZ::Uuid::CreateRandom(), 1), asset->GetType(), {} };
            asset->SetAutoLoadBehavior(AZ::Data::AssetLoadBehavior::NoLoad);
        }
        typename TypeParam::Lease lease;
        lease.Activate(this->m_stamp, input.m_configuration, AZ::Transform::CreateIdentity());
        ASSERT_TRUE(lease.IsRegistered());
        const auto originalLease = this->Records()[0].m_registrationId;
        cache.reset();
        cache = AZStd::make_unique<Cache>();
        AZ::SystemTickBus::Broadcast(&AZ::SystemTickEvents::OnSystemTick);
        auto record = this->Records()[0];
        EXPECT_EQ(record.m_registrationId, originalLease);
        for (const auto* snapshot : TypeParam::Snapshots(record))
            EXPECT_EQ(snapshot->m_status, decltype(snapshot->m_status)::Unsupported);

        const auto assets = TypeParam::Assets(input);
        for (size_t role = 0; role < assets.size(); ++role)
        {
            SCOPED_TRACE(role);
            const auto before = this->Records()[0].m_updateRevision;
            catalog.QueueRemoval(assets[role]->GetId());
            AZ::SystemTickBus::ExecuteQueuedEvents();
            record = this->Records()[0];
            EXPECT_EQ(record.m_updateRevision, before + 1);
            const auto snapshots = TypeParam::Snapshots(record);
            for (size_t other = 0; other < snapshots.size(); ++other)
            {
                using Status = decltype(snapshots[other]->m_status);
                EXPECT_EQ(snapshots[other]->m_status, other <= role ? Status::Missing : Status::Unsupported);
            }
        }

        // Retain old sources so queued work really executes after the client switches to fresh assets.
        AZStd::vector<typename Cache::Handle> retired;
        for (auto* asset : TypeParam::Assets(input))
        {
            retired.push_back(cache->Acquire(asset->GetId()));
            catalog.m_removed.erase(asset->GetId());
            AzFramework::AssetCatalogEventBus::Broadcast(
                &AzFramework::AssetCatalogEvents::OnCatalogAssetAdded, asset->GetId());
            *asset = { AZ::Data::AssetId(AZ::Uuid::CreateRandom(), 1), asset->GetType(), {} };
            asset->SetAutoLoadBehavior(AZ::Data::AssetLoadBehavior::NoLoad);
        }
        lease.Update(input.m_configuration, AZ::Transform::CreateIdentity());
        const auto revision = this->Records()[0].m_updateRevision;
        AZ::SystemTickBus::ExecuteQueuedEvents();
        record = this->Records()[0];
        EXPECT_EQ(record.m_updateRevision, revision);
        const auto currentAssets = TypeParam::Assets(input);
        const auto currentSnapshots = TypeParam::Snapshots(record);
        for (size_t role = 0; role < currentAssets.size(); ++role)
        {
            EXPECT_EQ(currentSnapshots[role]->m_assetId, currentAssets[role]->GetId());
            catalog.QueueRemoval(currentAssets[role]->GetId());
            retired.push_back(cache->Acquire(currentAssets[role]->GetId()));
        }
        lease.Deactivate();
        AZ::SystemTickBus::ExecuteQueuedEvents();
        EXPECT_TRUE(this->Records().empty());
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, PublicationDiagnosticsRememberRecoveryAndRetireWithTheClaim)
    {
        auto record = this->MakeRecord(this->m_stamp);
        record.m_configuration.m_orderingId = AZ::Uuid{};
        record.m_configuration.m_stableOrderKey.clear();
        this->Diagnostics().clear();
        ASSERT_TRUE(this->Register(record));
        const auto initial = this->Diagnostics();
        ASSERT_EQ(initial.size(), (std::is_same_v<TypeParam, RegistrationTestSupport::Image> ? 3 : 1));
        this->Diagnostics().clear();
        this->Republish();
        EXPECT_TRUE(this->Diagnostics().empty());
        record.m_configuration.AssignNewPersistentOrderingIdentity();
        using Status = decltype(TypeParam::Snapshot(record).m_status);
        TypeParam::Snapshot(record).m_status = Status::Loading;
        ++record.m_updateRevision;
        ASSERT_TRUE(this->Register(record));
        EXPECT_TRUE(this->Diagnostics().empty());
        record.m_configuration.m_orderingId = AZ::Uuid{};
        record.m_configuration.m_stableOrderKey.clear();
        ++record.m_updateRevision;
        ASSERT_TRUE(this->Register(record));
        EXPECT_EQ(this->Diagnostics(), initial);
        this->Diagnostics().clear();
        this->Unregister(record);
        record.m_registrationId = AZ::Uuid::CreateRandom();
        ASSERT_TRUE(this->Register(record));
        EXPECT_EQ(this->Diagnostics(), initial);
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, EveryRemovalRoleRetiresUnknownLeasesAcrossRegistrationTypes)
    {
        for (const auto remove : { RegistrationTestSupport::Image::Unregister,
                 RegistrationTestSupport::Cutout::Unregister, RegistrationTestSupport::MeshHeight::Unregister })
        {
            auto record = this->MakeRecord(this->m_stamp);
            const auto revision = this->PublishedRevision();
            TerrainCompositionRequestBus::Event(
                this->m_address, remove, AZ::EntityId{}, record.m_registrationId, record.m_compositionSession);
            EXPECT_EQ(this->PublishedRevision(), revision);
            EXPECT_FALSE(this->Register(record));
            record.m_registrationId = AZ::Uuid::CreateRandom();
            ASSERT_TRUE(this->Register(record));
            this->Unregister(record);
        }
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, WrongSessionAndNullLeaseRemovalDoNotRetireOrPublish)
    {
        for (const auto remove : { RegistrationTestSupport::Image::Unregister,
                 RegistrationTestSupport::Cutout::Unregister, RegistrationTestSupport::MeshHeight::Unregister })
        {
            auto record = this->MakeRecord(this->m_stamp);
            const auto revision = this->PublishedRevision();
            TerrainCompositionRequestBus::Event(
                this->m_address, remove, this->m_stamp, record.m_registrationId, AZ::Uuid::CreateRandom());
            TerrainCompositionRequestBus::Event(
                this->m_address, remove, this->m_stamp, AZ::Uuid{}, record.m_compositionSession);
            EXPECT_EQ(this->PublishedRevision(), revision);
            ASSERT_TRUE(this->Register(record));
            this->Unregister(record);
        }
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, ReplayAndRejectionLeavePendingDirtyStateUnpublished)
    {
        auto record = this->MakeRecord(this->m_stamp);
        record.m_updateRevision = 4;
        ASSERT_TRUE(this->Register(record));
        this->Diagnostics().clear();
        this->DirtyStamps()[this->m_peer] = Internal::DirtyAll;
        const auto revision = this->PublishedRevision();
        auto replay = record;
        replay.m_configuration.m_priority += 10;
        EXPECT_TRUE(this->Register(replay));
        --replay.m_updateRevision;
        EXPECT_FALSE(this->Register(replay));
        EXPECT_EQ(this->PublishedRevision(), revision);
        EXPECT_EQ(this->DirtyStamps().at(this->m_peer), Internal::DirtyAll);
        EXPECT_TRUE(this->Diagnostics().empty());
        ++record.m_updateRevision;
        ASSERT_TRUE(this->Register(record));
        EXPECT_GT(this->PublishedRevision(), revision);
        EXPECT_TRUE(this->DirtyStamps().empty());
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, StaleLeaseRemovalPreservesReplacementWarningHistory)
    {
        auto record = this->MakeRecord(this->m_stamp);
        record.m_configuration.m_orderingId = {};
        record.m_configuration.m_stableOrderKey.clear();
        this->Diagnostics().clear();
        ASSERT_TRUE(this->Register(record));
        const auto warnings = this->Diagnostics();
        ASSERT_FALSE(warnings.empty());
        this->Unregister(record);
        auto replacement = record;
        replacement.m_registrationId = AZ::Uuid::CreateRandom();
        ASSERT_TRUE(this->Register(replacement));
        this->Diagnostics().clear();
        const auto revision = this->PublishedRevision();
        this->Unregister(record);
        EXPECT_EQ(this->PublishedRevision(), revision);
        ASSERT_EQ(this->Records().size(), 1);
        EXPECT_EQ(this->Records()[0].m_registrationId, replacement.m_registrationId);
        this->Republish();
        EXPECT_TRUE(this->Diagnostics().empty());
        this->Unregister(replacement);
        replacement.m_registrationId = AZ::Uuid::CreateRandom();
        ASSERT_TRUE(this->Register(replacement));
        EXPECT_EQ(this->Diagnostics(), warnings);
    }

    TYPED_TEST(TerrainRegistrationLifecycleTests, SameObjectReactivationClearsLeaseAndDiagnosticHistory)
    {
        auto record = this->MakeRecord(this->m_stamp);
        record.m_configuration.m_orderingId = {};
        record.m_configuration.m_stableOrderKey.clear();
        this->Diagnostics().clear();
        ASSERT_TRUE(this->Register(record));
        const auto warnings = this->Diagnostics();
        ASSERT_FALSE(warnings.empty());
        auto retired = this->MakeRecord(this->m_peer);
        this->Unregister(retired);
        this->m_composition->EditorDeactivate(this->m_owner);
        this->m_composition->EditorActivate(this->m_owner);
        ASSERT_NE(this->Session(), record.m_compositionSession);
        EXPECT_TRUE(this->Records().empty());
        record.m_compositionSession = retired.m_compositionSession = this->Session();
        this->Diagnostics().clear();
        ASSERT_TRUE(this->Register(record));
        EXPECT_EQ(this->Diagnostics(), warnings);
        ASSERT_TRUE(this->Register(retired));
    }

    class TerrainImageRegistrationTests : public TerrainRegistrationLifecycleTests<RegistrationTestSupport::Image>
    {
    protected:
        void SetUp() override
        {
            m_source = AZStd::make_unique<TestSupport::ConstantGradient>(AZ::EntityId(71006), 0.25f);
            m_shape = AZStd::make_unique<::testing::NiceMock<UnitTest::MockShapeComponentRequests>>(AZ::EntityId(71005));
            ON_CALL(*m_shape, GetEncompassingAabb()).WillByDefault(::testing::Return(
                AZ::Aabb::CreateFromMinMaxValues(-100.0f, -100.0f, 0.0f, 100.0f, 100.0f, 100.0f)));
            m_configuration.m_proceduralSourceEntityId = AZ::EntityId(71006);
            m_configuration.m_targetTerrainRegionEntityId = AZ::EntityId(71005);
            TerrainRegistrationLifecycleTests::SetUp();
        }

        Record ContributingImage(AZ::EntityId entity, float value)
        {
            auto record = MakeRecord(entity);
            auto image = AZStd::make_shared<HeightmapData>(TestSupport::MakeImage(1, 1, { value }));
            image->m_assetId = { AZ::Uuid::CreateRandom(), 1 };
            image->m_revision = 1;
            record.m_heightmap = { HeightmapDataStatus::Ready, 1, image, image->m_assetId };
            record.m_configuration.m_featherWidth = 0.0f;
            return record;
        }

        float Sample() const
        {
            float value = -1.0f;
            GradientSignal::GradientRequestBus::EventResult(value, m_owner, &GradientSignal::GradientRequests::GetValue,
                GradientSignal::GradientSampleParams(AZ::Vector3::CreateZero()));
            return value;
        }

        AZStd::unique_ptr<TestSupport::ConstantGradient> m_source;
        AZStd::unique_ptr<::testing::NiceMock<UnitTest::MockShapeComponentRequests>> m_shape;
    };

    TEST_F(TerrainImageRegistrationTests, PublicationFootprintsRetainMovementRemovalAndRecoveryWithoutRepeatedRemoval)
    {
        auto record = ContributingImage(m_stamp, 0.8f);
        record.m_configuration.m_footprintWidth = record.m_configuration.m_footprintDepth = 10.0f;
        ASSERT_TRUE(Register(record));
        ASSERT_EQ(FootprintChanges().size(), 1);
        const auto first = FootprintChanges().back();
        EXPECT_FALSE(first.m_previousBounds.IsValid());
        EXPECT_TRUE(first.m_currentBounds.IsValid());
        FootprintChanges().clear();
        record.m_worldTransform.SetTranslation(AZ::Vector3(30.0f, 0.0f, 0.0f));
        ++record.m_updateRevision;
        ASSERT_TRUE(Register(record));
        ASSERT_EQ(FootprintChanges().size(), 1);
        const auto moved = FootprintChanges().back();
        EXPECT_EQ(moved.m_previousBounds, first.m_currentBounds);
        EXPECT_EQ(moved.m_previousRegionBounds, first.m_currentRegionBounds);
        EXPECT_EQ(moved.m_currentRegionEntityId, first.m_currentRegionEntityId);
        EXPECT_GT(moved.m_currentBounds.GetMin().GetX(), moved.m_previousBounds.GetMax().GetX());
        FootprintChanges().clear();
        record.m_transformAvailable = false;
        ++record.m_updateRevision;
        ASSERT_TRUE(Register(record));
        ASSERT_EQ(FootprintChanges().size(), 1);
        EXPECT_EQ(FootprintChanges().back().m_previousBounds, moved.m_currentBounds);
        EXPECT_FALSE(FootprintChanges().back().m_currentBounds.IsValid());
        FootprintChanges().clear();
        ++record.m_configuration.m_priority;
        ++record.m_updateRevision;
        ASSERT_TRUE(Register(record));
        EXPECT_TRUE(FootprintChanges().empty());
        record.m_transformAvailable = true;
        ++record.m_updateRevision;
        ASSERT_TRUE(Register(record));
        ASSERT_EQ(FootprintChanges().size(), 1);
        EXPECT_FALSE(FootprintChanges().back().m_previousBounds.IsValid());
        EXPECT_EQ(FootprintChanges().back().m_currentBounds, moved.m_currentBounds);
    }

    TEST_F(TerrainImageRegistrationTests, OrderingCollisionSuppressesEveryClaimantAndRemovalRecoversTheSurvivor)
    {
        const auto first = ContributingImage(m_stamp, 0.8f);
        auto second = ContributingImage(m_peer, 0.6f);
        second.m_configuration.m_orderingId = first.m_configuration.m_orderingId;
        second.m_configuration.m_stableOrderKey = first.m_configuration.m_stableOrderKey;
        ASSERT_TRUE(Register(first));
        EXPECT_FLOAT_EQ(Sample(), 0.8f);
        ASSERT_TRUE(Register(second));
        EXPECT_FLOAT_EQ(Sample(), 0.25f);
        Unregister(second);
        EXPECT_FLOAT_EQ(Sample(), 0.8f);
    }

    TEST_F(TerrainImageRegistrationTests, RejectedRenderPublicationRetainsQueryAndDirtyWorkUntilRetry)
    {
        ASSERT_EQ(AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get(), nullptr);
        TerrainMeshCutoutRenderRegistry registry;
        auto record = ContributingImage(m_stamp, 0.8f);
        ASSERT_TRUE(Register(record));
        const auto retained = QueryState();
        FootprintChanges().clear();
        Diagnostics().clear();
        ASSERT_TRUE(registry.Publish(nullptr, Session(), {}, {}, retained->m_revision + 100));
        record.m_worldTransform.SetTranslation(AZ::Vector3(30.0f, 0.0f, 0.0f));
        ++record.m_updateRevision;
        ASSERT_TRUE(Register(record));
        EXPECT_EQ(QueryState(), retained);
        EXPECT_TRUE(FootprintChanges().empty());
        EXPECT_TRUE(DirtyStamps().contains(m_stamp));
        ASSERT_EQ(Diagnostics().size(), 1);
        EXPECT_NE(Diagnostics()[0].find("render publication rejected stale generation"), AZStd::string::npos);
        registry.Remove(Session());
        Republish();
        EXPECT_NE(QueryState(), retained);
        EXPECT_TRUE(DirtyStamps().empty());
        ASSERT_EQ(FootprintChanges().size(), 1);
        EXPECT_EQ(FootprintChanges()[0].m_previousBounds, retained->m_heightContributors[0].GetWorldBounds());
        EXPECT_NE(FootprintChanges()[0].m_currentBounds, FootprintChanges()[0].m_previousBounds);
    }

    TEST_F(TerrainImageRegistrationTests, ReconstructionIsReusedAndRetainedAcrossConfigurationChanges)
    {
        auto record = ContributingImage(m_stamp, 0.8f);
        record.m_configuration.m_samplingMode = HeightmapSamplingMode::SmoothCubic;
        record.m_configuration.m_reconstructionRadius = 2.0f;
        ASSERT_TRUE(Register(record));
        const auto retained = QueryState();
        const auto reconstruction = retained->m_heightContributors[0].m_image.m_reconstruction;
        ASSERT_TRUE(reconstruction);
        Republish();
        EXPECT_EQ(QueryState()->m_heightContributors[0].m_image.m_reconstruction, reconstruction);
        record.m_configuration.m_samplingMode = HeightmapSamplingMode::Bilinear;
        ++record.m_updateRevision;
        ASSERT_TRUE(Register(record));
        EXPECT_FALSE(QueryState()->m_heightContributors[0].m_image.m_reconstruction);
        EXPECT_EQ(retained->m_heightContributors[0].m_image.m_reconstruction, reconstruction);
        EXPECT_FLOAT_EQ(reconstruction->m_samples[0], 0.8f);
    }

    TEST_F(TerrainImageRegistrationTests, InvalidMeshClaimSuppressesImageAndCollisionWarningRecoversAfterRemoval)
    {
        const auto image = ContributingImage(m_stamp, 0.8f);
        ASSERT_TRUE(Register(image));
        TerrainMeshCutoutRegistrationData cutout;
        cutout.m_cutoutEntityId = m_peer;
        cutout.m_contextId = m_address.first;
        cutout.m_compositionSession = Session();
        cutout.m_registrationId = AZ::Uuid::CreateRandom();
        cutout.m_updateRevision = 1;
        cutout.m_configuration.m_targetCompositionEntityId = m_owner;
        cutout.m_configuration.m_orderingId = image.m_configuration.m_orderingId;
        const auto publish = [&]
        {
            bool accepted = false;
            TerrainCompositionRequestBus::EventResult(accepted, m_address, &TerrainCompositionRequests::RegisterMeshCutout, cutout);
            EXPECT_TRUE(accepted);
        };
        Diagnostics().clear();
        publish();
        EXPECT_FLOAT_EQ(Sample(), 0.25f);
        const auto warnings = Diagnostics();
        ASSERT_EQ(warnings.size(), 2);
        EXPECT_NE(warnings[0].find("ALL claimants suppressed"), AZStd::string::npos);
        Diagnostics().clear();
        Republish();
        EXPECT_TRUE(Diagnostics().empty());
        TerrainCompositionRequestBus::Event(m_address, &TerrainCompositionRequests::UnregisterMeshCutout,
            m_peer, cutout.m_registrationId, Session());
        EXPECT_FLOAT_EQ(Sample(), 0.8f);
        cutout.m_registrationId = AZ::Uuid::CreateRandom();
        publish();
        EXPECT_EQ(Diagnostics(), warnings);
    }

    TEST_F(TerrainImageRegistrationTests, PreparationUsesCapturedScaleAndLeavesPublicationAndDiagnosticsUntouched)
    {
        auto record = ContributingImage(m_stamp, 0.8f);
        auto surface = AZStd::make_shared<HeightmapData>(TestSupport::MakeImage(1, 1, { 0.0f }));
        surface->m_rawSamples = { 0 };
        surface->m_assetId = { AZ::Uuid::CreateRandom(), 1 };
        record.m_surfaceIdA = { HeightmapDataStatus::Ready, 1, surface, surface->m_assetId };
        record.m_configuration.m_surfaceMaps.m_surfaceIdAAsset = AZ::Data::Asset<AZ::RPI::StreamingImageAsset>(
            surface->m_assetId, azrtti_typeid<AZ::RPI::StreamingImageAsset>());
        record.m_holeMask = record.m_heightmap;
        record.m_configuration.m_holeMask.m_maskAsset = AZ::Data::Asset<AZ::RPI::StreamingImageAsset>(
            record.m_heightmap.m_assetId, azrtti_typeid<AZ::RPI::StreamingImageAsset>());
        record.m_configuration.m_samplingMode = HeightmapSamplingMode::SmoothCubic;
        record.m_configuration.m_reconstructionRadius = 2.0f;
        ASSERT_TRUE(Register(record));
        const auto published = QueryState();
        const auto diagnostics = Diagnostics();
        const auto dirty = DirtyStamps();
        const auto ready = Prepare();
        ASSERT_EQ(ready.m_query.m_heightContributors.size(), 1);
        EXPECT_EQ(ready.m_query.m_surfaceStamps.size(), 1);
        EXPECT_EQ(ready.m_query.m_existenceContributors.size(), 1);
        EXPECT_FALSE(ready.m_query.m_heightContributors[0].m_image.m_reconstruction);
        const auto invalid = Prepare(0.0f, { m_stamp });
        EXPECT_TRUE(invalid.m_query.m_heightContributors.empty());
        EXPECT_TRUE(invalid.m_query.m_surfaceStamps.empty());
        EXPECT_TRUE(invalid.m_query.m_existenceContributors.empty());
        ASSERT_EQ(invalid.m_diagnostics.size(), 3);
        for (const auto& diagnostic : invalid.m_diagnostics)
            EXPECT_EQ(diagnostic.m_reason, GetHeightmapStampValidationMessage(HeightmapStampValidation::NonUniformScale));
        EXPECT_EQ(QueryState(), published);
        EXPECT_EQ(Diagnostics(), diagnostics);
        EXPECT_EQ(DirtyStamps(), dirty);
        EXPECT_EQ(Prepare().m_footprints.m_height, ready.m_footprints.m_height);
        EXPECT_FLOAT_EQ(record.m_heightmap.m_data->m_samples[0], 0.8f);
        record.m_configuration.m_strength = 0.0f;
        ++record.m_updateRevision;
        ASSERT_TRUE(Register(record));
        const auto zeroStrength = Prepare();
        EXPECT_TRUE(zeroStrength.m_query.m_heightContributors.empty());
        EXPECT_TRUE(zeroStrength.m_query.m_surfaceStamps.empty());
        EXPECT_EQ(zeroStrength.m_query.m_existenceContributors.size(), 1);
    }

    using TerrainCutoutPreparationTests = TerrainRegistrationLifecycleTests<RegistrationTestSupport::Cutout>;

    TEST_F(TerrainCutoutPreparationTests, PreparationKeepsRenderMarginsSeparateFromPaddedCollisionCoverage)
    {
        auto record = MakeRecord(m_stamp);
        const AZStd::vector<AZ::Vector3> vertices{ { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
            { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } };
        const AZStd::vector<AZ::u32> indices{ 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
            3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5 };
        auto data = AZStd::make_shared<TerrainMeshCutoutData>();
        ASSERT_EQ(BuildTerrainMeshCutoutData(vertices, indices, *data), TerrainMeshCutoutValidation::Valid);
        record.m_mesh.m_status = TerrainMeshCutoutDataStatus::Ready;
        record.m_mesh.m_data = data;
        record.m_configuration.m_renderMargin = 0.1f;
        record.m_configuration.m_collisionMargin = 0.2f;
        ASSERT_TRUE(Register(record));
        const auto prepared = Prepare(2.0f);
        const auto unpadded = Prepare();
        ASSERT_EQ(prepared.m_renderCutouts.size(), 1);
        ASSERT_EQ(prepared.m_query.m_existenceContributors.size(), 1);
        EXPECT_EQ(prepared.m_renderCutouts[0].m_collisionWorldBounds, unpadded.m_renderCutouts[0].m_collisionWorldBounds);
        EXPECT_NE(prepared.m_footprints.m_cutouts.at(m_stamp), unpadded.m_footprints.m_cutouts.at(m_stamp));
        EXPECT_EQ(prepared.m_footprints.m_existence.at(m_stamp), prepared.m_footprints.m_cutouts.at(m_stamp));
        EXPECT_TRUE(prepared.m_query.m_heightContributors.empty());
        EXPECT_EQ(data->m_localBounds, AZ::Aabb::CreateFromMinMaxValues(-1, -1, -1, 1, 1, 1));
    }

    using TerrainMeshPreparationTests = TerrainRegistrationLifecycleTests<RegistrationTestSupport::MeshHeight>;

    TEST_F(TerrainMeshPreparationTests, ZeroStrengthGapsKeepRoleCoverageAndMissingRegistrySuppressesOnlyCoupledGaps)
    {
        ASSERT_EQ(AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get(), nullptr);
        auto record = MakeRecord(m_stamp);
        AZStd::vector<AZ::Vector3> vertices;
        AZStd::vector<AZ::u32> indices;
        for (AZ::u32 y = 0; y < 4; ++y)
            for (AZ::u32 x = 0; x < 4; ++x)
                vertices.emplace_back(float(x), float(y), 2.0f);
        for (AZ::u32 y = 0; y < 3; ++y)
            for (AZ::u32 x = 0; x < 3; ++x)
            {
                if (x == 1 && y == 1)
                    continue;
                const AZ::u32 a = y * 4 + x;
                indices.insert(indices.end(), { a, a + 1, a + 5, a, a + 5, a + 4 });
            }
        auto data = AZStd::make_shared<TerrainMeshHeightData>();
        ASSERT_EQ(BuildTerrainMeshHeightData(vertices, indices, *data), TerrainMeshHeightValidation::Valid);
        record.m_mesh.m_status = TerrainMeshHeightDataStatus::Ready;
        record.m_mesh.m_data = data;
        record.m_configuration.m_featherWidth = 0.0f;
        record.m_configuration.m_strength = 0.0f;
        record.m_configuration.m_uncoveredAreaPolicy = TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain;
        ASSERT_TRUE(Register(record));
        const auto candidate = Prepare(2.0f);
        ASSERT_EQ(candidate.m_query.m_meshHeightGaps.size(), 1);
        EXPECT_TRUE(candidate.m_query.m_heightContributors.empty());
        EXPECT_EQ(candidate.m_query.m_existenceContributors.size(), 1);
        EXPECT_EQ(candidate.m_query.m_meshHeightGaps[0].m_compositionSession, Session());
        EXPECT_NE(candidate.m_footprints.m_gapRendering.at(m_stamp), candidate.m_footprints.m_gapQueries.at(m_stamp));
        EXPECT_TRUE(QueryState()->m_meshHeightGaps.empty());
        EXPECT_TRUE(QueryState()->m_existenceContributors.empty());
        auto queryOnly = record;
        queryOnly.m_stampEntityId = m_peer;
        queryOnly.m_registrationId = AZ::Uuid::CreateRandom();
        queryOnly.m_configuration.AssignNewPersistentOrderingIdentity();
        queryOnly.m_configuration.m_affectTerrainRendering = false;
        ASSERT_TRUE(Register(queryOnly));
        ASSERT_EQ(QueryState()->m_meshHeightGaps.size(), 1);
        EXPECT_EQ(QueryState()->m_meshHeightGaps[0].m_entityId, m_peer);
        EXPECT_EQ(QueryState()->m_existenceContributors.size(), 1);
        const auto retained = QueryState();
        TerrainMeshCutoutRenderRegistry registry;
        Republish();
        EXPECT_EQ(QueryState()->m_meshHeightGaps.size(), 2);
        EXPECT_EQ(QueryState()->m_existenceContributors.size(), 2);
        EXPECT_EQ(retained->m_meshHeightGaps.size(), 1);
        const auto render = registry.AcquireSceneChannel(nullptr)->m_snapshot.load();
        EXPECT_EQ(render->m_meshHeightGaps.size(), 2);
    }

    TEST_F(TerrainImageRegistrationTests, SharedImageFailureRemovesAllContributionsAndReadyRevisionRestoresThem)
    {
        auto first = ContributingImage(m_stamp, 0.8f);
        auto second = MakeRecord(m_peer);
        second.m_heightmap = first.m_heightmap;
        second.m_configuration.m_featherWidth = 0.0f;
        ASSERT_TRUE(Register(first));
        ASSERT_TRUE(Register(second));
        EXPECT_FLOAT_EQ(Sample(), 0.8f);
        const auto retained = first.m_heightmap.m_data;
        ++first.m_updateRevision;
        first.m_heightmap = { HeightmapDataStatus::Error, 2, {}, retained->m_assetId };
        ASSERT_TRUE(Register(first));
        EXPECT_FLOAT_EQ(Sample(), 0.25f);
        EXPECT_FLOAT_EQ(retained->m_samples[0], 0.8f);
        auto replacement = AZStd::make_shared<HeightmapData>(*retained);
        replacement->m_revision = 3;
        replacement->m_samples[0] = 0.6f;
        ++second.m_updateRevision;
        second.m_heightmap = { HeightmapDataStatus::Ready, 3, replacement, replacement->m_assetId };
        ASSERT_TRUE(Register(second));
        EXPECT_FLOAT_EQ(Sample(), 0.6f);
        EXPECT_FLOAT_EQ(retained->m_samples[0], 0.8f);
    }

    TEST_F(TerrainImageRegistrationTests, NewRegistrationReconcilesAllImageRolesBeforePublishing)
    {
        auto record = ContributingImage(m_stamp, 0.8f);
        const auto retained = record.m_heightmap.m_data;
        record.m_surfaceIdA = { HeightmapDataStatus::Loading, 2, {}, retained->m_assetId };
        record.m_holeMask = { HeightmapDataStatus::Error, 3, {}, retained->m_assetId };
        ASSERT_TRUE(Register(record));
        EXPECT_FLOAT_EQ(Sample(), 0.25f);
        const auto records = Records();
        ASSERT_EQ(records.size(), 1);
        for (const auto* snapshot : { &records[0].m_heightmap, &records[0].m_surfaceIdA, &records[0].m_holeMask })
        {
            EXPECT_EQ(snapshot->m_revision, 3);
            EXPECT_EQ(snapshot->m_status, HeightmapDataStatus::Error);
            EXPECT_FALSE(snapshot->m_data);
        }
        EXPECT_FLOAT_EQ(retained->m_samples[0], 0.8f);
    }

    TEST_F(TerrainImageRegistrationTests, RetargetingTheLastImageRoleRetiresItsRevisionHistory)
    {
        auto first = MakeRecord(m_stamp);
        first.m_heightmap = { HeightmapDataStatus::Loading, 20, {}, { AZ::Uuid::CreateRandom(), 1 } };
        ASSERT_TRUE(Register(first));
        auto second = MakeRecord(m_peer);
        second.m_heightmap = first.m_heightmap;
        second.m_heightmap.m_revision = 1;
        first.m_heightmap = {};
        ++first.m_updateRevision;
        ASSERT_TRUE(Register(first));
        ASSERT_TRUE(Register(second));
        for (const auto& current : Records())
        {
            if (current.m_stampEntityId == m_peer) { EXPECT_EQ(current.m_heightmap.m_revision, 1); }
        }
    }

    TEST_F(TerrainImageRegistrationTests, ReactivatingTheSameCompositionObjectClearsImageRevisionHistory)
    {
        auto record = MakeRecord(m_stamp);
        record.m_heightmap = { HeightmapDataStatus::Loading, 20, {}, { AZ::Uuid::CreateRandom(), 1 } };
        ASSERT_TRUE(Register(record));
        m_composition->EditorDeactivate(m_owner);
        m_composition->EditorActivate(m_owner);
        record.m_compositionSession = Session();
        record.m_registrationId = AZ::Uuid::CreateRandom();
        record.m_heightmap.m_revision = 1;
        ASSERT_TRUE(Register(record));
        ASSERT_EQ(Records().size(), 1);
        EXPECT_EQ(Records()[0].m_heightmap.m_revision, 1);
    }

    TEST_F(TerrainImageRegistrationTests, AssetRevisionPropagatesAcrossHeightSurfaceAndHoleRoles)
    {
        auto first = MakeRecord(m_stamp);
        auto second = MakeRecord(m_peer);
        const AZ::Data::AssetId asset(AZ::Uuid::CreateRandom(), 1);
        first.m_heightmap = { HeightmapDataStatus::Loading, 8, {}, asset };
        second.m_surfaceIdA = first.m_heightmap;
        second.m_holeMask = first.m_heightmap;
        ASSERT_TRUE(Register(first));
        ASSERT_TRUE(Register(second));
        ++first.m_updateRevision;
        first.m_heightmap.m_revision = 9;
        first.m_heightmap.m_status = HeightmapDataStatus::Missing;
        ASSERT_TRUE(Register(first));
        ++second.m_updateRevision;
        ASSERT_TRUE(Register(second));
        for (const auto& record : Records())
        {
            if (record.m_stampEntityId == m_peer)
            {
                EXPECT_EQ(record.m_surfaceIdA.m_revision, 9);
                EXPECT_EQ(record.m_holeMask.m_revision, 9);
                EXPECT_EQ(record.m_holeMask.m_status, HeightmapDataStatus::Missing);
            }
        }
    }
}
