#include <AzTest/AzTest.h>
#include <Atom/RPI.Reflect/Model/ModelAsset.h>
#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Jobs/JobContext.h>
#include <AzCore/Jobs/JobManager.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/parallel/thread.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzFramework/Asset/AssetCatalogBus.h>
#include <TerrainCompositor/TerrainMeshCutoutDataCache.h>
#include <TerrainCompositor/TerrainMeshHeightDataCache.h>
#include "TerrainTestFixtures.h"

namespace TerrainCompositor
{
    namespace ModelCacheTestSupport
    {
        class Model : public AZ::RPI::ModelAsset
        {
        public:
            void MarkReady() { m_status = AssetStatus::Ready; }
        };

        class ModelHandler : public AZ::Data::AssetHandler
        {
        public:
            AZ::Data::AssetPtr CreateAsset(const AZ::Data::AssetId&, const AZ::Data::AssetType&) override
            {
                return aznew Model;
            }
            void DestroyAsset(AZ::Data::AssetPtr asset) override { delete asset; }
            void GetHandledAssetTypes(AZStd::vector<AZ::Data::AssetType>& types) override
            {
                types.push_back(azrtti_typeid<AZ::RPI::ModelAsset>());
            }
            LoadResult LoadAssetData(const AZ::Data::Asset<AZ::Data::AssetData>&,
                AZStd::shared_ptr<AZ::Data::AssetDataStream>, const AZ::Data::AssetFilterCB&) override
            {
                ADD_FAILURE() << "Lifecycle tests must use in-memory models, not file loading";
                return LoadResult::Error;
            }
        };

        class Catalog : public AZ::Data::AssetCatalogRequestBus::Handler
        {
        public:
            Catalog() { BusConnect(); }
            ~Catalog() override { BusDisconnect(); }
            AZ::Data::AssetInfo GetAssetInfoById(const AZ::Data::AssetId& id) override
            {
                const auto found = m_assets.find(id);
                return found == m_assets.end() ? AZ::Data::AssetInfo{} : found->second;
            }
            void Add(const AZ::Data::AssetId& id, AZ::Data::AssetType type = azrtti_typeid<AZ::RPI::ModelAsset>())
            {
                AZ::Data::AssetInfo info;
                info.m_assetId = id;
                info.m_assetType = type;
                m_assets[id] = info;
            }
            AZStd::unordered_map<AZ::Data::AssetId, AZ::Data::AssetInfo> m_assets;
        };

        struct Cutout
        {
            using Cache = TerrainMeshCutoutDataCache;
            using Snapshot = TerrainMeshCutoutDataSnapshot;
            using Status = TerrainMeshCutoutDataStatus;
            static constexpr Status Prepared = Status::InvalidGeometry;
        };

        struct MeshHeight
        {
            using Cache = TerrainMeshHeightDataCache;
            using Snapshot = TerrainMeshHeightDataSnapshot;
            using Status = TerrainMeshHeightDataStatus;
            static constexpr Status Prepared = Status::InvalidModel;
        };
    }

    template<class Kind>
    class TerrainModelCacheLifecycleTests : public ::testing::Test
    {
    protected:
        using Cache = typename Kind::Cache;
        using Snapshot = typename Kind::Snapshot;
        using Status = typename Kind::Status;

        void SetUp() override
        {
            ASSERT_FALSE(AZ::Data::AssetManager::IsReady());
            AZ::Data::AssetManager::Create(AZ::Data::AssetManager::Descriptor{});
            m_handler = AZStd::make_unique<ModelCacheTestSupport::ModelHandler>();
            AZ::Data::AssetManager::Instance().RegisterHandler(m_handler.get(), azrtti_typeid<AZ::RPI::ModelAsset>());
            // Zero workers run preparation synchronously; its publication still crosses the real SystemTick queue.
            m_jobs = AZStd::make_unique<AZ::JobManager>(AZ::JobManagerDesc{});
            m_jobContext = AZStd::make_unique<AZ::JobContext>(*m_jobs);
            AZ::JobContext::SetGlobalContext(m_jobContext.get());
            m_catalog = AZStd::make_unique<ModelCacheTestSupport::Catalog>();
            m_cache = AZStd::make_unique<Cache>();
            m_changed = typename Cache::ChangedEvent::Handler([this](const Snapshot& snapshot) { m_events.push_back(snapshot); });
            MakeModel();
        }

        void TearDown() override
        {
            m_changed.Disconnect();
            m_cache.reset();
            m_handle.reset();
            Drain();
            m_model.Reset();
            m_catalog.reset();
            AZ::Data::AssetManager::Instance().DispatchEvents();
            AZ::Data::AssetManager::Instance().UnregisterHandler(m_handler.get());
            m_handler.reset();
            AZ::Data::AssetManager::Destroy();
            AZ::JobContext::SetGlobalContext(nullptr);
            m_jobContext.reset();
            m_jobs.reset();
        }

