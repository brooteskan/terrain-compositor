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
            PublishPreparedSnapshot(data, s_nextCutoutRevision, [&](AZ::u64 revision)
            {
                return TerrainMeshCutoutDataSnapshot{ status, validation, revision, AZStd::move(data), m_assetId };
            });
        }

        void OnReady(AZ::u64 generation, const AZ::Data::Asset<AZ::RPI::ModelAsset>& model)
        {
            PrepareModel(generation, model, Internal::PreparationTicketTiming::BeforeLoading, m_latestPreparationTicket,
                [](const AZ::Data::Asset<AZ::RPI::ModelAsset>& model)
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
                    return [data, validation](TerrainMeshCutoutDataSource& source) mutable
                    {
                        if (validation == TerrainMeshCutoutValidation::Valid)
                        {
                            data->m_revision = ++s_nextCutoutRevision;
                            source.Publish(TerrainMeshCutoutDataStatus::Ready, validation, data);
                        }
                        else
                        {
                            source.Publish(TerrainMeshCutoutDataStatus::InvalidGeometry, validation);
                        }
                    };
                });
        }

    };

    TerrainMeshCutoutDataCache::TerrainMeshCutoutDataCache()
    {
        TerrainMeshCutoutDataCacheInterface::Register(this);
    }

    TerrainMeshCutoutDataCache::~TerrainMeshCutoutDataCache()
    {
        TerrainMeshCutoutDataCacheInterface::Unregister(this);
        Internal::StopAssetSources(m_sources);
    }

    TerrainMeshCutoutDataCache::Handle TerrainMeshCutoutDataCache::Acquire(const AZ::Data::AssetId& assetId)
    {
        if (!m_controlThread.Check() || !assetId.IsValid())
            return {};
        return Internal::AcquireAssetSource<TerrainMeshCutoutDataSource>(m_sources, assetId);
    }

    TerrainMeshCutoutDataSnapshot TerrainMeshCutoutDataCache::GetSnapshot(const Handle& handle)
    {
        return Internal::GetAssetSourceSnapshot<TerrainMeshCutoutDataSnapshot>(handle);
    }

    void TerrainMeshCutoutDataCache::ConnectChangedHandler(const Handle& handle, ChangedEvent::Handler& handler)
    {
        Internal::ConnectAssetSourceChanged(handle, handler);
    }
} // namespace TerrainCompositor
