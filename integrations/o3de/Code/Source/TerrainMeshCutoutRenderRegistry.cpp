#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>

#include <TerrainCompositor/HeightmapStampIdentity.h>

#include <algorithm>

namespace TerrainCompositor
{
    TerrainMeshCutoutRenderRegistry::TerrainMeshCutoutRenderRegistry()
    {
        AZ::Interface<TerrainMeshCutoutRenderRegistry>::Register(this);
    }

    TerrainMeshCutoutRenderRegistry::~TerrainMeshCutoutRenderRegistry()
    {
        AZ::Interface<TerrainMeshCutoutRenderRegistry>::Unregister(this);
        AZStd::vector<TerrainMeshCutoutRenderChannelPtr> retired;
        {
            std::lock_guard lock(m_updateMutex);
            retired.reserve(m_sceneChannels.size());
            for (const auto& [key, channel] : m_sceneChannels)
            {
                (void)key;
                std::lock_guard publicationLock(channel->m_publicationMutex);
                channel->m_active = false;
                channel->m_snapshot.store({}, std::memory_order_release);
                channel->m_activation.store({}, std::memory_order_release);
                retired.push_back(channel);
            }
            m_sceneChannels.clear();
            m_byComposition.clear();
        }
        for (const auto& channel : retired)
        {
            channel->m_activationChanged.Signal({});
            channel->m_materialChanged.Signal({});
        }
    }

    TerrainMeshCutoutRenderChannelPtr TerrainMeshCutoutRenderRegistry::AcquireSceneChannel(const void* sceneKey)
    {
        std::lock_guard lock(m_updateMutex);
        auto& channel = m_sceneChannels[sceneKey];
        if (!channel)
        {
            channel = std::make_shared<TerrainMeshCutoutRenderChannel>();
            RebuildSnapshot(sceneKey);
        }
        return channel;
    }

    TerrainMeshCutoutRenderChannelPtr TerrainMeshCutoutRenderRegistry::FindSceneChannel(const void* sceneKey) const
    {
        std::lock_guard lock(m_updateMutex);
        const auto found = m_sceneChannels.find(sceneKey);
        return found != m_sceneChannels.end() ? found->second : TerrainMeshCutoutRenderChannelPtr{};
    }

    bool TerrainMeshCutoutRenderRegistry::Publish(
        const void* sceneKey,
        const AZ::Uuid& compositionSession,
        AZStd::vector<PreparedTerrainMeshCutout> cutouts,
        TerrainRenderGeometryQuery renderGeometryQuery,
        AZ::u64 compositionRevision,
        AZStd::vector<PreparedTerrainMeshHeightGap> meshHeightGaps)
    {
        std::lock_guard lock(m_updateMutex);
        const auto existing = m_byComposition.find(compositionSession);
        const bool hadExisting = existing != m_byComposition.end();
        if (existing != m_byComposition.end() && compositionRevision != 0 && existing->second.m_compositionRevision >= compositionRevision)
        {
            return existing->second.m_compositionRevision == compositionRevision;
        }
        const void* previousSceneKey = hadExisting ? existing->second.m_sceneKey : nullptr;
        m_byComposition[compositionSession] = CompositionEntry{
            sceneKey, compositionRevision, AZStd::move(cutouts), AZStd::move(meshHeightGaps), AZStd::move(renderGeometryQuery)
        };
        if (hadExisting && previousSceneKey != sceneKey)
        {
            RebuildSnapshot(previousSceneKey);
        }
        RebuildSnapshot(sceneKey);
        return true;
    }

    void TerrainMeshCutoutRenderRegistry::Remove(const AZ::Uuid& compositionSession)
    {
        std::lock_guard lock(m_updateMutex);
        const auto found = m_byComposition.find(compositionSession);
        if (found != m_byComposition.end())
        {
            const void* sceneKey = found->second.m_sceneKey;
            m_byComposition.erase(found);
            RebuildSnapshot(sceneKey);
        }
    }

    bool TerrainMeshCutoutRenderRegistry::ActivateGaps(const void* sceneKey,
        const TerrainMeshCutoutRenderSnapshotPtr& expected, AZStd::vector<PreparedTerrainMeshHeightGap> admitted)
    {
        TerrainMeshCutoutRenderChannelPtr target;
        TerrainMeshHeightGapActivationPtr activation;
        {
            std::lock_guard lock(m_updateMutex);
            const auto channel = m_sceneChannels.find(sceneKey);
            if (!expected || channel == m_sceneChannels.end() || channel->second->m_snapshot.load(std::memory_order_acquire) != expected) return false;
            target = channel->second;
            auto replacement = std::make_shared<TerrainMeshHeightGapActivation>();
            replacement->m_revision = expected->m_revision;
            replacement->m_gaps = AZStd::move(admitted);
            activation = AZStd::move(replacement);
            target->m_activation.store(activation, std::memory_order_release);
        }
        target->m_activationChanged.Signal(activation);
        return true;
    }

