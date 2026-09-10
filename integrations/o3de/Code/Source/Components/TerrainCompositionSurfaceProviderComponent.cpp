#include <TerrainCompositor/Components/TerrainCompositionSurfaceProviderComponent.h>
#include "../ComponentConfiguration.h"

#include "TerrainCompositionQueryHelpers.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <TerrainSystem/TerrainSystemBus.h>

namespace TerrainCompositor
{
    AZ_COMPONENT_IMPL(TerrainCompositionSurfaceProviderComponent, "TerrainCompositionSurfaceProviderComponent",
        TerrainCompositionSurfaceProviderComponentTypeId, AzFramework::EditorEntityEvents);

    void TerrainCompositionSurfaceProviderConfig::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection() &&
                serialize->FindClassData(azrtti_typeid<TerrainCompositionSurfaceProviderConfig>()))
            {
                return;
            }
            serialize->Class<TerrainCompositionSurfaceProviderConfig, AZ::ComponentConfig>()
                ->Version(1)
                ->Field("CompositionEntityId", &TerrainCompositionSurfaceProviderConfig::m_compositionEntityId);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<TerrainCompositionSurfaceProviderConfig>(
                    "Terrain Composition Surface Provider Configuration", "Composition reference for this terrain region.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                    ->DataElement(AZ::Edit::UIHandlers::Default,
                        &TerrainCompositionSurfaceProviderConfig::m_compositionEntityId,
                        "Terrain Composition", "Composition whose palette and surface stamps provide this region's weights.")
                    ->Attribute(AZ::Edit::Attributes::RequiredService, AZ_CRC_CE("TerrainCompositionService"));
            }
        }
    }

    void TerrainCompositionSurfaceProviderComponent::Reflect(AZ::ReflectContext* context)
    {
        TerrainCompositionSurfaceProviderConfig::Reflect(context);
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<TerrainCompositionSurfaceProviderComponent, AZ::Component>()
                ->Version(1)
                ->Field("Configuration", &TerrainCompositionSurfaceProviderComponent::m_configuration);
        }
    }

    void TerrainCompositionSurfaceProviderComponent::GetProvidedServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        services.push_back(AZ_CRC_CE("TerrainSurfaceProviderService"));
    }

    void TerrainCompositionSurfaceProviderComponent::GetIncompatibleServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        services.push_back(AZ_CRC_CE("TerrainSurfaceProviderService"));
    }

    void TerrainCompositionSurfaceProviderComponent::GetRequiredServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        services.push_back(AZ_CRC_CE("TerrainAreaService"));
    }

    void TerrainCompositionSurfaceProviderComponent::GetDependentServices(
        [[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& services)
    {
    }

    TerrainCompositionSurfaceProviderComponent::TerrainCompositionSurfaceProviderComponent(
        const TerrainCompositionSurfaceProviderConfig& configuration)
        : m_configuration(configuration)
    {
    }

    void TerrainCompositionSurfaceProviderComponent::Activate()
    {
        StartProvider(GetEntityId());
    }

    void TerrainCompositionSurfaceProviderComponent::Deactivate()
    {
        StopProvider();
    }

    void TerrainCompositionSurfaceProviderComponent::EditorActivate(AZ::EntityId entityId)
    {
        StartProvider(entityId);
    }

    void TerrainCompositionSurfaceProviderComponent::EditorDeactivate([[maybe_unused]] AZ::EntityId entityId)
    {
        StopProvider();
    }

    void TerrainCompositionSurfaceProviderComponent::StartProvider(AZ::EntityId terrainRegionEntityId)
    {
        if (!m_binding.PrepareToStart())
        {
            return;
        }
        StopProvider();
        m_binding.Activate(terrainRegionEntityId, m_configuration.m_compositionEntityId);
        Terrain::TerrainAreaSurfaceRequestBus::Handler::BusConnect(terrainRegionEntityId);
        AZ::SystemTickBus::Handler::BusConnect();
        RefreshArea();
    }

    void TerrainCompositionSurfaceProviderComponent::StopProvider()
    {
        if (!m_binding.BeginStop())
        {
            return;
        }
        // Shared-dispatch disconnect drains all terrain surface queries before query routing changes.
        Terrain::TerrainAreaSurfaceRequestBus::Handler::BusDisconnect();
        AZ::SystemTickBus::Handler::BusDisconnect();
        RefreshArea();
        m_binding.Clear();
    }

    void TerrainCompositionSurfaceProviderComponent::RestartProvider()
    {
        if (m_binding.IsActive())
        {
            const AZ::EntityId terrainRegionEntityId = m_binding.GetTerrainRegionEntityId();
            StopProvider();
            StartProvider(terrainRegionEntityId);
        }
    }

    void TerrainCompositionSurfaceProviderComponent::RefreshArea() const
    {
        const AZ::EntityId terrainRegionEntityId = m_binding.GetTerrainRegionEntityId();
        if (terrainRegionEntityId.IsValid())
        {
            Terrain::TerrainSystemServiceRequestBus::Broadcast(
                &Terrain::TerrainSystemServiceRequests::RefreshArea, terrainRegionEntityId,
                AzFramework::Terrain::TerrainDataNotifications::TerrainDataChangedMask::SurfaceData);
        }
    }

    bool TerrainCompositionSurfaceProviderComponent::ReadInConfig(const AZ::ComponentConfig* baseConfig)
    {
        if (!m_binding.CheckControlThread())
        {
            return false;
        }
        return Internal::ReadConfiguration<TerrainCompositionSurfaceProviderConfig>(baseConfig, [this](const auto& value)
        {
            const bool changed = value.m_compositionEntityId != m_configuration.m_compositionEntityId;
            m_configuration = value;
            if (changed)
            {
                RestartProvider();
            }
        });
    }

    bool TerrainCompositionSurfaceProviderComponent::WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const
    {
        if (!m_binding.CheckControlThread())
        {
            return false;
        }
        return Internal::WriteConfiguration(outBaseConfig, m_configuration);
    }

    AZStd::string TerrainCompositionSurfaceProviderComponent::GetStatusMessage() const
    {
        if (!m_binding.CheckControlThread()) { return "Unavailable off the control thread."; }
        if (!m_binding.IsActive()) { return "Inactive: no terrain surface provider."; }
        if (!m_configuration.m_compositionEntityId.IsValid()) { return "Select a Terrain Composition entity."; }
        const auto& compositionAddress = m_binding.GetCompositionAddress();
        if (compositionAddress.first.IsNull()) { return "Waiting for entity context ownership."; }
        if (!TerrainCompositionSurfaceRequestBus::HasHandlers(compositionAddress))
        {
            return "Terrain Composition is unavailable in this entity context.";
        }
        return "Ready: composition surface weights are provided to this terrain region.";
    }

    void TerrainCompositionSurfaceProviderComponent::OnSystemTick()
    {
        if (m_binding.HasOwningContextChanged())
        {
            RestartProvider();
        }
    }

    void TerrainCompositionSurfaceProviderComponent::GetSurfaceWeights(
        const AZ::Vector3& position, AzFramework::SurfaceData::SurfaceTagWeightList& outSurfaceWeights) const
    {
        outSurfaceWeights.clear();
        TerrainCompositionSurfaceRequestBus::Event(
            m_binding.GetCompositionAddress(), &TerrainCompositionSurfaceRequestBus::Events::GetSurfaceWeights,
            m_binding.GetTerrainRegionEntityId(), position, outSurfaceWeights);
    }

    void TerrainCompositionSurfaceProviderComponent::GetSurfaceWeightsFromList(
        AZStd::span<const AZ::Vector3> positions,
        AZStd::span<AzFramework::SurfaceData::SurfaceTagWeightList> outSurfaceWeights) const
    {
        if (!PrepareSurfaceWeightBatch(positions, outSurfaceWeights))
        {
            return;
        }
        TerrainCompositionSurfaceRequestBus::Event(
            m_binding.GetCompositionAddress(), &TerrainCompositionSurfaceRequestBus::Events::GetSurfaceWeightsFromList,
            m_binding.GetTerrainRegionEntityId(), positions, outSurfaceWeights);
    }
} // namespace TerrainCompositor
