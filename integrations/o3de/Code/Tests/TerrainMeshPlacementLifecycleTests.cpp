#include <AzTest/AzTest.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/std/parallel/thread.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzFramework/Scene/SceneSystemComponent.h>
#include <TerrainCompositor/Components/TerrainCompositionGradientComponent.h>
#include <TerrainCompositor/Components/TerrainMeshCutoutComponent.h>
#include <TerrainCompositor/Components/TerrainMeshHeightStampComponent.h>

namespace TerrainCompositor::MeshPlacementTestSupport
{
    class Transform : public AZ::TransformBus::Handler
    {
    public:
        explicit Transform(AZ::EntityId entity) { BusConnect(entity); }
        ~Transform() override { BusDisconnect(); }
        void BindTransformChangedEventHandler(AZ::TransformChangedEvent::Handler&) override {}
        void BindParentChangedEventHandler(AZ::ParentChangedEvent::Handler&) override {}
        void BindChildChangedEventHandler(AZ::ChildChangedEvent::Handler&) override {}
        void NotifyChildChangedEvent(AZ::ChildChangeType, AZ::EntityId) override {}
        const AZ::Transform& GetLocalTM() override { return m_world; }
        const AZ::Transform& GetWorldTM() override { return m_world; }
        bool IsStaticTransform() override { return false; }
        AZStd::vector<AZ::EntityId> GetAllDescendants() override { return m_descendants; }
        AZ::Transform m_world = AZ::Transform::CreateIdentity();
        AZStd::vector<AZ::EntityId> m_descendants;
    };

    class Mesh : public AZ::Render::MeshComponentRequestBus::Handler
    {
    public:
        Mesh(AZ::EntityId entity, AZ::Data::AssetId asset) : m_asset(asset) { BusConnect(entity); }
        ~Mesh() override { BusDisconnect(); }
        void SetModelAsset(AZ::Data::Asset<AZ::RPI::ModelAsset>) override {}
        AZ::Data::Asset<const AZ::RPI::ModelAsset> GetModelAsset() const override { return {}; }
        void SetModelAssetId(AZ::Data::AssetId asset) override { m_asset = asset; }
        AZ::Data::AssetId GetModelAssetId() const override { return m_asset; }
        void SetModelAssetPath(const AZStd::string&) override {}
        AZStd::string GetModelAssetPath() const override { return {}; }
        AZ::Data::Instance<AZ::RPI::Model> GetModel() const override { return {}; }
        void SetSortKey(AZ::RHI::DrawItemSortKey) override {}
        AZ::RHI::DrawItemSortKey GetSortKey() const override { return {}; }
        void SetIsAlwaysDynamic(bool) override {}
        bool GetIsAlwaysDynamic() const override { return false; }
        void SetLodType(AZ::RPI::Cullable::LodType) override {}
        AZ::RPI::Cullable::LodType GetLodType() const override { return {}; }
        void SetLodOverride(AZ::RPI::Cullable::LodOverride) override {}
        AZ::RPI::Cullable::LodOverride GetLodOverride() const override { return {}; }
        void SetMinimumScreenCoverage(float) override {}
        float GetMinimumScreenCoverage() const override { return 0.0f; }
        void SetQualityDecayRate(float) override {}
        float GetQualityDecayRate() const override { return 0.0f; }
        void SetVisibility(bool visible) override { m_visible = visible; ++m_visibilityWrites; }
        bool GetVisibility() const override { return m_visible; }
        void SetRayTracingEnabled(bool) override {}
        bool GetRayTracingEnabled() const override { return false; }
        void SetExcludeFromReflectionCubeMaps(bool) override {}
        bool GetExcludeFromReflectionCubeMaps() const override { return false; }
        AZ::Aabb GetWorldBounds() const override { return AZ::Aabb::CreateNull(); }
        AZ::Aabb GetLocalBounds() const override { return AZ::Aabb::CreateNull(); }
        AZ::Data::AssetId m_asset;
        bool m_visible = true;
        int m_visibilityWrites = 0;
    };

    class Scale : public AZ::NonUniformScaleRequestBus::Handler
    {
    public:
        explicit Scale(AZ::EntityId entity) { BusConnect(entity); }
        ~Scale() override { BusDisconnect(); }
        AZ::Vector3 GetScale() const override { return AZ::Vector3::CreateOne(); }
        void SetScale(const AZ::Vector3& scale) override { m_changed.Signal(scale); }
        void RegisterScaleChangedEvent(AZ::NonUniformScaleChangedEvent::Handler& handler) override { handler.Connect(m_changed); }
        AZ::NonUniformScaleChangedEvent m_changed;
    };

