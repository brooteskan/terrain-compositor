#include <AzTest/AzTest.h>
#include <AzCore/UnitTest/UnitTest.h>
#include <AzCore/std/parallel/thread.h>
#include <AzFramework/Scene/SceneSystemComponent.h>
#include <AzToolsFramework/Entity/EditorEntityInfoBus.h>
#include <Tests/Mocks/Terrain/MockTerrainDataRequestBus.h>
#include "Editor/EditorHeightmapStampComponent.h"
#include "Editor/EditorTerrainMeshCutoutComponent.h"
#include "Editor/EditorTerrainMeshHeightStampComponent.h"
#include "Editor/EditorTerrainCompositionGradientComponent.h"
#include "Editor/EditorTerrainCompositionHeightProviderComponent.h"
#include "Editor/EditorTerrainCompositionSurfaceProviderComponent.h"

namespace TerrainCompositor::EditorPreviewTestSupport
{
    class Selection : public AzToolsFramework::EditorEntityInfoRequestBus::Handler
    {
    public:
        explicit Selection(AZ::EntityId id) { BusConnect(id); }
        ~Selection() override { BusDisconnect(); }
        AZ::EntityId GetParent() const override { return AZ::EntityId{}; }
        AzToolsFramework::EntityIdList GetChildren() const override { return {}; }
        AZ::EntityId GetChild(AZStd::size_t) const override { return AZ::EntityId{}; }
        AZStd::size_t GetChildCount() const override { return 0; }
        AZ::u64 GetChildIndex(AZ::EntityId) const override { return 0; }
        AZStd::string GetName() const override { return {}; }
        AZStd::string GetSliceAssetName() const override { return {}; }
        bool IsSliceEntity() const override { return false; }
        bool IsSubsliceEntity() const override { return false; }
        bool IsSliceRoot() const override { return false; }
        bool IsSubsliceRoot() const override { return false; }
        bool HasSliceEntityAnyChildrenAddedOrDeleted() const override { return false; }
        bool HasSliceEntityPropertyOverridesInTopLevel() const override { return false; }
        bool HasSliceEntityOverrides() const override { return false; }
        bool HasSliceChildrenOverrides() const override { return false; }
        bool HasSliceAnyOverrides() const override { return false; }
        bool HasCyclicDependency() const override { return false; }
        void AddToCyclicDependencyList(const AZ::EntityId&) override {}
        void RemoveFromCyclicDependencyList(const AZ::EntityId&) override {}
        AzToolsFramework::EntityIdList GetCyclicDependencyList() const override { return {}; }
        AZ::u64 GetIndexForSorting() const override { return 0; }
        bool IsSelected() const override { return m_selected; }
        bool IsVisible() const override { return true; }
        bool IsHidden() const override { return false; }
        bool IsLocked() const override { return false; }
        AzToolsFramework::EditorEntityStartStatus GetStartStatus() const override { return {}; }
        bool IsJustThisEntityLocked() const override { return false; }
        bool IsComponentExpanded(AZ::ComponentId) const override { return false; }
        void SetComponentExpanded(AZ::ComponentId, bool) override {}
        bool m_selected = true;
    };

    class PropertyDisplay : public AzToolsFramework::ToolsApplicationNotificationBus::Handler
    {
    public:
        PropertyDisplay() { BusConnect(); }
        ~PropertyDisplay() override { BusDisconnect(); }
        void InvalidatePropertyDisplayForComponent(
            AZ::EntityComponentIdPair id, AzToolsFramework::PropertyModificationRefreshLevel level) override
        {
            m_lastId = id;
            m_lastLevel = level;
            ++m_count;
        }
        AZ::EntityComponentIdPair m_lastId;
        AzToolsFramework::PropertyModificationRefreshLevel m_lastLevel{};
        int m_count = 0;
    };

    class Context : public AzFramework::EntityIdContextQueryBus::Handler
    {
    public:
        explicit Context(AZ::EntityId id) { BusConnect(id); }
        ~Context() override { BusDisconnect(); }
        AzFramework::EntityContextId GetOwningContextId() override { return m_context; }
        AzFramework::EntityContextId m_context = AZ::Uuid::CreateRandom();
    };

