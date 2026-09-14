#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <AzFramework/Asset/AssetCatalogBus.h>

namespace TerrainCompositor::Internal
{
    inline AZ::Data::AssetInfo GetAssetInfo(const AZ::Data::AssetId& id)
    {
        AZ::Data::AssetInfo info;
        AZ::Data::AssetCatalogRequestBus::BroadcastResult(info, &AZ::Data::AssetCatalogRequestBus::Events::GetAssetInfoById, id);
        return info;
    }

    template<class Source, class Sources>
    AZStd::shared_ptr<Source> AcquireAssetSource(Sources& sources, const AZ::Data::AssetId& assetId)
    {
        const auto info = GetAssetInfo(assetId);
        const AZ::Data::AssetId canonical = info.m_assetId.IsValid() ? info.m_assetId : assetId;
        for (auto iterator = sources.begin(); iterator != sources.end();)
        {
            if (iterator->second.expired())
                iterator = sources.erase(iterator);
            else
                ++iterator;
        }
        if (auto source = sources[canonical].lock())
            return source;
        auto source = AZStd::make_shared<Source>(canonical);
        sources[canonical] = source;
        source->Start();
        return source;
    }

    template<class Sources>
    void StopAssetSources(const Sources& sources)
    {
        for (const auto& [id, weak] : sources)
        {
            if (auto source = weak.lock()) source->Stop();
        }
    }

    template<class Snapshot, class Handle>
    Snapshot GetAssetSourceSnapshot(const Handle& handle)
    {
        return handle && handle->m_controlThread.Check() ? handle->m_snapshot : Snapshot{};
    }

    template<class Handle, class Handler>
    void ConnectAssetSourceChanged(const Handle& handle, Handler& handler)
    {
        if (handle && handle->m_controlThread.Check()) handler.Connect(handle->m_changed);
    }
}