    class Context : public AzFramework::EntityIdContextQueryBus::MultiHandler
    {
    public:
        ~Context() override { BusDisconnect(); }
        AzFramework::EntityContextId GetOwningContextId() override { return m_id; }
        AzFramework::EntityContextId m_id = AZ::Uuid::CreateRandom();
    };

    class Identity : public HeightmapStampIdentityInterface::Registrar
    {
    public:
        AZStd::string ResolveStampOrderKey(AZ::EntityId) const override { return m_key; }
        AZStd::string m_key;
    };

    struct Cutout
    {
        using Component = TerrainMeshCutoutComponent;
        using Config = TerrainMeshCutoutConfig;
        using Record = TerrainMeshCutoutRegistrationData;
        static auto& Asset(Config& config) { return config.m_cutoutMeshAsset; }
        static void Configure(Component& component, const Config& config) { component.SetCutoutConfiguration(config); }
        static constexpr auto Enumerate = &TerrainCompositionRequests::GetRegisteredMeshCutouts;
    };

    struct Height
    {
        using Component = TerrainMeshHeightStampComponent;
        using Config = TerrainMeshHeightStampConfig;
        using Record = TerrainMeshHeightStampRegistrationData;
        static auto& Asset(Config& config) { return config.m_terrainMeshAsset; }
        static void Configure(Component& component, const Config& config) { component.SetStampConfiguration(config); }
        static constexpr auto Enumerate = &TerrainCompositionRequests::GetRegisteredMeshHeightStamps;
    };
}

