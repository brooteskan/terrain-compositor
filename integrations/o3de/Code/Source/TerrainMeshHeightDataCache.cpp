#include <TerrainCompositor/TerrainMeshHeightDataCache.h>

#include <Atom/Feature/Mesh/ModelReloaderSystemInterface.h>
#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Jobs/JobFunction.h>
#include <AzCore/std/parallel/atomic.h>
#include <AzCore/std/smart_ptr/enable_shared_from_this.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <AzFramework/Asset/AssetCatalogBus.h>

namespace TerrainCompositor
{
    namespace
    {
        AZ::u64 s_nextMeshHeightRevision = 0;
        AZ::u64 s_nextMeshHeightPreparationTicket = 0;

        AZ::Data::AssetInfo GetAssetInfo(const AZ::Data::AssetId& id)
        {
            AZ::Data::AssetInfo info;
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(info, &AZ::Data::AssetCatalogRequestBus::Events::GetAssetInfoById, id);
            return info;
        }
    } // namespace

    class TerrainMeshHeightDataSource final
        : public AZStd::enable_shared_from_this<TerrainMeshHeightDataSource>
        , private AZ::Data::AssetBus::Handler
        , private AzFramework::AssetCatalogEventBus::Handler
    {
    public:
        explicit TerrainMeshHeightDataSource(AZ::Data::AssetId assetId)
            : m_assetId(assetId)
            , m_modelReloadedHandler(
                  [this](const AZ::Data::Asset<AZ::RPI::ModelAsset>& model)
                  {
                      QueueReady(model);
                  })
        {
        }

        ~TerrainMeshHeightDataSource()
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
        TerrainMeshHeightDataSnapshot m_snapshot;
        TerrainMeshHeightDataCache::ChangedEvent m_changed;

    private:
        void Publish(
            TerrainMeshHeightDataStatus status,
            TerrainModelGeometryValidation modelValidation = TerrainModelGeometryValidation::Valid,
            TerrainMeshHeightValidation validation = TerrainMeshHeightValidation::Valid,
            TerrainMeshHeightDataPtr data = {},
            TerrainMeshHeightBuildDiagnostics diagnostics = {})
        {
            if (!m_active)
                return;
            const AZ::u64 revision = data ? data->m_revision : ++s_nextMeshHeightRevision;
            m_snapshot = { status, modelValidation, validation, revision, AZStd::move(data), m_assetId, AZStd::move(diagnostics) };
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
                Publish(TerrainMeshHeightDataStatus::Missing);
                return;
            }
            if (info.m_assetType != azrtti_typeid<AZ::RPI::ModelAsset>())
            {
                Publish(TerrainMeshHeightDataStatus::Unsupported);
                return;
            }
            Publish(TerrainMeshHeightDataStatus::Loading);
            m_model =
                AZ::Data::AssetManager::Instance().GetAsset<AZ::RPI::ModelAsset>(info.m_assetId, AZ::Data::AssetLoadBehavior::PreLoad);
            AZ::Data::AssetBus::Handler::BusConnect(info.m_assetId);
            if (!m_model.Get())
            {
                Publish(TerrainMeshHeightDataStatus::Error);
            }
        }

        void QueueReady(const AZ::Data::Asset<AZ::RPI::ModelAsset>& model)
        {
            const auto weak = m_weakSelf;
            const AZ::u64 generation = m_generation;
            AZ::SystemTickBus::QueueFunction(
                [weak, generation, model]()
                {
                    if (auto source = weak.lock(); source && source->m_active)
                    {
                        source->OnReady(generation, model);
                    }
                });
        }

