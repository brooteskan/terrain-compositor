#pragma once

#include <AzCore/EBus/Event.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/smart_ptr/weak_ptr.h>
#include <TerrainCompositor/HeightmapData.h>
#include <TerrainCompositor/HeightmapControlThread.h>
#include <TerrainCompositor/TerrainCompositorTypeIds.h>

namespace TerrainCompositor
{
    class HeightmapDataSource;

    //! Project-system-owned cache of images, not stamps. All public operations and change handlers run on the main thread.
    //! Acquire/Connect/GetSnapshot are control-plane operations, never height-query APIs.
    class HeightmapDataCache final
    {
    public:
        AZ_TYPE_INFO(HeightmapDataCache, HeightmapDataCacheTypeId);
        using Handle = AZStd::shared_ptr<HeightmapDataSource>;
        using ChangedEvent = AZ::Event<const HeightmapDataSnapshot&>;

        HeightmapDataCache();
        ~HeightmapDataCache();
        HeightmapDataCache(const HeightmapDataCache&) = delete;
        HeightmapDataCache& operator=(const HeightmapDataCache&) = delete;

        Handle Acquire(const AZ::Data::AssetId& assetId);
        static HeightmapDataSnapshot GetSnapshot(const Handle& handle);
        static void ConnectChangedHandler(const Handle& handle, ChangedEvent::Handler& handler);

    private:
        HeightmapControlThread m_controlThread;
        // Weak entries release image/mip assets after the last subscriber leaves. Query snapshots keep their own samples alive.
        AZStd::unordered_map<AZ::Data::AssetId, AZStd::weak_ptr<HeightmapDataSource>> m_sources;
    };
    using HeightmapDataCacheInterface = AZ::Interface<HeightmapDataCache>;
} // namespace TerrainCompositor
