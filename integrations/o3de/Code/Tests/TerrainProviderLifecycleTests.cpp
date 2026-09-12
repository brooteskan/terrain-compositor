#include <AzTest/AzTest.h>
#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/UnitTest/UnitTest.h>
#include <AzCore/std/containers/array.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <TerrainCompositor/Components/TerrainCompositionHeightProviderComponent.h>
#include <TerrainCompositor/Components/TerrainCompositionSurfaceProviderComponent.h>
#include <Tests/Mocks/Terrain/MockTerrainDataRequestBus.h>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace TerrainCompositor::ProviderTestSupport
{
    using ChangeMask = AzFramework::Terrain::TerrainDataNotifications::TerrainDataChangedMask;
    using Weights = AzFramework::SurfaceData::SurfaceTagWeightList;

    class Context : public AzFramework::EntityIdContextQueryBus::MultiHandler
    {
    public:
        ~Context() override { BusDisconnect(); }
        AzFramework::EntityContextId GetOwningContextId() override { return m_context; }
        AzFramework::EntityContextId m_context = AZ::Uuid::CreateRandom();
    };

    class Refreshes : public Terrain::TerrainSystemServiceRequestBus::Handler
    {
    public:
        struct Area { AZ::EntityId m_region; ChangeMask m_mask; };
        Refreshes() { BusConnect(); }
        ~Refreshes() override { BusDisconnect(); }
        void Activate() override {}
        void Deactivate() override {}
        void RegisterArea(AZ::EntityId) override {}
        void UnregisterArea(AZ::EntityId) override {}
        void RefreshArea(AZ::EntityId region, ChangeMask mask) override
        {
            m_areas.push_back({ region, mask });
            if (m_observe) { m_observe(region); }
        }
        void RefreshRegion(const AZ::Aabb& region, ChangeMask mask) override
        {
            m_dirtyRegion = region;
            m_dirtyMask = mask;
            ++m_regionCount;
        }
        AZStd::vector<Area> m_areas;
        AZStd::function<void(AZ::EntityId)> m_observe;
        AZ::Aabb m_dirtyRegion = AZ::Aabb::CreateNull();
        ChangeMask m_dirtyMask{};
        int m_regionCount = 0;
    };

    struct QueryLog
    {
        void Visit(AZ::EntityId region, size_t count) const
        {
            m_regions.push_back(region);
            m_sizes.push_back(count);
            if (m_observe) { m_observe(); }
        }
        mutable AZStd::vector<AZ::EntityId> m_regions;
        mutable AZStd::vector<size_t> m_sizes;
        AZStd::function<void()> m_observe;
        float m_value = 4.0f;
    };

    class HeightQueries : public TerrainCompositionHeightRequestBus::Handler, public QueryLog
    {
    public:
        explicit HeightQueries(TerrainCompositionAddress address) { BusConnect(address); }
        ~HeightQueries() override { BusDisconnect(); }
        float GetHeight(AZ::EntityId region, const AZ::Vector3&, bool& exists) const override
        {
            Visit(region, 1);
            exists = true;
            return m_value;
        }
        void GetHeights(AZ::EntityId region, AZStd::span<const AZ::Vector3> positions,
            AZStd::span<float> heights, AZStd::span<bool> exists) const override
        {
            Visit(region, positions.size());
            AZStd::fill(heights.begin(), heights.end(), m_value);
            AZStd::fill(exists.begin(), exists.end(), true);
        }
    };

    class SurfaceQueries : public TerrainCompositionSurfaceRequestBus::Handler, public QueryLog
    {
    public:
        explicit SurfaceQueries(TerrainCompositionAddress address) { BusConnect(address); }
        ~SurfaceQueries() override { BusDisconnect(); }
        void GetSurfaceWeights(AZ::EntityId region, const AZ::Vector3&, Weights& weights) const override
        {
            Visit(region, 1);
            weights.emplace_back(AZ::Crc32(7), m_value);
        }
        void GetSurfaceWeightsFromList(AZ::EntityId region, AZStd::span<const AZ::Vector3> positions,
            AZStd::span<Weights> weights) const override
        {
            Visit(region, positions.size());
            for (auto& result : weights) { result.emplace_back(AZ::Crc32(7), m_value); }
        }
    };

    struct Height
    {
        using Component = TerrainCompositionHeightProviderComponent;
        using Config = TerrainCompositionHeightProviderConfig;
        using AreaBus = Terrain::TerrainAreaHeightRequestBus;
        using Queries = HeightQueries;
        static constexpr ChangeMask Mask = ChangeMask::HeightData | ChangeMask::SurfaceData;
        static constexpr const char* Inactive = "Inactive: no terrain height provider.";
        static constexpr const char* Ready = "Ready: composition height and terrain existence are provided to this terrain region.";
        static AZStd::vector<float> Sample(AZ::EntityId region, bool batch, size_t count = 513)
        {
            AZStd::array<AZ::Vector3, 513> positions;
            AZStd::array<bool, 513> exists{};
            positions.fill(AZ::Vector3(2.0f, 3.0f, -99.0f));
            if (batch)
            {
                AreaBus::Event(region, &AreaBus::Events::GetHeights,
                    AZStd::span<AZ::Vector3>(positions.data(), count), AZStd::span<bool>(exists.data(), count));
            }
            else
            {
                count = 1;
                AreaBus::Event(region, &AreaBus::Events::GetHeight, positions[0], positions[0], exists[0]);
            }
            AZStd::vector<float> result;
            for (size_t i = 0; i < count; ++i)
            {
                EXPECT_EQ(positions[i].GetX(), 2.0f);
                EXPECT_EQ(positions[i].GetY(), 3.0f);
                result.push_back(positions[i].GetZ());
            }
            return result;
        }
    };

    struct Surface
    {
        using Component = TerrainCompositionSurfaceProviderComponent;
        using Config = TerrainCompositionSurfaceProviderConfig;
        using AreaBus = Terrain::TerrainAreaSurfaceRequestBus;
        using Queries = SurfaceQueries;
        static constexpr ChangeMask Mask = ChangeMask::SurfaceData;
        static constexpr const char* Inactive = "Inactive: no terrain surface provider.";
        static constexpr const char* Ready = "Ready: composition surface weights are provided to this terrain region.";
        static AZStd::vector<float> Sample(AZ::EntityId region, bool batch, size_t count = 513)
        {
            AZStd::array<AZ::Vector3, 513> positions;
            AZStd::array<Weights, 513> weights;
            positions.fill(AZ::Vector3(2.0f, 3.0f, -99.0f));
            for (auto& value : weights) { value.emplace_back(AZ::Crc32(99), -99.0f); }
            if (batch)
            {
                AreaBus::Event(region, &AreaBus::Events::GetSurfaceWeightsFromList,
                    AZStd::span<const AZ::Vector3>(positions.data(), count), AZStd::span<Weights>(weights.data(), count));
            }
            else
            {
                count = 1;
                AreaBus::Event(region, &AreaBus::Events::GetSurfaceWeights, positions[0], weights[0]);
            }
            AZStd::vector<float> result;
            for (size_t i = 0; i < count; ++i)
            {
                EXPECT_LE(weights[i].size(), 1);
                if (!weights[i].empty()) { EXPECT_EQ(weights[i][0].m_surfaceType, AZ::Crc32(7)); }
                result.push_back(weights[i].empty() ? 0.0f : weights[i][0].m_weight);
            }
            return result;
        }
    };
}