        void Drain()
        {
            for (int pass = 0; pass < 16 && AZ::SystemTickBus::QueuedEventCount() != 0; ++pass)
                AZ::SystemTickBus::ExecuteQueuedEvents();
            EXPECT_EQ(AZ::SystemTickBus::QueuedEventCount(), 0);
        }

        void MakeModel()
        {
            m_model = AZ::Data::AssetManager::Instance().CreateAsset(m_id, azrtti_typeid<AZ::RPI::ModelAsset>());
            ASSERT_TRUE(m_model.Get());
            static_cast<ModelCacheTestSupport::Model*>(m_model.Get())->MarkReady();
            m_catalog->Add(m_id);
        }

        void Acquire()
        {
            m_handle = m_cache->Acquire(m_id);
            ASSERT_TRUE(m_handle);
            Cache::ConnectChangedHandler(m_handle, m_changed);
        }

        void SendReady() { AZ::Data::AssetBus::Event(m_id, &AZ::Data::AssetEvents::OnAssetReady, m_model); }
        void SendFailure() { AZ::Data::AssetBus::Event(m_id, &AZ::Data::AssetEvents::OnAssetError, m_model); }
        Snapshot Current() const { return Cache::GetSnapshot(m_handle); }

        size_t Count(Status status) const
        {
            return AZStd::count_if(m_events.begin(), m_events.end(), [status](const Snapshot& snapshot)
                { return snapshot.m_status == status; });
        }

        TestSupport::ScopedNameDictionary m_names;
        const AZ::Data::AssetId m_id{ AZ::Uuid::CreateRandom(), 1 };
        AZStd::unique_ptr<ModelCacheTestSupport::Catalog> m_catalog;
        AZStd::unique_ptr<ModelCacheTestSupport::ModelHandler> m_handler;
        AZStd::unique_ptr<AZ::JobManager> m_jobs;
        AZStd::unique_ptr<AZ::JobContext> m_jobContext;
        AZStd::unique_ptr<Cache> m_cache;
        typename Cache::Handle m_handle;
        typename Cache::ChangedEvent::Handler m_changed;
        AZ::Data::Asset<AZ::RPI::ModelAsset> m_model;
        AZStd::vector<Snapshot> m_events;
    };

    using ModelCacheKinds = ::testing::Types<ModelCacheTestSupport::Cutout, ModelCacheTestSupport::MeshHeight>;
    TYPED_TEST_SUITE(TerrainModelCacheLifecycleTests, ModelCacheKinds);

    TYPED_TEST(TerrainModelCacheLifecycleTests, InvalidIdsAreRejectedAndAliasesShareTheCanonicalSubscription)
    {
        EXPECT_FALSE(this->m_cache->Acquire({}));
        const AZ::Data::AssetId alias(AZ::Uuid::CreateRandom(), 2);
        this->m_catalog->m_assets[alias] = this->m_catalog->GetAssetInfoById(this->m_id);
        this->Acquire();
        EXPECT_EQ(this->m_cache->Acquire(alias), this->m_handle);
        EXPECT_EQ(this->m_cache->Acquire(this->m_id), this->m_handle);
        this->Drain();
        EXPECT_EQ(this->Current().m_assetId, this->m_id);
    }

    TYPED_TEST(TerrainModelCacheLifecycleTests, ReadyCallbacksPublishOnlyOnTheControlThreadQueue)
    {
        this->Acquire();
        EXPECT_EQ(this->Current().m_status, TypeParam::Status::Loading);
        EXPECT_TRUE(this->m_events.empty());
        AZ::SystemTickBus::ExecuteQueuedEvents();
        EXPECT_EQ(this->Current().m_status, TypeParam::Status::Loading);
        EXPECT_EQ(this->Count(TypeParam::Prepared), 0);
        this->Drain();
        EXPECT_EQ(this->Current().m_status, TypeParam::Prepared);
        EXPECT_EQ(this->Count(TypeParam::Prepared), 1);
        // Empty in-memory models deliberately exercise a completed, rejected geometry preparation.
        EXPECT_FALSE(this->Current().m_data);
    }

    TYPED_TEST(TerrainModelCacheLifecycleTests, NewerAcceptedPreparationSuppressesAnOlderQueuedCompletion)
    {
        this->Acquire();
        this->SendReady(); // Both ready callbacks precede either completion publication.
        AZ::SystemTickBus::ExecuteQueuedEvents();
        ASSERT_EQ(this->Count(TypeParam::Status::Loading), 2);
        this->Drain();
        EXPECT_EQ(this->Count(TypeParam::Prepared), 1);
    }