        void OnReady(AZ::u64 generation, const AZ::Data::Asset<AZ::RPI::ModelAsset>& model)
        {
            if (!m_controlThread.Check() || generation != m_generation || model.GetId() != m_assetId || !model.IsReady())
            {
                return;
            }
            m_model = model;
            Publish(TerrainMeshHeightDataStatus::Loading);
            const auto weak = m_weakSelf;
            const AZ::u64 preparationTicket = ++s_nextMeshHeightPreparationTicket;
            m_latestPreparationTicket = preparationTicket;
            AZ::Job* job = AZ::CreateJobFunction(
                [weak, generation, preparationTicket, model]() mutable
                {
                    TerrainModelGeometry geometry;
                    const auto modelValidation = ExtractTerrainModelGeometry(*model, model.GetId(), geometry);
                    auto data = AZStd::make_shared<TerrainMeshHeightData>();
                    data->m_assetId = model.GetId();
                    TerrainMeshHeightValidation validation = TerrainMeshHeightValidation::Valid;
                    TerrainMeshHeightBuildDiagnostics diagnostics;
                    if (modelValidation == TerrainModelGeometryValidation::Valid)
                    {
                        validation = BuildTerrainMeshHeightData(geometry.m_positions, geometry.m_indices, *data, &diagnostics);
                    }
                    AZ::SystemTickBus::QueueFunction(
                        [weak,
                         generation,
                         preparationTicket,
                         model,
                         data,
                         modelValidation,
                         validation,
                         diagnostics = AZStd::move(diagnostics)]() mutable
                        {
                            if (auto source = weak.lock();
                                source && source->m_active &&
                                IsTerrainMeshHeightPreparationCurrent(
                                    generation, preparationTicket, source->m_generation, source->m_latestPreparationTicket) &&
                                source->m_model.GetId() == model.GetId())
                            {
                                if (modelValidation != TerrainModelGeometryValidation::Valid)
                                {
                                    source->Publish(TerrainMeshHeightDataStatus::InvalidModel, modelValidation);
                                }
                                else if (validation != TerrainMeshHeightValidation::Valid)
                                {
                                    source->Publish(
                                        TerrainMeshHeightDataStatus::InvalidGeometry,
                                        TerrainModelGeometryValidation::Valid,
                                        validation,
                                        {},
                                        AZStd::move(diagnostics));
                                }
                                else
                                {
                                    data->m_revision = ++s_nextMeshHeightRevision;
                                    source->Publish(
                                        TerrainMeshHeightDataStatus::Ready, TerrainModelGeometryValidation::Valid, validation, data);
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
            // A failure is a new lifecycle state and must retire every accepted
            // preparation that could otherwise complete afterward.
            const AZ::u64 generation = ++m_generation;
            AZ::SystemTickBus::QueueFunction(
                [weak, generation]()
                {
                    if (auto source = weak.lock(); source && source->m_active && source->m_generation == generation)
                    {
                        source->Publish(TerrainMeshHeightDataStatus::Error);
                    }
                });
        }

        void OnAssetReady(AZ::Data::Asset<AZ::Data::AssetData> asset) override
        {
            QueueReady(asset);
        }
        void OnAssetReloaded(AZ::Data::Asset<AZ::Data::AssetData> asset) override
        {
            QueueReady(asset);
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
                        source->Publish(TerrainMeshHeightDataStatus::Loading);
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
                            source->Publish(TerrainMeshHeightDataStatus::Missing);
                        }
                        else if (!source->m_model.IsReady())
                        {
                            source->StartModel();
                        }
                        else if (auto* reloader = AZ::Render::ModelReloaderSystemInterface::Get())
                        {
                            ++source->m_generation;
                            source->Publish(TerrainMeshHeightDataStatus::Loading);
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
        AZStd::weak_ptr<TerrainMeshHeightDataSource> m_weakSelf;
        AZ::Data::Asset<AZ::RPI::ModelAsset> m_model;
        AZ::Render::ModelReloadedEvent::Handler m_modelReloadedHandler;
        AZStd::atomic<AZ::u64> m_generation = 0;
        AZ::u64 m_latestPreparationTicket = 0;
        bool m_active = false;
    };

    TerrainMeshHeightDataCache::TerrainMeshHeightDataCache()
    {
        TerrainMeshHeightDataCacheInterface::Register(this);
    }

    TerrainMeshHeightDataCache::~TerrainMeshHeightDataCache()
    {
        TerrainMeshHeightDataCacheInterface::Unregister(this);
        for (const auto& [id, weak] : m_sources)
        {
            (void)id;
            if (auto source = weak.lock())
                source->Stop();
        }
    }

    TerrainMeshHeightDataCache::Handle TerrainMeshHeightDataCache::Acquire(const AZ::Data::AssetId& assetId)
    {
        if (!m_controlThread.Check() || !assetId.IsValid())
            return {};
        const auto info = GetAssetInfo(assetId);
        const AZ::Data::AssetId canonical = info.m_assetId.IsValid() ? info.m_assetId : assetId;
        for (auto iterator = m_sources.begin(); iterator != m_sources.end();)
        {
            if (iterator->second.expired())
                iterator = m_sources.erase(iterator);
            else
                ++iterator;
        }
        if (auto source = m_sources[canonical].lock())
            return source;
        auto source = AZStd::make_shared<TerrainMeshHeightDataSource>(canonical);
        m_sources[canonical] = source;
        source->Start();
        return source;
    }

    TerrainMeshHeightDataSnapshot TerrainMeshHeightDataCache::GetSnapshot(const Handle& handle)
    {
        return handle && handle->m_controlThread.Check() ? handle->m_snapshot : TerrainMeshHeightDataSnapshot{};
    }

    void TerrainMeshHeightDataCache::ConnectChangedHandler(const Handle& handle, ChangedEvent::Handler& handler)
    {
        if (handle && handle->m_controlThread.Check())
            handler.Connect(handle->m_changed);
    }
} // namespace TerrainCompositor
