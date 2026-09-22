#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/std/function/function_template.h>
#include <TerrainCompositor/Internal/CacheLifecycle.h>

namespace TerrainCompositor::Internal
{
    // Owns one selected asset's subscription; prepared data remains owned by immutable snapshots.
    template<class Cache>
    class AssetSubscription
    {
    public:
        using Snapshot = decltype(Cache::GetSnapshot(typename Cache::Handle{}));

        AssetSubscription()
            : m_cacheChanged([this](bool available) { OnCacheAvailabilityChanged(available); })
        {
        }
        AssetSubscription(const AssetSubscription&) = delete;
        AssetSubscription& operator=(const AssetSubscription&) = delete;
        ~AssetSubscription() { Reset(); }

        void Reset()
        {
            ++m_generation;
            m_changed.Disconnect();
            m_cacheChanged.Disconnect();
            m_source.reset();
            m_selectedAssetId = {};
            m_target = nullptr;
            m_canPublish = {};
            m_onChanged = {};
        }

        const AZ::Data::AssetId& SelectedAssetId() const { return m_selectedAssetId; }

        // False identifies a missing cache so the caller can retain its role-specific diagnostic policy.
        template<class CanPublish, class Changed>
        bool Update(const AZ::Data::AssetId& assetId, Snapshot& target, CanPublish canPublish, Changed changed)
        {
            m_target = &target;
            m_canPublish = AZStd::move(canPublish);
            m_onChanged = AZStd::move(changed);
            if (!m_cacheChanged.IsConnected())
            {
                CacheLifecycle<Cache>::Connect(m_cacheChanged);
            }
            if (assetId == m_selectedAssetId && ((m_source && m_changed.IsConnected()) || !assetId.IsValid()))
            {
                return true;
            }
            ++m_generation;
            m_changed.Disconnect();
            m_source.reset();
            m_selectedAssetId = assetId;
            target = {};
            if (!assetId.IsValid())
            {
                return true;
            }
            return Acquire();
        }

    private:
        bool Acquire()
        {
            auto* cache = AZ::Interface<Cache>::Get();
            if (!cache)
            {
                if (m_target) m_target->m_status = decltype(m_target->m_status)::Error;
                return false;
            }
            const AZ::u64 generation = m_generation;
            m_source = cache->Acquire(m_selectedAssetId);
            m_changed = typename Cache::ChangedEvent::Handler(
                [this, generation](const Snapshot& snapshot)
                {
                    if (m_target && m_canPublish && m_canPublish() && generation == m_generation)
                    {
                        *m_target = snapshot;
                        if (m_onChanged) m_onChanged();
                    }
                });
            Cache::ConnectChangedHandler(m_source, m_changed);
            if (m_target) *m_target = Cache::GetSnapshot(m_source);
            return true;
        }

        void OnCacheAvailabilityChanged(bool available)
        {
            if (!m_target || !m_selectedAssetId.IsValid()) return;
            ++m_generation;
            m_changed.Disconnect();
            m_source.reset();
            if (available)
            {
                Acquire();
            }
            else
            {
                *m_target = {};
                m_target->m_status = decltype(m_target->m_status)::Error;
            }
            if (m_canPublish && m_canPublish() && m_onChanged) m_onChanged();
        }

        typename Cache::Handle m_source;
        typename Cache::ChangedEvent::Handler m_changed;
        typename CacheLifecycle<Cache>::Handler m_cacheChanged;
        AZ::Data::AssetId m_selectedAssetId;
        Snapshot* m_target = nullptr;
        AZStd::function<bool()> m_canPublish;
        AZStd::function<void()> m_onChanged;
        AZ::u64 m_generation = 0;
    };
}