    TYPED_TEST(TerrainModelCacheLifecycleTests, FailureRetiresPreparationAlreadyWaitingToPublish)
    {
        this->Acquire();
        AZ::SystemTickBus::ExecuteQueuedEvents();
        ASSERT_GT(AZ::SystemTickBus::QueuedEventCount(), 0);
        this->SendFailure();
        this->Drain();
        EXPECT_EQ(this->Current().m_status, TypeParam::Status::Error);
        EXPECT_EQ(this->Count(TypeParam::Prepared), 0);
        EXPECT_EQ(this->Count(TypeParam::Status::Error), 1);
    }

    TYPED_TEST(TerrainModelCacheLifecycleTests, FailureDuringPreparationCannotBeOverwrittenByItsCompletion)
    {
        this->Acquire();
        using Cache = typename TypeParam::Cache;
        typename Cache::ChangedEvent::Handler failOnLoading([this](const auto& snapshot)
        {
            if (snapshot.m_status == TypeParam::Status::Loading)
                this->SendFailure();
        });
        Cache::ConnectChangedHandler(this->m_handle, failOnLoading);
        this->Drain();
        EXPECT_EQ(this->Count(TypeParam::Status::Error), 1);
        EXPECT_EQ(this->Count(TypeParam::Prepared), 0);
        EXPECT_EQ(this->Current().m_status, TypeParam::Status::Error);
    }

    TYPED_TEST(TerrainModelCacheLifecycleTests, WorkerThreadAssetFailureDoesNotNotifySubscribersUntilTheControlThreadPumps)
    {
        this->Acquire();
        this->Drain();
        this->m_events.clear();
        AZStd::thread worker([this] { this->SendFailure(); });
        worker.join();
        EXPECT_TRUE(this->m_events.empty());
        EXPECT_EQ(this->Current().m_status, TypeParam::Prepared);
        this->Drain();
        EXPECT_EQ(this->Count(TypeParam::Status::Error), 1);
        EXPECT_EQ(this->Current().m_status, TypeParam::Status::Error);
    }

    TYPED_TEST(TerrainModelCacheLifecycleTests, FailureBeforeQueuedReadyRetiresItAndLaterReloadRecovers)
    {
        this->Acquire();
        this->SendFailure();
        this->Drain();
        EXPECT_EQ(this->Count(TypeParam::Status::Loading), 0);
        EXPECT_EQ(this->Count(TypeParam::Prepared), 0);
        EXPECT_EQ(this->Count(TypeParam::Status::Error), 1);
        EXPECT_EQ(this->Current().m_status, TypeParam::Status::Error);
        const auto failedRevision = this->Current().m_revision;

        AZ::Data::AssetBus::Event(this->m_id, &AZ::Data::AssetEvents::OnAssetReloaded, this->m_model);
        this->Drain();
        EXPECT_EQ(this->Count(TypeParam::Status::Loading), 1);
        EXPECT_EQ(this->Count(TypeParam::Prepared), 1);
        EXPECT_EQ(this->Current().m_status, TypeParam::Prepared);
        EXPECT_GT(this->Current().m_revision, failedRevision);
    }

    TYPED_TEST(TerrainModelCacheLifecycleTests, WorkerReloadFailureRetiresQueuedPreparationAndLaterReadyRecovers)
    {
        this->Acquire();
        AZ::SystemTickBus::ExecuteQueuedEvents();
        ASSERT_GT(AZ::SystemTickBus::QueuedEventCount(), 0);
        this->m_events.clear();
        AZStd::thread worker([this]
        {
            AZ::Data::AssetBus::Event(this->m_id, &AZ::Data::AssetEvents::OnAssetReloadError, this->m_model);
        });
        worker.join();
        EXPECT_TRUE(this->m_events.empty());
        EXPECT_EQ(this->Current().m_status, TypeParam::Status::Loading);
        this->Drain();
        EXPECT_EQ(this->Count(TypeParam::Prepared), 0);
        EXPECT_EQ(this->Count(TypeParam::Status::Error), 1);
        EXPECT_EQ(this->Current().m_status, TypeParam::Status::Error);
        const auto failedRevision = this->Current().m_revision;

        this->SendReady();
        this->Drain();
        EXPECT_EQ(this->Count(TypeParam::Status::Loading), 1);
        EXPECT_EQ(this->Count(TypeParam::Prepared), 1);
        EXPECT_EQ(this->Current().m_status, TypeParam::Prepared);
        EXPECT_GT(this->Current().m_revision, failedRevision);
    }