    class Transform : public AZ::TransformBus::Handler
    {
    public:
        explicit Transform(AZ::EntityId id) { BusConnect(id); }
        ~Transform() override { BusDisconnect(); }
        void BindTransformChangedEventHandler(AZ::TransformChangedEvent::Handler&) override {}
        void BindParentChangedEventHandler(AZ::ParentChangedEvent::Handler&) override {}
        void BindChildChangedEventHandler(AZ::ChildChangedEvent::Handler&) override {}
        void NotifyChildChangedEvent(AZ::ChildChangeType, AZ::EntityId) override {}
        const AZ::Transform& GetLocalTM() override { return m_world; }
        const AZ::Transform& GetWorldTM() override { return m_world; }
        bool IsStaticTransform() override { return false; }
        AZ::Transform m_world = AZ::Transform::CreateIdentity();
    };

    struct Image
    {
        using Editor = EditorHeightmapStampComponent;
        using Runtime = HeightmapStampComponent;
        using Config = HeightmapStampConfig;
        static void Change(Config& config) { config.m_targetCompositionEntityId = AZ::EntityId(91002); }
        static void Export(Editor& editor, const AZStd::string& key) { editor.SetExportOrderKey(key); }
    };
    struct Cutout
    {
        using Editor = EditorTerrainMeshCutoutComponent;
        using Runtime = TerrainMeshCutoutComponent;
        using Config = TerrainMeshCutoutConfig;
        static void Change(Config& config)
        {
            config.m_cutoutMeshAsset = AZ::Data::Asset<AZ::RPI::ModelAsset>(
                AZ::Data::AssetId(AZ::Uuid::CreateRandom(), 1), azrtti_typeid<AZ::RPI::ModelAsset>());
        }
        static void Export(Editor& editor, const AZStd::string& key) { editor.SetExportData(key); }
    };
    struct Height
    {
        using Editor = EditorTerrainMeshHeightStampComponent;
        using Runtime = TerrainMeshHeightStampComponent;
        using Config = TerrainMeshHeightStampConfig;
        static void Change(Config& config)
        {
            config.m_terrainMeshAsset = AZ::Data::Asset<AZ::RPI::ModelAsset>(
                AZ::Data::AssetId(AZ::Uuid::CreateRandom(), 1), azrtti_typeid<AZ::RPI::ModelAsset>());
        }
        static void Export(Editor& editor, const AZStd::string& key) { editor.SetExportData(key); }
    };
    struct Composition
    {
        using Editor = EditorTerrainCompositionGradientComponent;
        using Runtime = TerrainCompositionGradientComponent;
        using Config = TerrainCompositionConfig;
        static void Change(Config& config) { config.m_proceduralSourceEntityId = AZ::EntityId(91002); }
    };
    struct HeightProvider
    {
        using Editor = EditorTerrainCompositionHeightProviderComponent;
        using Runtime = TerrainCompositionHeightProviderComponent;
        using Config = TerrainCompositionHeightProviderConfig;
        static void Change(Config& config) { config.m_compositionEntityId = AZ::EntityId(91002); }
    };
    struct SurfaceProvider
    {
        using Editor = EditorTerrainCompositionSurfaceProviderComponent;
        using Runtime = TerrainCompositionSurfaceProviderComponent;
        using Config = TerrainCompositionSurfaceProviderConfig;
        static void Change(Config& config) { config.m_compositionEntityId = AZ::EntityId(91002); }
    };
}

