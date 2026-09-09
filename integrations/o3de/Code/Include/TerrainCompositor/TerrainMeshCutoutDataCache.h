#pragma once

#include <AzCore/EBus/Event.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/smart_ptr/weak_ptr.h>
#include <TerrainCompositor/HeightmapControlThread.h>
#include <TerrainCompositor/TerrainMeshCutoutData.h>
#include <TerrainCompositor/TerrainCompositorTypeIds.h>

namespace TerrainCompositor
{
    class TerrainMeshCutoutDataSource;

    class TerrainMeshCutoutDataCache final
    {
    public:
        AZ_TYPE_INFO(TerrainMeshCutoutDataCache, TerrainMeshCutoutDataCacheTypeId);
        using Handle = AZStd::shared_ptr<TerrainMeshCutoutDataSource>;
        using ChangedEvent = AZ::Event<const TerrainMeshCutoutDataSnapshot&>;

        TerrainMeshCutoutDataCache();
        ~TerrainMeshCutoutDataCache();
        TerrainMeshCutoutDataCache(const TerrainMeshCutoutDataCache&) = delete;
        TerrainMeshCutoutDataCache& operator=(const TerrainMeshCutoutDataCache&) = delete;

        Handle Acquire(const AZ::Data::AssetId& assetId);
        static TerrainMeshCutoutDataSnapshot GetSnapshot(const Handle& handle);
        static void ConnectChangedHandler(const Handle& handle, ChangedEvent::Handler& handler);

    private:
        HeightmapControlThread m_controlThread;
        AZStd::unordered_map<AZ::Data::AssetId, AZStd::weak_ptr<TerrainMeshCutoutDataSource>> m_sources;
    };
    using TerrainMeshCutoutDataCacheInterface = AZ::Interface<TerrainMeshCutoutDataCache>;
} // namespace TerrainCompositor