    TYPED_TEST(TerrainModelCacheLifecycleTests, PreReloadRetiresOldPreparationAndLaterReadyRecovers)
    {
        this->Acquire();
        AZ::SystemTickBus::ExecuteQueuedEvents();
        AZ::Data::AssetBus::Event(this->m_id, &AZ::Data::AssetEvents::OnAssetPreReload, this->m_model);
        this->Drain();
        EXPECT_EQ(this->Current().m_status, TypeParam::Status::Loading);
        EXPECT_EQ(this->Count(TypeParam::Prepared), 0);
        AZ::Data::AssetBus::Event(this->m_id, &AZ::Data::AssetEvents::OnAssetReloaded, this->m_model);
        this->Drain();
        EXPECT_EQ(this->Current().m_status, TypeParam::Prepared);
        EXPECT_EQ(this->Count(TypeParam::Prepared), 1);
    }

    TYPED_TEST(TerrainModelCacheLifecycleTests, MissingProductRecoversAfterDiscoveryAndRemoveRecreate)
    {
        this->m_catalog->m_assets.erase(this->m_id);
        this->Acquire();
        EXPECT_EQ(this->Current().m_status, TypeParam::Status::Missing);
        this->m_catalog->Add(this->m_id);
        AzFramework::AssetCatalogEventBus::Broadcast(&AzFramework::AssetCatalogEvents::OnCatalogAssetAdded, this->m_id);
        this->Drain();
        EXPECT_EQ(this->Current().m_status, TypeParam::Prepared);
        const auto revision = this->Current().m_revision;
        const auto info = this->m_catalog->GetAssetInfoById(this->m_id);
        this->m_catalog->m_assets.erase(this->m_id);
        AzFramework::AssetCatalogEventBus::Broadcast(&AzFramework::AssetCatalogEvents::OnCatalogAssetRemoved, this->m_id, info);
        this->Drain();
        EXPECT_EQ(this->Current().m_status, TypeParam::Status::Missing);
        EXPECT_GT(this->Current().m_revision, revision);
        this->m_catalog->Add(this->m_id);
        AzFramework::AssetCatalogEventBus::Broadcast(&AzFramework::AssetCatalogEvents::OnCatalogAssetAdded, this->m_id);
        this->Drain();
        EXPECT_EQ(this->Current().m_status, TypeParam::Prepared);
    }

    TYPED_TEST(TerrainModelCacheLifecycleTests, DelayedRemovalCannotDiscardARecreatedCatalogProduct)
    {
        this->Acquire();
        this->Drain();
        const auto info = this->m_catalog->GetAssetInfoById(this->m_id);
        this->m_catalog->m_assets.erase(this->m_id);
        AzFramework::AssetCatalogEventBus::Broadcast(&AzFramework::AssetCatalogEvents::OnCatalogAssetRemoved, this->m_id, info);
        this->m_catalog->Add(this->m_id);
        this->m_events.clear();
        this->Drain();
        EXPECT_EQ(this->Count(TypeParam::Status::Missing), 0);
        EXPECT_EQ(this->Current().m_status, TypeParam::Prepared);
    }

    TYPED_TEST(TerrainModelCacheLifecycleTests, CacheDestructionDisconnectsSubscribersAndRetiresQueuedWork)
    {
        this->Acquire();
        AZ::SystemTickBus::ExecuteQueuedEvents();
        ASSERT_TRUE(this->m_changed.IsConnected());
        this->m_cache.reset();
        EXPECT_FALSE(this->m_changed.IsConnected());
        const size_t previousEvents = this->m_events.size();
        this->Drain();
        EXPECT_EQ(this->m_events.size(), previousEvents);
        using Cache = typename TypeParam::Cache;
        this->m_cache = AZStd::make_unique<Cache>();
        const auto replacement = this->m_cache->Acquire(this->m_id);
        EXPECT_NE(replacement, this->m_handle);
        this->Drain();
        EXPECT_EQ(Cache::GetSnapshot(replacement).m_status, TypeParam::Prepared);
    }

    TYPED_TEST(TerrainModelCacheLifecycleTests, LastHandleReleaseRetiresCallbacksAndAllowsFreshAcquisition)
    {
        this->Acquire();
        AZ::SystemTickBus::ExecuteQueuedEvents();
        this->m_handle.reset();
        EXPECT_FALSE(this->m_changed.IsConnected());
        this->Drain();
        EXPECT_EQ(this->Count(TypeParam::Prepared), 0);
        this->Acquire();
        this->Drain();
        EXPECT_EQ(this->Count(TypeParam::Prepared), 1);
    }
}