namespace TerrainCompositor
{
    template<class Kind>
    class TerrainMeshPlacementLifecycleTests : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_context.BusConnect(m_owner);
            m_context.BusConnect(m_target);
            m_composition.EditorActivate(m_target);
            m_configuration.m_targetCompositionEntityId = m_target;
            m_configuration.AssignNewPersistentOrderingIdentity();
            Kind::Asset(m_configuration) = AZ::Data::Asset<AZ::RPI::ModelAsset>(m_asset, azrtti_typeid<AZ::RPI::ModelAsset>());
            m_transform.m_descendants = { m_child };
            m_childTransform.m_world.SetTranslation(AZ::Vector3(3.0f, 4.0f, 5.0f));
            m_component = AZStd::make_unique<typename Kind::Component>(m_configuration);
            m_entity.AddComponent(m_component.get());
        }

        void TearDown() override
        {
            Destroy();
            m_composition.EditorDeactivate(m_target);
            AZ::SystemTickBus::ExecuteQueuedEvents();
            AZ::TickBus::ExecuteQueuedEvents();
        }

        void Start(bool editor = false)
        {
            m_editor = editor;
            if (editor) m_component->EditorActivate(m_owner);
            else m_component->Activate();
        }
        void Stop()
        {
            if (m_editor) m_component->EditorDeactivate(m_owner);
            else m_component->Deactivate();
        }
        void Destroy()
        {
            if (!m_component) return;
            Stop();
            m_entity.RemoveComponent(m_component.get());
            m_component.reset();
        }
        void Tick(int count = 1)
        {
            while (count-- > 0)
            {
                AZ::SystemTickBus::ExecuteQueuedEvents();
                AZ::SystemTickBus::Broadcast(&AZ::SystemTickEvents::OnSystemTick);
            }
        }
        auto Records() const
        {
            AZStd::vector<typename Kind::Record> records;
            TerrainCompositionRequestBus::EventResult(records, TerrainCompositionAddress{ m_context.m_id, m_target }, Kind::Enumerate);
            return records;
        }
        void ChildrenChanged()
        {
            AZ::TransformNotificationBus::Event(m_owner, &AZ::TransformNotificationBus::Events::OnChildAdded, m_child);
        }
        void ModelReady()
        {
            AZ::Render::MeshComponentNotificationBus::Event(m_child, &AZ::Render::MeshComponentNotifications::OnModelReady,
                AZ::Data::Asset<AZ::RPI::ModelAsset>{}, AZ::Data::Instance<AZ::RPI::Model>{});
        }

        const AZ::EntityId m_owner{ 81001 }, m_target{ 81002 }, m_child{ 81003 }, m_otherChild{ 81004 };
        const AZ::Data::AssetId m_asset{ AZ::Uuid::CreateRandom(), 1 };
        AzFramework::SceneSystemComponent m_sceneSystem;
        MeshPlacementTestSupport::Context m_context;
        TerrainCompositionGradientComponent m_composition;
        AZ::Entity m_entity{ m_owner };
        MeshPlacementTestSupport::Transform m_transform{ m_owner }, m_childTransform{ m_child };
        MeshPlacementTestSupport::Mesh m_mesh{ m_child, m_asset };
        typename Kind::Config m_configuration;
        AZStd::unique_ptr<typename Kind::Component> m_component;
        bool m_editor = false;
    };

    using MeshPlacementKinds = ::testing::Types<MeshPlacementTestSupport::Cutout, MeshPlacementTestSupport::Height>;
    TYPED_TEST_SUITE(TerrainMeshPlacementLifecycleTests, MeshPlacementKinds);

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, UniqueDescendantRegistersItsTransformAndRuntimeRestoresVisibility)
    {
        this->Start();
        ASSERT_EQ(this->Records().size(), 1);
        EXPECT_EQ(this->Records()[0].m_worldTransform, this->m_childTransform.m_world);
        EXPECT_TRUE(this->m_mesh.m_visible);
        this->Tick(8);
        EXPECT_FALSE(this->m_mesh.m_visible);
        EXPECT_EQ(this->m_mesh.m_visibilityWrites, 1);
        this->Tick();
        EXPECT_EQ(this->m_mesh.m_visibilityWrites, 1);
        this->Stop();
        EXPECT_TRUE(this->Records().empty());
        EXPECT_TRUE(this->m_mesh.m_visible);
        EXPECT_FALSE(AZ::Render::MeshComponentNotificationBus::HasHandlers(this->m_child));
    }

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, MatchingOwnerUsesOwnerTransformAndDoesNotDoubleBind)
    {
        this->m_mesh.BusDisconnect();
        MeshPlacementTestSupport::Mesh ownerMesh(this->m_owner, this->m_asset);
        this->Start();
        ASSERT_EQ(this->Records().size(), 1);
        EXPECT_EQ(this->Records()[0].m_worldTransform, this->m_transform.m_world);
        this->Tick();
        EXPECT_FALSE(ownerMesh.m_visible);
        this->Stop();
        EXPECT_TRUE(ownerMesh.m_visible);
    }

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, InitiallyHiddenMeshStaysHiddenAfterReconfigurationAndStop)
    {
        this->m_mesh.m_visible = false;
        this->Start();
        this->Tick(2);
        ++this->m_configuration.m_priority;
        TypeParam::Configure(*this->m_component, this->m_configuration);
        this->Tick();
        this->Stop();
        EXPECT_FALSE(this->m_mesh.m_visible);
    }

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, LateChildAndModelHandlerAreDiscoveredWithinRetryWindow)
    {
        this->m_transform.m_descendants.clear();
        this->m_mesh.BusDisconnect();
        this->Start();
        EXPECT_TRUE(this->Records().empty());
        EXPECT_NE(this->m_component->GetStatusMessage().find("Waiting for the matching"), AZStd::string::npos);
        this->Tick(3);
        this->m_transform.m_descendants.push_back(this->m_child);
        this->Tick();
        EXPECT_TRUE(this->Records().empty());
        this->m_mesh.BusConnect(this->m_child);
        this->Tick();
        ASSERT_EQ(this->Records().size(), 1);
        EXPECT_FALSE(this->m_mesh.m_visible);
    }

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, AmbiguityRetiresClaimAndRestoresMeshThenRecoversWithNewLease)
    {
        this->Start();
        this->Tick();
        const auto lease = this->Records()[0].m_registrationId;
        MeshPlacementTestSupport::Mesh other(this->m_otherChild, this->m_asset);
        this->m_transform.m_descendants.push_back(this->m_otherChild);
        this->ChildrenChanged();
        this->Tick();
        EXPECT_TRUE(this->Records().empty());
        EXPECT_TRUE(this->m_mesh.m_visible);
        EXPECT_TRUE(other.m_visible);
        EXPECT_NE(this->m_component->GetStatusMessage().find("ambiguous"), AZStd::string::npos);
        this->m_transform.m_descendants.pop_back();
        AZ::TransformNotificationBus::Event(this->m_owner, &AZ::TransformNotificationBus::Events::OnChildRemoved, this->m_otherChild);
        this->Tick();
        ASSERT_EQ(this->Records().size(), 1);
        EXPECT_NE(this->Records()[0].m_registrationId, lease);
        EXPECT_FALSE(this->m_mesh.m_visible);
    }

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, ParentChangeRebindsAndDisconnectsOldTransformNotifications)
    {
        this->Start();
        this->Tick();
        MeshPlacementTestSupport::Transform otherTransform(this->m_otherChild);
        MeshPlacementTestSupport::Mesh other(this->m_otherChild, this->m_asset);
        otherTransform.m_world.SetTranslation(AZ::Vector3(9.0f, 8.0f, 7.0f));
        this->m_transform.m_descendants = { this->m_otherChild };
        AZ::TransformNotificationBus::Event(this->m_child, &AZ::TransformNotificationBus::Events::OnParentChanged,
            this->m_owner, AZ::EntityId{});
        this->Tick();
        ASSERT_EQ(this->Records().size(), 1);
        EXPECT_EQ(this->Records()[0].m_worldTransform, otherTransform.m_world);
        EXPECT_TRUE(this->m_mesh.m_visible);
        EXPECT_FALSE(other.m_visible);
        EXPECT_FALSE(AZ::Render::MeshComponentNotificationBus::HasHandlers(this->m_child));
        const auto revision = this->Records()[0].m_updateRevision;
        AZ::TransformNotificationBus::Event(this->m_child, &AZ::TransformNotificationBus::Events::OnTransformChanged,
            this->m_childTransform.m_world, this->m_childTransform.m_world);
        EXPECT_EQ(this->Records()[0].m_updateRevision, revision);
        this->Stop();
        EXPECT_TRUE(other.m_visible);
    }

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, TransformAndScaleChangesUpdateExistingLease)
    {
        MeshPlacementTestSupport::Scale scale(this->m_child);
        this->Start();
        ASSERT_EQ(this->Records().size(), 1);
        const auto before = this->Records()[0];
        this->m_childTransform.m_world.SetTranslation(AZ::Vector3(10.0f, 0.0f, 0.0f));
        AZ::TransformNotificationBus::Event(this->m_child, &AZ::TransformNotificationBus::Events::OnTransformChanged,
            this->m_childTransform.m_world, this->m_childTransform.m_world);
        EXPECT_EQ(this->Records()[0].m_worldTransform, this->m_childTransform.m_world);
        const auto revision = this->Records()[0].m_updateRevision;
        scale.SetScale(AZ::Vector3(2.0f, 1.0f, 1.0f));
        EXPECT_GT(this->Records()[0].m_updateRevision, revision);
        EXPECT_EQ(this->Records()[0].m_registrationId, before.m_registrationId);
        EXPECT_TRUE(this->Records()[0].m_hasNonUniformScale);
        this->Stop();
        scale.SetScale(AZ::Vector3::CreateOne());
        EXPECT_TRUE(this->Records().empty());
    }

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, ConfigurationRetargetsModelAndRestoresPreviousVisibility)
    {
        this->Start();
        this->Tick();
        const AZ::Data::AssetId otherAsset{ AZ::Uuid::CreateRandom(), 1 };
        MeshPlacementTestSupport::Transform otherTransform(this->m_otherChild);
        MeshPlacementTestSupport::Mesh other(this->m_otherChild, otherAsset);
        this->m_transform.m_descendants.push_back(this->m_otherChild);
        TypeParam::Asset(this->m_configuration) = AZ::Data::Asset<AZ::RPI::ModelAsset>(otherAsset, azrtti_typeid<AZ::RPI::ModelAsset>());
        TypeParam::Configure(*this->m_component, this->m_configuration);
        EXPECT_TRUE(this->m_mesh.m_visible);
        ASSERT_EQ(this->Records().size(), 1);
        EXPECT_EQ(TypeParam::Asset(this->Records()[0].m_configuration).GetId(), otherAsset);
        this->Tick();
        EXPECT_FALSE(other.m_visible);
        this->Stop();
        EXPECT_TRUE(other.m_visible);
    }

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, WorkerModelNotificationDefersRegistrationUntilControlTick)
    {
        this->Start();
        this->Tick(8);
        const auto revision = this->Records()[0].m_updateRevision;
        AZStd::thread worker([this] { this->ModelReady(); });
        worker.join();
        EXPECT_EQ(this->Records()[0].m_updateRevision, revision);
        this->Tick();
        EXPECT_GT(this->Records()[0].m_updateRevision, revision);
    }

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, QueuedCallbacksCannotRestartStoppedOrReactivatedInstance)
    {
        this->Start();
        this->Tick(8);
        this->ModelReady();
        this->Stop();
        this->Tick();
        EXPECT_TRUE(this->Records().empty());
        EXPECT_TRUE(this->m_mesh.m_visible);
        this->Start();
        this->ModelReady();
        this->Stop();
        this->Start();
        // Leave old queued callbacks pending while the new activation exhausts its own retry window.
        for (int i = 0; i < 8; ++i) AZ::SystemTickBus::Broadcast(&AZ::SystemTickEvents::OnSystemTick);
        const int writes = this->m_mesh.m_visibilityWrites;
        this->Tick();
        EXPECT_EQ(this->m_mesh.m_visibilityWrites, writes);
    }

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, QueuedPreDestroyCallbackIsSafeAfterComponentDestruction)
    {
        this->Start();
        this->Tick();
        AZ::Render::MeshComponentNotificationBus::Event(this->m_child, &AZ::Render::MeshComponentNotifications::OnModelPreDestroy);
        this->Destroy();
        this->Tick(10);
        EXPECT_TRUE(this->Records().empty());
        EXPECT_TRUE(this->m_mesh.m_visible);
        EXPECT_FALSE(AZ::Render::MeshComponentNotificationBus::HasHandlers(this->m_child));
    }

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, EditorPreservesVisibilityAndResolvesIdentityWithoutMutatingConfig)
    {
        MeshPlacementTestSupport::Identity identity;
        this->Start(true);
        ASSERT_EQ(this->Records().size(), 1);
        EXPECT_TRUE(this->Records()[0].m_identityPending);
        EXPECT_TRUE(this->Records()[0].m_configuration.m_orderingId.IsNull());
        identity.m_key = MakeUuidStampOrderKey(AZ::Uuid::CreateRandom());
        HeightmapStampIdentityNotificationBus::Broadcast(&HeightmapStampIdentityNotifications::OnStampIdentitiesChanged);
        EXPECT_FALSE(this->Records()[0].m_identityPending);
        EXPECT_EQ(this->Records()[0].m_configuration.m_stableOrderKey, identity.m_key);
        this->Tick(10);
        this->ModelReady();
        this->Tick(10);
        EXPECT_EQ(this->m_mesh.m_visibilityWrites, 0);
        this->Stop();
        this->Start();
        EXPECT_EQ(this->Records()[0].m_configuration.m_orderingId, this->m_configuration.m_orderingId);
        EXPECT_FALSE(this->Records()[0].m_identityPending);
        this->Tick();
        EXPECT_FALSE(this->m_mesh.m_visible);
    }

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, ConfigurationBeforeActivationDoesNotRegisterOrHideMesh)
    {
        TypeParam::Configure(*this->m_component, this->m_configuration);
        this->Tick(10);
        EXPECT_TRUE(this->Records().empty());
        EXPECT_EQ(this->m_mesh.m_visibilityWrites, 0);
        this->Start();
        this->Tick();
        EXPECT_EQ(this->Records().size(), 1);
        EXPECT_FALSE(this->m_mesh.m_visible);
    }

    TYPED_TEST(TerrainMeshPlacementLifecycleTests, ExhaustedRetryWindowWaitsForHierarchyNotification)
    {
        this->m_mesh.BusDisconnect();
        this->Start();
        this->Tick(8);
        EXPECT_NE(this->m_component->GetStatusMessage().find("No Atom Mesh instance"), AZStd::string::npos);
        this->m_mesh.BusConnect(this->m_child);
        this->Tick();
        EXPECT_TRUE(this->Records().empty());
        this->ChildrenChanged();
        this->Tick();
        ASSERT_EQ(this->Records().size(), 1);
        // A newly resolved placement independently schedules and completes visibility work.
        EXPECT_FALSE(this->m_mesh.m_visible);
        const int writes = this->m_mesh.m_visibilityWrites;
        this->ModelReady();
        this->Tick();
        EXPECT_FALSE(this->m_mesh.m_visible);
        EXPECT_EQ(this->m_mesh.m_visibilityWrites, writes + 1);
    }
}