namespace TerrainCompositor
{
    template<class Kind>
    class TerrainProviderLifecycleTests : public ::testing::Test
    {
    protected:
        using Config = typename Kind::Config;
        void SetUp() override
        {
            m_context.BusConnect(m_region);
            m_context.BusConnect(m_otherRegion);
            m_component = AZStd::make_unique<typename Kind::Component>();
            m_entity.AddComponent(m_component.get());
            ON_CALL(m_terrain, GetTerrainHeightBounds()).WillByDefault([this] { return m_bounds; });
            Configure(m_target);
        }
        void TearDown() override
        {
            m_refresh.m_observe = {};
            Destroy();
            AZ::SystemTickBus::ExecuteQueuedEvents();
        }
        void Destroy()
        {
            if (!m_component) { return; }
            m_component->Deactivate();
            m_entity.RemoveComponent(m_component.get());
            m_component.reset();
        }
        void Start() { m_component->EditorActivate(m_region); }
        void Stop() { m_component->EditorDeactivate(m_region); }
        void Tick() { AZ::SystemTickBus::Broadcast(&AZ::SystemTickEvents::OnSystemTick); }
        void Configure(AZ::EntityId target)
        {
            Config config;
            config.m_compositionEntityId = target;
            EXPECT_TRUE(m_component->ReadInConfig(&config));
        }
        auto& Binding() { return m_component->m_binding; }
        void ExpectRefresh(size_t index, AZ::EntityId region)
        {
            ASSERT_LT(index, m_refresh.m_areas.size());
            EXPECT_EQ(m_refresh.m_areas[index].m_region, region);
            EXPECT_EQ(m_refresh.m_areas[index].m_mask, Kind::Mask);
        }
        void ExpectSamples(float expected, bool batch = false, AZ::EntityId region = AZ::EntityId{})
        {
            for (float value : Kind::Sample(region.IsValid() ? region : m_region, batch)) { EXPECT_EQ(value, expected); }
        }
        void DrainQuery(bool batch, int action)
        {
            typename Kind::Queries original({ m_context.m_context, m_target });
            typename Kind::Queries replacement({ m_context.m_context, m_otherTarget });
            replacement.m_value = 5.0f;
            Start();
            std::mutex mutex;
            std::condition_variable changed;
            bool entered = false, attempting = false, completed = false, completedEarly = false;
            original.m_observe = [&]
            {
                std::unique_lock lock(mutex);
                if (entered) { return; }
                entered = true;
                changed.notify_all();
                changed.wait(lock, [&] { return attempting; });
                // A disconnect must wait for this entire area-bus dispatch, including later height chunks.
                completedEarly = changed.wait_for(lock, std::chrono::milliseconds(10), [&] { return completed; });
            };
            AZStd::vector<float> values;
            std::thread worker([&] { values = Kind::Sample(m_region, batch); });
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return entered; });
                attempting = true;
                changed.notify_all();
            }
            if (action == 0) { Stop(); }
            else if (action == 1) { Configure(m_otherTarget); }
            else if (action == 2) { m_component->EditorActivate(m_otherRegion); }
            else { m_context.m_context = AZ::Uuid::CreateRandom(); Tick(); }
            {
                std::lock_guard lock(mutex);
                completed = true;
                changed.notify_all();
            }
            worker.join();
            EXPECT_FALSE(completedEarly);
            for (float value : values) { EXPECT_EQ(value, original.m_value); }
            for (auto region : original.m_regions) { EXPECT_EQ(region, m_region); }
            ExpectRefresh(1, m_region);
            if (action == 1) { ExpectSamples(replacement.m_value, batch); }
            if (action == 2) { ExpectSamples(original.m_value, batch, m_otherRegion); }
        }

        const AZ::EntityId m_region{ 93001 }, m_otherRegion{ 93002 }, m_target{ 93003 }, m_otherTarget{ 93004 };
        AZ::Entity m_entity{ m_region };
        ProviderTestSupport::Context m_context;
        ProviderTestSupport::Refreshes m_refresh;
        ::testing::NiceMock<UnitTest::MockTerrainDataRequests> m_terrain;
        AzFramework::Terrain::FloatRange m_bounds{ 2.0f, 6.0f };
        AZStd::unique_ptr<typename Kind::Component> m_component;
    };

    using ProviderKinds = ::testing::Types<ProviderTestSupport::Height, ProviderTestSupport::Surface>;
    TYPED_TEST_SUITE(TerrainProviderLifecycleTests, ProviderKinds);

    TYPED_TEST(TerrainProviderLifecycleTests, InactiveConfigurationRoundTripsAndRejectsWrongTypes)
    {
        EXPECT_EQ(this->m_component->GetStatusMessage(), TypeParam::Inactive);
        typename TestFixture::Config out;
        EXPECT_TRUE(this->m_component->WriteOutConfig(&out));
        EXPECT_EQ(out.m_compositionEntityId, this->m_target);
        AZ::ComponentConfig wrong;
        EXPECT_FALSE(this->m_component->ReadInConfig(&wrong));
        EXPECT_FALSE(this->m_component->WriteOutConfig(&wrong));
        EXPECT_FALSE(this->m_component->ReadInConfig(nullptr));
        EXPECT_FALSE(this->m_component->WriteOutConfig(nullptr));
        this->Configure(this->m_otherTarget);
        this->Tick();
        EXPECT_TRUE(this->m_refresh.m_areas.empty());
        EXPECT_FALSE(TypeParam::AreaBus::HasHandlers(this->m_region));
    }

    TYPED_TEST(TerrainProviderLifecycleTests, RuntimeAndEditorActivationUseTheirRegionAndRetainStatusStrings)
    {
        typename TypeParam::Queries queries({ this->m_context.m_context, this->m_target });
        this->m_component->Activate();
        EXPECT_EQ(this->m_component->GetStatusMessage(), TypeParam::Ready);
        this->ExpectSamples(queries.m_value);
        this->ExpectRefresh(0, this->m_region);
        this->m_component->Deactivate();
        this->m_component->EditorActivate(this->m_otherRegion);
        this->ExpectSamples(queries.m_value, false, this->m_otherRegion);
        EXPECT_EQ(queries.m_regions.back(), this->m_otherRegion);
        EXPECT_FALSE(TypeParam::AreaBus::HasHandlers(this->m_region));
        this->ExpectRefresh(1, this->m_region);
        this->ExpectRefresh(2, this->m_otherRegion);
    }

    TYPED_TEST(TerrainProviderLifecycleTests, RepeatedStartAndStopInvalidateBeforeClearingTheOldBinding)
    {
        this->Start();
        this->m_refresh.m_observe = [this](AZ::EntityId region)
        {
            EXPECT_EQ(this->Binding().GetTerrainRegionEntityId(), region);
            EXPECT_EQ(TypeParam::AreaBus::HasHandlers(region), this->Binding().IsActive());
        };
        this->Start();
        EXPECT_EQ(this->m_refresh.m_areas.size(), 3);
        this->Stop();
        EXPECT_FALSE(this->Binding().IsActive());
        EXPECT_FALSE(this->Binding().GetTerrainRegionEntityId().IsValid());
        EXPECT_EQ(this->Binding().GetCompositionAddress(), TerrainCompositionAddress{});
        this->Stop();
        this->Tick();
        EXPECT_EQ(this->m_refresh.m_areas.size(), 4);
        EXPECT_EQ(this->m_component->GetStatusMessage(), TypeParam::Inactive);
    }

    TYPED_TEST(TerrainProviderLifecycleTests, SameTargetConfigurationDoesNotRestartButChangedTargetDoes)
    {
        typename TypeParam::Queries original({ this->m_context.m_context, this->m_target });
        typename TypeParam::Queries replacement({ this->m_context.m_context, this->m_otherTarget });
        replacement.m_value = 5.0f;
        this->Start();
        this->Configure(this->m_target);
        this->Tick();
        EXPECT_EQ(this->m_refresh.m_areas.size(), 1);
        this->ExpectSamples(original.m_value);
        this->Configure(this->m_otherTarget);
        EXPECT_EQ(this->m_refresh.m_areas.size(), 3);
        this->ExpectRefresh(1, this->m_region);
        this->ExpectRefresh(2, this->m_region);
        this->ExpectSamples(replacement.m_value);
    }

    TYPED_TEST(TerrainProviderLifecycleTests, ClearingTargetKeepsAreaProviderActiveAndCanRecover)
    {
        typename TypeParam::Queries queries({ this->m_context.m_context, this->m_target });
        this->Start();
        this->Configure(AZ::EntityId{});
        EXPECT_TRUE(TypeParam::AreaBus::HasHandlers(this->m_region));
        EXPECT_EQ(this->m_component->GetStatusMessage(), "Select a Terrain Composition entity.");
        this->ExpectSamples(std::is_same_v<TypeParam, ProviderTestSupport::Height> ? 2.0f : 0.0f);
        this->Configure(this->m_target);
        EXPECT_EQ(this->m_component->GetStatusMessage(), TypeParam::Ready);
        this->ExpectSamples(queries.m_value);
    }

    TYPED_TEST(TerrainProviderLifecycleTests, LateChangedAndLostContextRestartOnlyWhenOwnershipChanges)
    {
        this->m_context.BusDisconnect();
        this->Start();
        EXPECT_EQ(this->m_component->GetStatusMessage(), "Waiting for entity context ownership.");
        this->m_context.BusConnect(this->m_region);
        this->Tick();
        EXPECT_EQ(this->m_component->GetStatusMessage(), "Terrain Composition is unavailable in this entity context.");
        typename TypeParam::Queries first({ this->m_context.m_context, this->m_target });
        EXPECT_EQ(this->m_component->GetStatusMessage(), TypeParam::Ready);
        this->ExpectSamples(first.m_value);
        this->m_context.m_context = AZ::Uuid::CreateRandom();
        typename TypeParam::Queries second({ this->m_context.m_context, this->m_target });
        second.m_value = 5.0f;
        this->Tick();
        this->ExpectSamples(second.m_value);
        this->Tick();
        EXPECT_EQ(this->m_refresh.m_areas.size(), 5);
        this->m_context.BusDisconnect();
        this->Tick();
        EXPECT_EQ(this->m_component->GetStatusMessage(), "Waiting for entity context ownership.");
        EXPECT_EQ(this->m_refresh.m_areas.size(), 7);
    }

    TYPED_TEST(TerrainProviderLifecycleTests, ActiveWorkerControlCallsAreRejectedWithoutChangingRouting)
    {
        this->Start();
        typename TestFixture::Config configuration;
        configuration.m_compositionEntityId = this->m_otherTarget;
        AZ_TEST_START_TRACE_SUPPRESSION;
        std::thread worker([&]
        {
            EXPECT_FALSE(this->m_component->ReadInConfig(&configuration));
            EXPECT_FALSE(this->m_component->WriteOutConfig(&configuration));
            EXPECT_EQ(this->m_component->GetStatusMessage(), "Unavailable off the control thread.");
            this->m_component->EditorActivate(this->m_otherRegion);
            this->m_component->EditorDeactivate(this->m_region);
            this->Tick();
        });
        worker.join();
        AZ_TEST_STOP_TRACE_SUPPRESSION(6);
        EXPECT_EQ(this->Binding().GetCompositionAddress().second, this->m_target);
        EXPECT_EQ(this->m_refresh.m_areas.size(), 1);
        EXPECT_TRUE(TypeParam::AreaBus::HasHandlers(this->m_region));
    }

    TYPED_TEST(TerrainProviderLifecycleTests, ActivationRebindsAnObjectConstructedAndConfiguredOnAWorker)
    {
        this->Destroy();
        std::thread worker([&]
        {
            typename TestFixture::Config configuration;
            configuration.m_compositionEntityId = this->m_target;
            this->m_component = AZStd::make_unique<typename TypeParam::Component>(configuration);
            EXPECT_TRUE(this->m_component->ReadInConfig(&configuration));
        });
        worker.join();
        this->m_entity.AddComponent(this->m_component.get());
        this->Start();
        EXPECT_EQ(this->Binding().GetCompositionAddress().second, this->m_target);
        EXPECT_TRUE(this->m_component->GetStatusMessage().starts_with("Terrain Composition is unavailable"));
    }

    TYPED_TEST(TerrainProviderLifecycleTests, ScalarAndBatchedQueriesPreserveForwardingAndHeightChunkBoundaries)
    {
        typename TypeParam::Queries queries({ this->m_context.m_context, this->m_target });
        this->Start();
        this->ExpectSamples(queries.m_value);
        queries.m_sizes.clear();
        this->ExpectSamples(queries.m_value, true);
        if constexpr (std::is_same_v<TypeParam, ProviderTestSupport::Height>)
        {
            EXPECT_EQ(queries.m_sizes, (AZStd::vector<size_t>{ 256, 256, 1 }));
        }
        else { EXPECT_EQ(queries.m_sizes, (AZStd::vector<size_t>{ 513 })); }
        for (auto region : queries.m_regions) { EXPECT_EQ(region, this->m_region); }
    }

    TYPED_TEST(TerrainProviderLifecycleTests, QueuedTickAfterStopAndDestructionDoesNotRecreateTheProvider)
    {
        this->Start();
        AZ::SystemTickBus::QueueFunction([] { AZ::SystemTickBus::Broadcast(&AZ::SystemTickEvents::OnSystemTick); });
        this->Destroy();
        this->m_context.m_context = AZ::Uuid::CreateRandom();
        AZ::SystemTickBus::ExecuteQueuedEvents();
        EXPECT_FALSE(TypeParam::AreaBus::HasHandlers(this->m_region));
        EXPECT_EQ(this->m_refresh.m_areas.size(), 2);
    }

    TYPED_TEST(TerrainProviderLifecycleTests, StopDrainsScalarQueries) { this->DrainQuery(false, 0); }
    TYPED_TEST(TerrainProviderLifecycleTests, StopDrainsBatchedQueries) { this->DrainQuery(true, 0); }
    TYPED_TEST(TerrainProviderLifecycleTests, RetargetDrainsScalarQueries) { this->DrainQuery(false, 1); }
    TYPED_TEST(TerrainProviderLifecycleTests, RetargetDrainsBatchedQueries) { this->DrainQuery(true, 1); }
    TYPED_TEST(TerrainProviderLifecycleTests, RegionChangeDrainsBatchedQueries) { this->DrainQuery(true, 2); }
    TYPED_TEST(TerrainProviderLifecycleTests, ContextChangeDrainsBatchedQueries) { this->DrainQuery(true, 3); }

    using TerrainHeightProviderNotificationsTests = TerrainProviderLifecycleTests<ProviderTestSupport::Height>;

    TEST_F(TerrainHeightProviderNotificationsTests, BoundsClampQueriesAndOnlySettingsNotificationsRefreshThem)
    {
        ProviderTestSupport::HeightQueries queries({ m_context.m_context, m_target });
        queries.m_value = 8.0f;
        Start();
        ExpectSamples(6.0f);
        ExpectSamples(6.0f, true);
        m_bounds = { 3.0f, 5.0f };
        AzFramework::Terrain::TerrainDataNotificationBus::Broadcast(
            &AzFramework::Terrain::TerrainDataNotifications::OnTerrainDataChanged,
            AZ::Aabb::CreateNull(), ProviderTestSupport::ChangeMask::HeightData);
        ExpectSamples(6.0f);
        EXPECT_EQ(m_refresh.m_areas.size(), 1);
        AzFramework::Terrain::TerrainDataNotificationBus::Broadcast(
            &AzFramework::Terrain::TerrainDataNotifications::OnTerrainDataChanged,
            AZ::Aabb::CreateNull(), ProviderTestSupport::ChangeMask::Settings);
        ExpectSamples(5.0f, true);
        EXPECT_EQ(m_refresh.m_areas.size(), 2);
        Stop();
        m_bounds = AzFramework::Terrain::FloatRange::CreateNull();
        Start();
        ExpectSamples(8.0f);
    }

    TEST_F(TerrainHeightProviderNotificationsTests, DependencyNotificationsFollowTheTargetAndKeepTheirRefreshMasks)
    {
        Start();
        LmbrCentral::DependencyNotificationBus::Event(m_target, &LmbrCentral::DependencyNotifications::OnCompositionChanged);
        ExpectRefresh(1, m_region);
        const AZ::Aabb dirty = AZ::Aabb::CreateCenterRadius(AZ::Vector3::CreateZero(), 2.0f);
        LmbrCentral::DependencyNotificationBus::Event(
            m_target, &LmbrCentral::DependencyNotifications::OnCompositionRegionChanged, dirty);
        EXPECT_EQ(m_refresh.m_regionCount, 1);
        EXPECT_EQ(m_refresh.m_dirtyRegion, dirty);
        EXPECT_EQ(m_refresh.m_dirtyMask, ProviderTestSupport::ChangeMask::HeightData);
        LmbrCentral::DependencyNotificationBus::Event(
            m_target, &LmbrCentral::DependencyNotifications::OnCompositionRegionChanged, AZ::Aabb::CreateNull());
        ExpectRefresh(2, m_region);
        Configure(m_otherTarget);
        const size_t before = m_refresh.m_areas.size();
        LmbrCentral::DependencyNotificationBus::Event(m_target, &LmbrCentral::DependencyNotifications::OnCompositionChanged);
        EXPECT_EQ(m_refresh.m_areas.size(), before);
        LmbrCentral::DependencyNotificationBus::Event(m_otherTarget, &LmbrCentral::DependencyNotifications::OnCompositionChanged);
        ExpectRefresh(before, m_region);
        Stop();
        const size_t stopped = m_refresh.m_areas.size();
        LmbrCentral::DependencyNotificationBus::Event(m_otherTarget, &LmbrCentral::DependencyNotifications::OnCompositionChanged);
        AzFramework::Terrain::TerrainDataNotificationBus::Broadcast(
            &AzFramework::Terrain::TerrainDataNotifications::OnTerrainDataChanged,
            AZ::Aabb::CreateNull(), ProviderTestSupport::ChangeMask::Settings);
        EXPECT_EQ(m_refresh.m_areas.size(), stopped);
    }
}