    void TerrainMeshCutoutRenderRegistry::ClearGapActivation(const void* sceneKey)
    {
        TerrainMeshCutoutRenderChannelPtr target;
        {
            std::lock_guard lock(m_updateMutex);
            const auto channel = m_sceneChannels.find(sceneKey);
            if (channel != m_sceneChannels.end())
            {
                target = channel->second;
                if (!target->m_activation.exchange({}, std::memory_order_acq_rel)) target.reset();
            }
        }
        if (target) target->m_activationChanged.Signal({});
    }

    void TerrainMeshCutoutRenderRegistry::RemoveScene(const void* sceneKey, bool removeRegistrations)
    {
        TerrainMeshCutoutRenderChannelPtr retired;
        {
            std::lock_guard lock(m_updateMutex);
            const auto found = m_sceneChannels.find(sceneKey);
            if (found != m_sceneChannels.end())
            {
                retired = found->second;
                std::lock_guard publicationLock(retired->m_publicationMutex);
                retired->m_active = false;
                retired->m_snapshot.store({}, std::memory_order_release);
                retired->m_activation.store({}, std::memory_order_release);
                m_sceneChannels.erase(found);
            }
            if (removeRegistrations)
                AZStd::erase_if(m_byComposition, [sceneKey](const auto& entry) { return entry.second.m_sceneKey == sceneKey; });
        }
        if (retired)
        {
            retired->m_activationChanged.Signal({});
            retired->m_materialChanged.Signal({});
        }
    }

    void TerrainMeshCutoutRenderRegistry::RebuildSnapshot(const void* sceneKey)
    {
        auto& channel = m_sceneChannels[sceneKey];
        if (!channel)
        {
            channel = std::make_shared<TerrainMeshCutoutRenderChannel>();
        }
        auto replacement = std::make_shared<TerrainMeshCutoutRenderSnapshot>();
        replacement->m_revision = ++m_revision;
        size_t total = 0;
        size_t totalGaps = 0;
        for (const auto& [session, entry] : m_byComposition)
        {
            (void)session;
            if (entry.m_sceneKey == sceneKey)
            {
                total += entry.m_cutouts.size();
                totalGaps += entry.m_meshHeightGaps.size();
            }
        }
        replacement->m_cutouts.reserve(total);
        replacement->m_meshHeightGaps.reserve(totalGaps);
        replacement->m_renderGeometryQueries.reserve(m_byComposition.size());
        replacement->m_compositionGenerations.reserve(m_byComposition.size());
        for (const auto& [session, entry] : m_byComposition)
        {
            (void)session;
            if (entry.m_sceneKey == sceneKey)
            {
                replacement->m_cutouts.insert(replacement->m_cutouts.end(), entry.m_cutouts.begin(), entry.m_cutouts.end());
                replacement->m_meshHeightGaps.insert(
                    replacement->m_meshHeightGaps.end(), entry.m_meshHeightGaps.begin(), entry.m_meshHeightGaps.end());
                replacement->m_compositionGenerations.emplace_back(session, entry.m_compositionRevision);
                if (entry.m_renderGeometryQuery.m_getTerrainExists)
                {
                    replacement->m_renderGeometryQueries.push_back(entry.m_renderGeometryQuery);
                    auto& query = replacement->m_renderGeometryQueries.back();
                    query.m_compositionSession = session;
                    query.m_compositionRevision = entry.m_compositionRevision;
                }
            }
        }
        AZStd::sort(
            replacement->m_cutouts.begin(),
            replacement->m_cutouts.end(),
            [](const auto& left, const auto& right)
            {
                return StampPriorityLess(left.m_priority, left.m_stableOrderKey, right.m_priority, right.m_stableOrderKey);
            });
        AZStd::sort(
            replacement->m_meshHeightGaps.begin(),
            replacement->m_meshHeightGaps.end(),
            [](const auto& left, const auto& right)
            {
                return StampPriorityLess(left.m_priority, left.m_stableOrderKey, right.m_priority, right.m_stableOrderKey);
            });
        AZStd::sort(
            replacement->m_compositionGenerations.begin(),
            replacement->m_compositionGenerations.end(),
            [](const auto& left, const auto& right)
            {
                return left.first < right.first;
            });
        std::lock_guard publicationLock(channel->m_publicationMutex);
        channel->m_snapshot.store(TerrainMeshCutoutRenderSnapshotPtr(AZStd::move(replacement)), std::memory_order_release);
    }
} // namespace TerrainCompositor
