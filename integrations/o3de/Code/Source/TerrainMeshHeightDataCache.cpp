#include <TerrainCompositor/TerrainMeshHeightDataCache.h>
#include "ModelAssetSource.h"

#include <AzCore/Jobs/JobFunction.h>

namespace TerrainCompositor
{
    namespace
    {
        AZ::u64 s_nextMeshHeightRevision = 0;
        AZ::u64 s_nextMeshHeightPreparationTicket = 0;

    } // namespace

    class TerrainMeshHeightDataSource final
        : public Internal::ModelAssetSource<TerrainMeshHeightDataSource, TerrainMeshHeightDataStatus>
    {
        using Base = Internal::ModelAssetSource<TerrainMeshHeightDataSource, TerrainMeshHeightDataStatus>;
        friend Base;

    public:
        using Base::Base;
        ~TerrainMeshHeightDataSource() { Stop(); }

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
            QueueStatus(TerrainMeshHeightDataStatus::Error, ++m_generation);
        }

        AZ::u64 m_latestPreparationTicket = 0;
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
        return Internal::AcquireModelSource<TerrainMeshHeightDataSource>(m_sources, assetId);
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
