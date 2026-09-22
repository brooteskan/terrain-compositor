#include <TerrainCompositor/TerrainMeshHeightDataCache.h>
#include <TerrainCompositor/Internal/CacheLifecycle.h>
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
            PublishPreparedSnapshot(data, s_nextMeshHeightRevision, [&](AZ::u64 revision)
            {
                return TerrainMeshHeightDataSnapshot{ status, modelValidation, validation, revision, AZStd::move(data), m_assetId, AZStd::move(diagnostics) };
            });
        }

        void OnReady(AZ::u64 generation, const AZ::Data::Asset<AZ::RPI::ModelAsset>& model)
        {
            PrepareModel(generation, model, Internal::PreparationTicketTiming::AfterLoading, s_nextMeshHeightPreparationTicket,
                [](const AZ::Data::Asset<AZ::RPI::ModelAsset>& model)
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
                    return [data, modelValidation, validation, diagnostics = AZStd::move(diagnostics)](TerrainMeshHeightDataSource& source) mutable
                    {
                        if (modelValidation != TerrainModelGeometryValidation::Valid)
                        {
                            source.Publish(TerrainMeshHeightDataStatus::InvalidModel, modelValidation);
                        }
                        else if (validation != TerrainMeshHeightValidation::Valid)
                        {
                            source.Publish(
                                TerrainMeshHeightDataStatus::InvalidGeometry,
                                TerrainModelGeometryValidation::Valid,
                                validation,
                                {},
                                AZStd::move(diagnostics));
                        }
                        else
                        {
                            data->m_revision = ++s_nextMeshHeightRevision;
                            source.Publish(
                                TerrainMeshHeightDataStatus::Ready, TerrainModelGeometryValidation::Valid, validation, data);
                        }
                    };
                });
        }

    };

    TerrainMeshHeightDataCache::TerrainMeshHeightDataCache()
    {
        TerrainMeshHeightDataCacheInterface::Register(this);
        Internal::CacheLifecycle<TerrainMeshHeightDataCache>::Signal(true);
    }

    TerrainMeshHeightDataCache::~TerrainMeshHeightDataCache()
    {
        TerrainMeshHeightDataCacheInterface::Unregister(this);
        Internal::CacheLifecycle<TerrainMeshHeightDataCache>::Signal(false);
        Internal::StopAssetSources(m_sources);
    }

    TerrainMeshHeightDataCache::Handle TerrainMeshHeightDataCache::Acquire(const AZ::Data::AssetId& assetId)
    {
        if (!m_controlThread.Check() || !assetId.IsValid())
            return {};
        return Internal::AcquireAssetSource<TerrainMeshHeightDataSource>(m_sources, assetId);
    }

    TerrainMeshHeightDataSnapshot TerrainMeshHeightDataCache::GetSnapshot(const Handle& handle)
    {
        return Internal::GetAssetSourceSnapshot<TerrainMeshHeightDataSnapshot>(handle);
    }

    void TerrainMeshHeightDataCache::ConnectChangedHandler(const Handle& handle, ChangedEvent::Handler& handler)
    {
        Internal::ConnectAssetSourceChanged(handle, handler);
    }
} // namespace TerrainCompositor
