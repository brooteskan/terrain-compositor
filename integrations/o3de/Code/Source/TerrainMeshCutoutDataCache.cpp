#include <TerrainCompositor/TerrainMeshCutoutDataCache.h>
#include <TerrainCompositor/TerrainModelGeometry.h>

#include <Atom/Feature/Mesh/ModelReloaderSystemInterface.h>
#include <Atom/RPI.Reflect/Model/ModelLodAsset.h>
#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Jobs/JobFunction.h>
#include <AzCore/Name/Name.h>
#include <AzCore/std/parallel/atomic.h>
#include <AzCore/std/smart_ptr/enable_shared_from_this.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <AzFramework/Asset/AssetCatalogBus.h>

namespace TerrainCompositor
{
    namespace
    {
        AZ::u64 s_nextCutoutRevision = 0;

        AZ::Data::AssetInfo GetAssetInfo(const AZ::Data::AssetId& id)
        {
            AZ::Data::AssetInfo info;
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(info, &AZ::Data::AssetCatalogRequestBus::Events::GetAssetInfoById, id);
            return info;
        }

        TerrainMeshCutoutValidation ExtractModelGeometry(
            AZ::RPI::ModelAsset& model, AZStd::vector<AZ::Vector3>& positions, AZStd::vector<AZ::u32>& indices)
        {
            TerrainModelGeometry geometry;
            const auto validation = ExtractTerrainModelGeometry(model, {}, geometry);
            if (validation == TerrainModelGeometryValidation::Valid)
            {
                positions = AZStd::move(geometry.m_positions);
                indices = AZStd::move(geometry.m_indices);
                return TerrainMeshCutoutValidation::Valid;
            }
            if (validation == TerrainModelGeometryValidation::NonFinitePosition)
            {
                return TerrainMeshCutoutValidation::NonFinitePosition;
            }
            if (validation == TerrainModelGeometryValidation::IndexOutOfRange)
            {
                return TerrainMeshCutoutValidation::IndexOutOfRange;
            }
            if (validation == TerrainModelGeometryValidation::ResourceLimit)
            {
                return TerrainMeshCutoutValidation::ResourceLimit;
            }
            return validation == TerrainModelGeometryValidation::IndexCount ||
                    validation == TerrainModelGeometryValidation::UnsupportedIndexFormat
                ? TerrainMeshCutoutValidation::IndexCount
                : TerrainMeshCutoutValidation::Empty;
        }
    } // namespace

