#include <TerrainCompositor/Components/TerrainCompositionSurfaceProviderComponent.h>

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
        m_binding.Start(GetEntityId());
    }

    void TerrainCompositionSurfaceProviderComponent::Deactivate()
    {
        m_binding.Stop();
    }

    void TerrainCompositionSurfaceProviderComponent::EditorActivate(AZ::EntityId entityId)
    {
        m_binding.Start(entityId);
    }

    void TerrainCompositionSurfaceProviderComponent::EditorDeactivate([[maybe_unused]] AZ::EntityId entityId)
    {
        m_binding.Stop();
    }

    void TerrainCompositionSurfaceProviderComponent::ConnectProvider()
    {
        Terrain::TerrainAreaSurfaceRequestBus::Handler::BusConnect(m_binding.GetTerrainRegionEntityId());
    }

    void TerrainCompositionSurfaceProviderComponent::DisconnectProvider()
    {
        Terrain::TerrainAreaSurfaceRequestBus::Handler::BusDisconnect();
    }

    bool TerrainCompositionSurfaceProviderComponent::ReadInConfig(const AZ::ComponentConfig* baseConfig)
    {
        return m_binding.ReadConfiguration(baseConfig, m_configuration);
    }

    bool TerrainCompositionSurfaceProviderComponent::WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const
    {
        return m_binding.WriteConfiguration(outBaseConfig, m_configuration);
    }

    AZStd::string TerrainCompositionSurfaceProviderComponent::GetStatusMessage() const
    {
        return m_binding.GetStatusMessage<TerrainCompositionSurfaceRequestBus>(
            "Inactive: no terrain surface provider.",
            "Ready: composition surface weights are provided to this terrain region.");
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
