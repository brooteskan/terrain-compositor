#include <TerrainCompositor/TerrainMeshCutoutDataCache.h>
#include "ModelAssetSource.h"
#include <TerrainCompositor/TerrainModelGeometry.h>

#include <Atom/RPI.Reflect/Model/ModelLodAsset.h>
#include <AzCore/Jobs/JobFunction.h>
#include <AzCore/Name/Name.h>

namespace TerrainCompositor
{
    namespace
    {
        AZ::u64 s_nextCutoutRevision = 0;

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
        : public Internal::ModelAssetSource<TerrainMeshCutoutDataSource, TerrainMeshCutoutDataStatus>
    {
        using Base = Internal::ModelAssetSource<TerrainMeshCutoutDataSource, TerrainMeshCutoutDataStatus>;
        friend Base;

    public:
        using Base::Base;
        ~TerrainMeshCutoutDataSource() { Stop(); }

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

        void OnReady(AZ::u64 generation, const AZ::Data::Asset<AZ::RPI::ModelAsset>& model)
        {
            if (!m_controlThread.Check() || generation != m_generation || model.GetId() != m_assetId || !model.IsReady())
            {
                return;
            }
            m_model = model;
            // Retire older preparations before Loading subscribers can reenter.
            const AZ::u64 preparationTicket = ++m_latestPreparationTicket;
            Publish(TerrainMeshCutoutDataStatus::Loading);
            const auto weak = m_weakSelf;
            AZ::Job* job = AZ::CreateJobFunction(
                [weak, generation, preparationTicket, model]() mutable
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
                        [weak, generation, preparationTicket, model, data, validation]()
                        {
                            if (auto source = weak.lock(); source && source->m_active && source->m_generation == generation &&
                                source->m_latestPreparationTicket == preparationTicket &&
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
            // Retire queued ready callbacks and completions before the control thread publishes the error.
            QueueStatus(TerrainMeshCutoutDataStatus::Error, ++m_generation);
        }

        AZ::u64 m_latestPreparationTicket = 0;
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
        return Internal::AcquireModelSource<TerrainMeshCutoutDataSource>(m_sources, assetId);
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
