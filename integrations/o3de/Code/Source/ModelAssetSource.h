#pragma once

#include "AssetSource.h"
#include <AzCore/Jobs/JobFunction.h>
#include <TerrainCompositor/Internal/AssetPreparation.h>

#include <TerrainCompositor/HeightmapControlThread.h>
#include <Atom/Feature/Mesh/ModelReloaderSystemInterface.h>
#include <Atom/RPI.Reflect/Model/ModelAsset.h>
#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/parallel/atomic.h>
#include <AzCore/std/smart_ptr/enable_shared_from_this.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <AzFramework/Asset/AssetCatalogBus.h>

namespace TerrainCompositor::Internal
{
    enum class PreparationTicketTiming { BeforeLoading, AfterLoading };

    template<class Source, class Status>
    class ModelAssetSource
        : public AZStd::enable_shared_from_this<Source>
        , private AZ::Data::AssetBus::Handler
        , private AzFramework::AssetCatalogEventBus::Handler
    {
    public:
        explicit ModelAssetSource(AZ::Data::AssetId assetId)
            : m_assetId(assetId)
            , m_modelReloadedHandler(
                  [this](const AZ::Data::Asset<AZ::RPI::ModelAsset>& model)
                  {
                      QueueReady(model);
                  })
        {
        }

        void Start()
        {
            if (!m_controlThread.Check())
                return;
            m_active = true;
            m_weakSelf = this->shared_from_this();
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
            Self().m_changed.DisconnectAllHandlers();
        }

        HeightmapControlThread m_controlThread;

    protected:
        // Keep each role's historical ticket timing visible. Loading can pump the
        // completion queue or accept another ready event before this call resumes.
        template<class Prepare>
        void PrepareModel(AZ::u64 generation, const AZ::Data::Asset<AZ::RPI::ModelAsset>& model,
            PreparationTicketTiming timing, AZ::u64& ticketCounter, Prepare prepare)
        {
            if (!m_controlThread.Check() || generation != m_generation || model.GetId() != m_assetId || !model.IsReady())
                return;
            m_model = model;
            AZ::u64 ticket = 0;
            const auto advance = [&] { ticket = ++ticketCounter; m_latestPreparationTicket = ticket; };
            if (timing == PreparationTicketTiming::BeforeLoading) advance();
            Self().Publish(Status::Loading);
            if (timing == PreparationTicketTiming::AfterLoading) advance();
            const auto weak = m_weakSelf;
            AZ::Job* job = AZ::CreateJobFunction([weak, generation, ticket, model, prepare]() mutable
            {
                auto complete = prepare(model);
                AZ::SystemTickBus::QueueFunction([weak, generation, ticket, model, complete = AZStd::move(complete)]() mutable
                {
                    if (auto source = weak.lock(); source && source->m_active &&
                        IsAssetPreparationCurrent(generation, ticket, source->m_generation, source->m_latestPreparationTicket) &&
                        source->m_model.GetId() == model.GetId())
                    {
                        complete(*source);
                    }
                });
            }, true);
            job->Start();
        }

        template<class Data, class MakeSnapshot>
        void PublishPreparedSnapshot(const Data& data, AZ::u64& revisionCounter, MakeSnapshot makeSnapshot)
        {
            if (!m_active) return;
            const AZ::u64 revision = data ? data->m_revision : ++revisionCounter;
            Self().m_snapshot = makeSnapshot(revision);
            const auto snapshot = Self().m_snapshot;
            Self().m_changed.Signal(snapshot);
        }

        void QueueFailure()
        {
            // Retire ready callbacks and completions before the queued error notification.
            QueueStatus(Status::Error, ++m_generation);
        }

        void QueueStatus(Status status, AZ::u64 generation)
        {
            const auto weak = m_weakSelf;
            AZ::SystemTickBus::QueueFunction([weak, generation, status]()
            {
                if (auto source = weak.lock(); source && source->m_active && source->m_generation == generation)
                {
                    source->Publish(status);
                }
            });
        }

        const AZ::Data::AssetId m_assetId;
        AZStd::weak_ptr<Source> m_weakSelf;
        AZ::Data::Asset<AZ::RPI::ModelAsset> m_model;
        AZStd::atomic<AZ::u64> m_generation = 0;
        bool m_active = false;
        AZ::u64 m_latestPreparationTicket = 0;

    private:
        Source& Self() { return *static_cast<Source*>(this); }

        void StartModel()
        {
            ++m_generation;
            AZ::Data::AssetBus::Handler::BusDisconnect();
            m_modelReloadedHandler.Disconnect();
            m_model.Reset();
            const auto info = GetAssetInfo(m_assetId);
            if (!info.m_assetId.IsValid())
            {
                Self().Publish(Status::Missing);
                return;
            }
            if (info.m_assetType != azrtti_typeid<AZ::RPI::ModelAsset>())
            {
                Self().Publish(Status::Unsupported);
                return;
            }
            Self().Publish(Status::Loading);
            m_model =
                AZ::Data::AssetManager::Instance().GetAsset<AZ::RPI::ModelAsset>(info.m_assetId, AZ::Data::AssetLoadBehavior::PreLoad);
            AZ::Data::AssetBus::Handler::BusConnect(info.m_assetId);
            if (!m_model.Get())
            {
                Self().Publish(Status::Error);
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
            QueueStatus(Status::Loading, ++m_generation);
        }

        void OnAssetError([[maybe_unused]] AZ::Data::Asset<AZ::Data::AssetData> asset) override
        {
            Self().QueueFailure();
        }

        void OnAssetReloadError([[maybe_unused]] AZ::Data::Asset<AZ::Data::AssetData> asset) override
        {
            Self().QueueFailure();
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
                            source->Publish(Status::Missing);
                        }
                        else if (!source->m_model.IsReady())
                        {
                            source->StartModel();
                        }
                        else if (auto* reloader = AZ::Render::ModelReloaderSystemInterface::Get())
                        {
                            ++source->m_generation;
                            source->Publish(Status::Loading);
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

        AZ::Render::ModelReloadedEvent::Handler m_modelReloadedHandler;
    };


}
