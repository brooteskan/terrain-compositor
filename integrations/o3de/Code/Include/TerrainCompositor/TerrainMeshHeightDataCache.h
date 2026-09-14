#pragma once

#include <TerrainCompositor/Internal/AssetPreparation.h>

#include <AzCore/EBus/Event.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/smart_ptr/weak_ptr.h>
#include <TerrainCompositor/HeightmapControlThread.h>
#include <TerrainCompositor/TerrainCompositorTypeIds.h>
#include <TerrainCompositor/TerrainMeshHeightData.h>

namespace TerrainCompositor
{
    //! A lifecycle generation alone cannot order overlapping jobs accepted for
    //! one model lifecycle. Both values must still match at completion.
    inline bool IsTerrainMeshHeightPreparationCurrent(
        AZ::u64 expectedLifecycleGeneration,
        AZ::u64 expectedPreparationTicket,
        AZ::u64 currentLifecycleGeneration,
        AZ::u64 latestPreparationTicket)
    {
        return Internal::IsAssetPreparationCurrent(
            expectedLifecycleGeneration, expectedPreparationTicket, currentLifecycleGeneration, latestPreparationTicket);
    }

    class TerrainMeshHeightDataSource;

    class TerrainMeshHeightDataCache final
    {
    public:
        AZ_TYPE_INFO(TerrainMeshHeightDataCache, TerrainMeshHeightDataCacheTypeId);
        using Handle = AZStd::shared_ptr<TerrainMeshHeightDataSource>;
        using ChangedEvent = AZ::Event<const TerrainMeshHeightDataSnapshot&>;

        TerrainMeshHeightDataCache();
        ~TerrainMeshHeightDataCache();
        TerrainMeshHeightDataCache(const TerrainMeshHeightDataCache&) = delete;
        TerrainMeshHeightDataCache& operator=(const TerrainMeshHeightDataCache&) = delete;

        Handle Acquire(const AZ::Data::AssetId& assetId);
        static TerrainMeshHeightDataSnapshot GetSnapshot(const Handle& handle);
        static void ConnectChangedHandler(const Handle& handle, ChangedEvent::Handler& handler);

    private:
        HeightmapControlThread m_controlThread;
        AZStd::unordered_map<AZ::Data::AssetId, AZStd::weak_ptr<TerrainMeshHeightDataSource>> m_sources;
    };
    using TerrainMeshHeightDataCacheInterface = AZ::Interface<TerrainMeshHeightDataCache>;
} // namespace TerrainCompositor