namespace TerrainCompositor
{
    template<class Kind>
    class TerrainEditorPreviewLifecycleTests : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_editor = AZStd::make_unique<typename Kind::Editor>();
            Attach();
        }
        void TearDown() override
        {
            Destroy();
            AZ::TickBus::ExecuteQueuedEvents();
            AZ::SystemTickBus::ExecuteQueuedEvents();
        }
        void Attach()
        {
            m_editor->SetSerializedIdentifier("PreviewLifecycleTest");
            m_entity.AddComponent(m_editor.get());
        }
        void Start() { m_editor->Activate(); }
        void Stop() { m_editor->Deactivate(); }
        void Destroy()
        {
            if (!m_editor) return;
            Stop();
            m_entity.RemoveComponent(m_editor.get());
            m_editor.reset();
        }
        auto* Preview() const { return m_editor->m_preview.get(); }
        auto Status() const { return m_editor->GetStatusText(); }
        void Tick(float elapsed)
        {
            AZ::TickBus::Broadcast(&AZ::TickEvents::OnTick, elapsed, AZ::ScriptTimePoint{});
        }
        auto ChangedConfiguration()
        {
            typename Kind::Config config;
            Kind::Change(config);
            return config;
        }
        auto ChangePreviewStatus()
        {
            auto config = ChangedConfiguration();
            EXPECT_TRUE(Preview()->ReadInConfig(&config));
            return Preview()->GetStatusMessage();
        }

        const AZ::EntityId m_owner{ 91001 };
        AzFramework::SceneSystemComponent m_sceneSystem;
        EditorPreviewTestSupport::Context m_context{ m_owner };
        EditorPreviewTestSupport::Transform m_transform{ m_owner };
        EditorPreviewTestSupport::Selection m_selection{ m_owner };
        EditorPreviewTestSupport::PropertyDisplay m_display;
        AZ::Entity m_entity{ m_owner };
        AZStd::unique_ptr<typename Kind::Editor> m_editor;
    };

    using EditorKinds = ::testing::Types<EditorPreviewTestSupport::Image, EditorPreviewTestSupport::Cutout,
        EditorPreviewTestSupport::Height, EditorPreviewTestSupport::Composition,
        EditorPreviewTestSupport::HeightProvider, EditorPreviewTestSupport::SurfaceProvider>;
    TYPED_TEST_SUITE(TerrainEditorPreviewLifecycleTests, EditorKinds);

    TYPED_TEST(TerrainEditorPreviewLifecycleTests, ActivationOwnsPreviewAndRepeatedTeardownRestoresInactiveStatus)
    {
        EXPECT_EQ(this->Preview(), nullptr);
        const auto inactive = this->Status();
        EXPECT_EQ(inactive.find("Inactive:"), 0);
        this->Start();
        ASSERT_NE(this->Preview(), nullptr);
        EXPECT_EQ(this->Status(), this->Preview()->GetStatusMessage());
        this->Stop();
        this->Stop();
        EXPECT_EQ(this->Preview(), nullptr);
        EXPECT_EQ(this->Status(), inactive);
        this->Start();
        EXPECT_EQ(this->Status(), this->Preview()->GetStatusMessage());
        this->Tick(1.0f);
        EXPECT_EQ(this->m_display.m_count, 0);
    }

    TYPED_TEST(TerrainEditorPreviewLifecycleTests, WorkerConstructionAndConfigurationDoNotCreateRuntimePreview)
    {
        this->Destroy();
        auto config = this->ChangedConfiguration();
        AZStd::thread worker([this, &config]
        {
            this->m_editor = AZStd::make_unique<typename TypeParam::Editor>();
            EXPECT_TRUE(this->m_editor->ReadInConfig(&config));
            EXPECT_EQ(this->Preview(), nullptr);
        });
        worker.join();
        this->Attach();
        this->Start();
        ASSERT_NE(this->Preview(), nullptr);
        EXPECT_EQ(this->Status(), this->Preview()->GetStatusMessage());
    }

    TYPED_TEST(TerrainEditorPreviewLifecycleTests, PollingAccumulatesSelectedTimeAndOnlyInvalidatesChangedStatus)
    {
        this->Start();
        const auto before = this->Status();
        const auto after = this->ChangePreviewStatus();
        ASSERT_NE(before, after);
        this->Tick(0.125f);
        EXPECT_EQ(this->Status(), before);
        this->m_selection.m_selected = false;
        AzToolsFramework::EntitySelectionEvents::Bus::Event(
            this->m_owner, &AzToolsFramework::EntitySelectionEvents::OnDeselected);
        this->Tick(10.0f);
        EXPECT_EQ(this->Status(), before);
        EXPECT_EQ(this->m_display.m_count, 0);
        this->m_selection.m_selected = true;
        AzToolsFramework::EntitySelectionEvents::Bus::Event(
            this->m_owner, &AzToolsFramework::EntitySelectionEvents::OnSelected);
        this->Tick(EditorPreviewStatusPollIntervalSeconds);
        EXPECT_EQ(this->Status(), after);
        EXPECT_EQ(this->m_display.m_count, 1);
        EXPECT_EQ(this->m_display.m_lastId, AZ::EntityComponentIdPair(this->m_owner, this->m_editor->GetId()));
        EXPECT_EQ(this->m_display.m_lastLevel, AzToolsFramework::Refresh_AttributesAndValues);
        this->Tick(1.0f);
        EXPECT_EQ(this->m_display.m_count, 1);
    }

    TYPED_TEST(TerrainEditorPreviewLifecycleTests, ConfigurationRefreshesImmediatelyAndPreservesAccumulatedPollingTime)
    {
        this->Start();
        auto* originalPreview = this->Preview();
        const auto before = this->Status();
        this->Tick(0.125f);
        auto config = this->ChangedConfiguration();
        ASSERT_TRUE(this->m_editor->ReadInConfig(&config));
        EXPECT_EQ(this->Preview(), originalPreview);
        EXPECT_EQ(this->Status(), this->Preview()->GetStatusMessage());
        EXPECT_NE(this->Status(), before);
        EXPECT_EQ(this->m_display.m_count, 0);
        typename TypeParam::Config original;
        ASSERT_TRUE(this->Preview()->ReadInConfig(&original));
        this->Tick(0.125f);
        EXPECT_EQ(this->Status(), before);
        EXPECT_EQ(this->m_display.m_count, 1);
    }

    TYPED_TEST(TerrainEditorPreviewLifecycleTests, InactiveAndWrongTypeConfigurationDoNotConstructOrReplacePreview)
    {
        const auto inactive = this->Status();
        auto config = this->ChangedConfiguration();
        ASSERT_TRUE(this->m_editor->ReadInConfig(&config));
        EXPECT_EQ(this->Preview(), nullptr);
        EXPECT_EQ(this->Status(), inactive);
        AZ::ComponentConfig wrong;
        EXPECT_FALSE(this->m_editor->ReadInConfig(nullptr));
        EXPECT_FALSE(this->m_editor->ReadInConfig(&wrong));
        EXPECT_FALSE(this->m_editor->WriteOutConfig(&wrong));
        this->Start();
        auto* preview = this->Preview();
        const auto status = this->Status();
        EXPECT_FALSE(this->m_editor->ReadInConfig(&wrong));
        EXPECT_EQ(this->Preview(), preview);
        EXPECT_EQ(this->Status(), status);
    }

    TYPED_TEST(TerrainEditorPreviewLifecycleTests, ReactivationResetsPollingClockAndTeardownDisconnectsQueuedTicks)
    {
        this->Start();
        this->Tick(0.125f);
        this->Stop();
        this->Start();
        const auto before = this->Status();
        this->ChangePreviewStatus();
        this->Tick(0.125f);
        EXPECT_EQ(this->Status(), before);
        EXPECT_EQ(this->m_display.m_count, 0);
        AZ::TickBus::QueueFunction([]
        {
            AZ::TickBus::Broadcast(&AZ::TickEvents::OnTick, 1.0f, AZ::ScriptTimePoint{});
        });
        this->Destroy();
        AZ::TickBus::ExecuteQueuedEvents();
        AZ::SystemTickBus::ExecuteQueuedEvents();
        this->Tick(1.0f);
        EXPECT_EQ(this->m_display.m_count, 0);
        EXPECT_FALSE(AzFramework::EntityDebugDisplayEventBus::HasHandlers(this->m_owner));
    }

    TYPED_TEST(TerrainEditorPreviewLifecycleTests, ExportBuildsUnderlyingRuntimeTypeWithoutStartingPreview)
    {
        auto config = this->ChangedConfiguration();
        ASSERT_TRUE(this->m_editor->ReadInConfig(&config));
        if constexpr (requires { TypeParam::Export(*this->m_editor, AZStd::string{}); })
        {
            TypeParam::Export(*this->m_editor, MakeUuidStampOrderKey(AZ::Uuid::CreateRandom()));
        }
        AZ::Entity gameEntity;
        this->m_editor->BuildGameEntity(&gameEntity);
        auto* runtime = gameEntity.FindComponent<typename TypeParam::Runtime>();
        ASSERT_NE(runtime, nullptr);
        EXPECT_EQ(this->m_editor->GetUnderlyingComponentType(), azrtti_typeid<typename TypeParam::Runtime>());
        typename TypeParam::Config exported, authored;
        ASSERT_TRUE(runtime->WriteOutConfig(&exported));
        ASSERT_TRUE(this->m_editor->WriteOutConfig(&authored));
        if constexpr (requires { exported.m_stableOrderKey; })
            EXPECT_EQ(exported.m_stableOrderKey, authored.m_stableOrderKey);
        else if constexpr (requires { exported.m_compositionEntityId; })
            EXPECT_EQ(exported.m_compositionEntityId, authored.m_compositionEntityId);
        else
            EXPECT_EQ(exported.m_proceduralSourceEntityId, authored.m_proceduralSourceEntityId);
        EXPECT_EQ(this->Preview(), nullptr);
    }

    using TerrainCompositionEditorPreviewTests = TerrainEditorPreviewLifecycleTests<EditorPreviewTestSupport::Composition>;
    TEST_F(TerrainCompositionEditorPreviewTests, CapturesQualityBaselineBeforePreviewActivationAndRestoresItOnStop)
    {
        float resolution = 2.0f;
        int captures = 0;
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> terrain;
        ON_CALL(terrain, GetTerrainHeightQueryResolution).WillByDefault([&]
        {
            if (captures++ == 0)
            {
                EXPECT_NE(Preview(), nullptr);
                EXPECT_EQ(Preview()->GetStatusMessage(), "Inactive: no composed gradient.");
            }
            return resolution;
        });
        ON_CALL(terrain, SetTerrainHeightQueryResolution).WillByDefault([&](float value) { resolution = value; });
        TerrainCompositionConfig config;
        config.m_terrainQuality.m_overrideTerrainQuality = true;
        config.m_terrainQuality.m_heightQueryResolution = 0.5f;
        ASSERT_TRUE(m_editor->ReadInConfig(&config));
        Start();
        EXPECT_GT(captures, 0);
        EXPECT_FLOAT_EQ(resolution, 0.5f);
        Stop();
        EXPECT_FLOAT_EQ(resolution, 2.0f);
    }

    template<class Kind>
    class TerrainStampEditorExportTests : public TerrainEditorPreviewLifecycleTests<Kind> {};
    using StampEditorKinds = ::testing::Types<EditorPreviewTestSupport::Image,
        EditorPreviewTestSupport::Cutout, EditorPreviewTestSupport::Height>;
    TYPED_TEST_SUITE(TerrainStampEditorExportTests, StampEditorKinds);

    TYPED_TEST(TerrainStampEditorExportTests, RejectsMissingBakedKeyAndDoesNotOverwriteLiveAuthoringIdentity)
    {
        AZ::Entity rejected;
        AZ_TEST_START_TRACE_SUPPRESSION;
        this->m_editor->BuildGameEntity(&rejected);
        AZ_TEST_STOP_TRACE_SUPPRESSION(1);
        EXPECT_TRUE(rejected.GetComponents().empty());
        const auto key = MakeUuidStampOrderKey(AZ::Uuid::CreateRandom());
        TypeParam::Export(*this->m_editor, key);
        this->Start();
        AZ_TEST_START_TRACE_SUPPRESSION;
        TypeParam::Export(*this->m_editor, MakeUuidStampOrderKey(AZ::Uuid::CreateRandom()));
        AZ_TEST_STOP_TRACE_SUPPRESSION(1);
        typename TypeParam::Config authored;
        ASSERT_TRUE(this->m_editor->WriteOutConfig(&authored));
        EXPECT_EQ(authored.m_stableOrderKey, key);
        this->Stop();
        const auto newKey = MakeUuidStampOrderKey(AZ::Uuid::CreateRandom());
        TypeParam::Export(*this->m_editor, newKey);
        ASSERT_TRUE(this->m_editor->WriteOutConfig(&authored));
        EXPECT_EQ(authored.m_stableOrderKey, newKey);
    }
}