    class TerrainMeshCutoutDataSource final
        : public AZStd::enable_shared_from_this<TerrainMeshCutoutDataSource>
        , private AZ::Data::AssetBus::Handler
        , private AzFramework::AssetCatalogEventBus::Handler
    {
    public:
        explicit TerrainMeshCutoutDataSource(AZ::Data::AssetId assetId)
            : m_assetId(assetId)
            , m_modelReloadedHandler(
                  [this](const AZ::Data::Asset<AZ::RPI::ModelAsset>& model)
                  {
                      QueueReady(model, true);
                  })
        {
        }

        ~TerrainMeshCutoutDataSource()
        {
            Stop();
        }

        void Start()
        {
            if (!m_controlThread.Check())
                return;
            m_active = true;
            m_weakSelf = shared_from_this();
            AzFramework::AssetCatalogEventBus::Handler::BusConnect();
            StartModel();
        }

        void Stop()
        {
            if (!m_controlThread.Check())
                return;
            m_active = false;
            ++m_generation;
            AZ::Data::AssetBus::Handler::BusDisconnect();
            AzFramework::AssetCatalogEventBus::Handler::BusDisconnect();
            m_modelReloadedHandler.Disconnect();
            m_model.Reset();
            m_changed.DisconnectAllHandlers();
        }

        HeightmapControlThread m_controlThread;
        TerrainMeshCutoutDataSnapshot m_snapshot;
        TerrainMeshCutoutDataCache::ChangedEvent m_changed;

    private:
        void Publish(
            TerrainMeshCutoutDataStatus status,
            TerrainMeshCutoutValidation validation = TerrainMeshCutoutValidation::Valid,
            TerrainMeshCutoutDataPtr data = {})
        {
            if (!m_active)
                return;
            const AZ::u64 revision = data ? data->m_revision : ++s_nextCutoutRevision;
            m_snapshot = { status, validation, revision, AZStd::move(data), m_assetId };
            const auto snapshot = m_snapshot;
            m_changed.Signal(snapshot);
        }

        void StartModel()
        {
            ++m_generation;
            AZ::Data::AssetBus::Handler::BusDisconnect();
            m_modelReloadedHandler.Disconnect();
            m_model.Reset();
            const auto info = GetAssetInfo(m_assetId);
            if (!info.m_assetId.IsValid())
            {
                Publish(TerrainMeshCutoutDataStatus::Missing);
                return;
            }
            if (info.m_assetType != azrtti_typeid<AZ::RPI::ModelAsset>())
            {
                Publish(TerrainMeshCutoutDataStatus::Unsupported);
                return;
            }
            Publish(TerrainMeshCutoutDataStatus::Loading);
            m_model =
                AZ::Data::AssetManager::Instance().GetAsset<AZ::RPI::ModelAsset>(info.m_assetId, AZ::Data::AssetLoadBehavior::PreLoad);
            AZ::Data::AssetBus::Handler::BusConnect(info.m_assetId);
            if (!m_model.Get())
            {
                Publish(TerrainMeshCutoutDataStatus::Error);
            }
        }

        void QueueReady(const AZ::Data::Asset<AZ::RPI::ModelAsset>& model, bool reloaded)
        {
            const auto weak = m_weakSelf;
            const AZ::u64 generation = m_generation;
            AZ::SystemTickBus::QueueFunction(
                [weak, generation, model, reloaded]()
                {
                    if (auto source = weak.lock(); source && source->m_active)
                    {
                        source->OnReady(generation, model, reloaded);
                    }
                });
        }

        void OnReady(AZ::u64 generation, const AZ::Data::Asset<AZ::RPI::ModelAsset>& model, bool reloaded)
        {
            if (!m_controlThread.Check() || generation != m_generation || model.GetId() != m_assetId || !model.IsReady())
            {
                return;
            }
            m_model = model;
            Publish(TerrainMeshCutoutDataStatus::Loading);
            const auto weak = m_weakSelf;
            AZ::Job* job = AZ::CreateJobFunction(
                [weak, generation, model, reloaded]() mutable
                {
                    AZStd::vector<AZ::Vector3> positions;
                    AZStd::vector<AZ::u32> indices;
                    auto data = AZStd::make_shared<TerrainMeshCutoutData>();
                    data->m_assetId = model.GetId();
                    TerrainMeshCutoutValidation validation = ExtractModelGeometry(*model, positions, indices);
                    if (validation == TerrainMeshCutoutValidation::Valid)
                    {
                        validation = BuildTerrainMeshCutoutData(positions, indices, *data);
                    }
                    AZ::SystemTickBus::QueueFunction(
                        [weak, generation, model, data, validation, reloaded]()
                        {
                            (void)reloaded;
                            if (auto source = weak.lock(); source && source->m_active && source->m_generation == generation &&
                                source->m_model.GetId() == model.GetId())
                            {
                                if (validation == TerrainMeshCutoutValidation::Valid)
                                {
                                    data->m_revision = ++s_nextCutoutRevision;
                                    source->Publish(TerrainMeshCutoutDataStatus::Ready, validation, data);
                                }
                                else
                                {
                                    source->Publish(TerrainMeshCutoutDataStatus::InvalidGeometry, validation);
                                }
                            }
                        });
                },
                true);
            job->Start();
        }

        void QueueFailure()
        {
            const auto weak = m_weakSelf;
            const AZ::u64 generation = m_generation;
            AZ::SystemTickBus::QueueFunction(
                [weak, generation]()
                {
                    if (auto source = weak.lock(); source && source->m_active && source->m_generation == generation)
                    {
                        source->Publish(TerrainMeshCutoutDataStatus::Error);
                    }
                });
        }

        void OnAssetReady(AZ::Data::Asset<AZ::Data::AssetData> asset) override
        {
            QueueReady(asset, false);
        }
        void OnAssetReloaded(AZ::Data::Asset<AZ::Data::AssetData> asset) override
        {
            QueueReady(asset, true);
        }
        void OnAssetPreReload([[maybe_unused]] AZ::Data::Asset<AZ::Data::AssetData> asset) override
        {
            const AZ::u64 generation = ++m_generation;
            const auto weak = m_weakSelf;
            AZ::SystemTickBus::QueueFunction(
                [weak, generation]()
                {
                    if (auto source = weak.lock(); source && source->m_active && source->m_generation == generation)
                    {
                        source->Publish(TerrainMeshCutoutDataStatus::Loading);
                    }
                });
        }
        void OnAssetError([[maybe_unused]] AZ::Data::Asset<AZ::Data::AssetData> asset) override
        {
            QueueFailure();
        }
        void OnAssetReloadError([[maybe_unused]] AZ::Data::Asset<AZ::Data::AssetData> asset) override
        {
            QueueFailure();
        }

        void QueueCatalogChanged(const AZ::Data::AssetId& id, bool removed)
        {
            if (id != m_assetId)
                return;
            const auto weak = m_weakSelf;
            AZ::SystemTickBus::QueueFunction(
                [weak, removed]()
                {
                    if (auto source = weak.lock(); source && source->m_active)
                    {
                        if (removed && !GetAssetInfo(source->m_assetId).m_assetId.IsValid())
                        {
                            ++source->m_generation;
                            source->Publish(TerrainMeshCutoutDataStatus::Missing);
                            return;
                        }
                        if (!source->m_model.IsReady())
                        {
                            source->StartModel();
                        }
                        else if (auto* reloader = AZ::Render::ModelReloaderSystemInterface::Get())
                        {
                            ++source->m_generation;
                            source->Publish(TerrainMeshCutoutDataStatus::Loading);
                            source->m_modelReloadedHandler.Disconnect();
                            reloader->ReloadModel(source->m_model, source->m_modelReloadedHandler);
                        }
                        else
                        {
                            source->StartModel();
                        }
                    }
                });
        }

        void OnCatalogAssetAdded(const AZ::Data::AssetId& id) override
        {
            QueueCatalogChanged(id, false);
        }
        void OnCatalogAssetChanged(const AZ::Data::AssetId& id) override
        {
            QueueCatalogChanged(id, false);
        }
        void OnCatalogAssetRemoved(const AZ::Data::AssetId& id, [[maybe_unused]] const AZ::Data::AssetInfo& info) override
        {
            QueueCatalogChanged(id, true);
        }

        const AZ::Data::AssetId m_assetId;
        AZStd::weak_ptr<TerrainMeshCutoutDataSource> m_weakSelf;
        AZ::Data::Asset<AZ::RPI::ModelAsset> m_model;
        AZ::Render::ModelReloadedEvent::Handler m_modelReloadedHandler;
        AZStd::atomic<AZ::u64> m_generation = 0;
        bool m_active = false;
    };

