#pragma once

#include <AzCore/Component/TickBus.h>
#include <TerrainCompositor/TerrainCompositionProviderBinding.h>
#include <TerrainSystem/TerrainSystemBus.h>

namespace TerrainCompositor::Internal
{
    //! Owns provider transitions; the adapter retains its query and notification bus connections.
    template<class Provider>
    class ProviderLifecycle : public TerrainCompositionProviderBinding, private AZ::SystemTickBus::Handler
    {
        using ChangeMask = AzFramework::Terrain::TerrainDataNotifications::TerrainDataChangedMask;
    public:
        ProviderLifecycle(Provider& provider, ChangeMask refreshMask)
            : m_provider(provider), m_refreshMask(refreshMask) {}

        void Start(AZ::EntityId region)
        {
            if (!PrepareToStart()) { return; }
            Stop();
            Activate(region, m_provider.m_configuration.m_compositionEntityId);
            m_provider.ConnectProvider();
            AZ::SystemTickBus::Handler::BusConnect();
            RefreshArea();
        }

        void Stop()
        {
            if (!BeginStop()) { return; }
            // Drain queries before changing routing; invalidate the old region before clearing it.
            m_provider.DisconnectProvider();
            AZ::SystemTickBus::Handler::BusDisconnect();
            RefreshArea();
            Clear();
            m_provider.ClearProvider();
        }

        void RefreshArea() const
        {
            const AZ::EntityId region = GetTerrainRegionEntityId();
            if (region.IsValid())
            {
                Terrain::TerrainSystemServiceRequestBus::Broadcast(
                    &Terrain::TerrainSystemServiceRequests::RefreshArea, region, m_refreshMask);
            }
        }

        template<class Configuration>
        bool ReadConfiguration(const AZ::ComponentConfig* input, Configuration& configuration)
        {
            if (!CheckControlThread()) { return false; }
            if (const auto* value = azrtti_cast<const Configuration*>(input))
            {
                const bool changed = value->m_compositionEntityId != configuration.m_compositionEntityId;
                configuration = *value;
                if (changed) { Restart(); }
                return true;
            }
            return false;
        }

        template<class Configuration>
        bool WriteConfiguration(AZ::ComponentConfig* output, const Configuration& configuration) const
        {
            if (!CheckControlThread()) { return false; }
            if (auto* destination = azrtti_cast<Configuration*>(output))
            {
                *destination = configuration;
                return true;
            }
            return false;
        }

        template<class QueryBus>
        AZStd::string GetStatusMessage(const char* inactive, const char* ready) const
        {
            if (!CheckControlThread()) { return "Unavailable off the control thread."; }
            if (!IsActive()) { return inactive; }
            if (!m_provider.m_configuration.m_compositionEntityId.IsValid()) { return "Select a Terrain Composition entity."; }
            if (GetCompositionAddress().first.IsNull()) { return "Waiting for entity context ownership."; }
            if (!QueryBus::HasHandlers(GetCompositionAddress()))
            {
                return "Terrain Composition is unavailable in this entity context.";
            }
            return ready;
        }

    private:
        void Restart()
        {
            if (IsActive())
            {
                const AZ::EntityId region = GetTerrainRegionEntityId();
                Stop();
                Start(region);
            }
        }

        void OnSystemTick() override
        {
            if (HasOwningContextChanged()) { Restart(); }
        }

        Provider& m_provider;
        const ChangeMask m_refreshMask;
    };
}
