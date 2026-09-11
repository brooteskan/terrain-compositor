#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Interface/Interface.h>

namespace TerrainCompositor::Internal
{
    // Owns one selected asset's subscription; prepared data remains owned by immutable snapshots.
    template<class Cache>
    class AssetSubscription
    {
    public:
        using Snapshot = decltype(Cache::GetSnapshot(typename Cache::Handle{}));

        AssetSubscription() = default;
        AssetSubscription(const AssetSubscription&) = delete;
        AssetSubscription& operator=(const AssetSubscription&) = delete;
        ~AssetSubscription() { Reset(); }

        void Reset()
        {
            ++m_generation;
            m_changed.Disconnect();
            m_source.reset();
            m_selectedAssetId = {};
        }

        const AZ::Data::AssetId& SelectedAssetId() const { return m_selectedAssetId; }
        bool NeedsRetry() const { return m_selectedAssetId.IsValid() && !m_changed.IsConnected(); }

        // False identifies a missing cache so the caller can retain its role-specific diagnostic policy.
        template<class CanPublish, class Changed>
        bool Update(const AZ::Data::AssetId& assetId, Snapshot& target, CanPublish canPublish, Changed changed)
        {
            if (assetId == m_selectedAssetId && ((m_source && m_changed.IsConnected()) || !assetId.IsValid()))
            {
                return true;
            }
            Reset();
            m_selectedAssetId = assetId;
            target = {};
            if (!assetId.IsValid())
            {
                return true;
            }
            auto* cache = AZ::Interface<Cache>::Get();
            if (!cache)
            {
                target.m_status = decltype(target.m_status)::Error;
                return false;
            }
            const AZ::u64 generation = m_generation;
            m_source = cache->Acquire(assetId);
            m_changed = typename Cache::ChangedEvent::Handler(
                [this, generation, &target, canPublish, changed](const Snapshot& snapshot)
                {
                    if (canPublish() && generation == m_generation)
                    {
                        target = snapshot;
                        changed();
                    }
                });
            Cache::ConnectChangedHandler(m_source, m_changed);
            target = Cache::GetSnapshot(m_source);
            return true;
        }

    private:
        typename Cache::Handle m_source;
        typename Cache::ChangedEvent::Handler m_changed;
        AZ::Data::AssetId m_selectedAssetId;
        AZ::u64 m_generation = 0;
    };
}