    TerrainMeshCutoutDataCache::TerrainMeshCutoutDataCache()
    {
        TerrainMeshCutoutDataCacheInterface::Register(this);
    }

    TerrainMeshCutoutDataCache::~TerrainMeshCutoutDataCache()
    {
        TerrainMeshCutoutDataCacheInterface::Unregister(this);
        for (const auto& [id, weak] : m_sources)
        {
            (void)id;
            if (auto source = weak.lock())
                source->Stop();
        }
    }

    TerrainMeshCutoutDataCache::Handle TerrainMeshCutoutDataCache::Acquire(const AZ::Data::AssetId& assetId)
    {
        if (!m_controlThread.Check() || !assetId.IsValid())
            return {};
        const auto info = GetAssetInfo(assetId);
        const AZ::Data::AssetId canonical = info.m_assetId.IsValid() ? info.m_assetId : assetId;
        for (auto iterator = m_sources.begin(); iterator != m_sources.end();)
        {
            if (iterator->second.expired())
            {
                iterator = m_sources.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        if (auto source = m_sources[canonical].lock())
            return source;
        auto source = AZStd::make_shared<TerrainMeshCutoutDataSource>(canonical);
        m_sources[canonical] = source;
        source->Start();
        return source;
    }

    TerrainMeshCutoutDataSnapshot TerrainMeshCutoutDataCache::GetSnapshot(const Handle& handle)
    {
        return handle && handle->m_controlThread.Check() ? handle->m_snapshot : TerrainMeshCutoutDataSnapshot{};
    }

    void TerrainMeshCutoutDataCache::ConnectChangedHandler(const Handle& handle, ChangedEvent::Handler& handler)
    {
        if (handle && handle->m_controlThread.Check())
            handler.Connect(handle->m_changed);
    }
} // namespace TerrainCompositor
