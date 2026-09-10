#include <TerrainCompositor/Components/TerrainMeshHeightStampComponent.h>
#include "../ComponentConfiguration.h"

#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace TerrainCompositor
{
    AZ_COMPONENT_IMPL(
        TerrainMeshHeightStampComponent,
        "TerrainMeshHeightStampComponent",
        TerrainMeshHeightStampComponentTypeId,
        AzFramework::EditorEntityEvents);

    TerrainMeshHeightStampComponent::TerrainMeshHeightStampComponent() = default;

    TerrainMeshHeightStampComponent::TerrainMeshHeightStampComponent(const TerrainMeshHeightStampConfig& configuration)
        : m_configuration(configuration)
    {
    }

    void TerrainMeshHeightStampComponent::Reflect(AZ::ReflectContext* context)
    {
        TerrainMeshHeightStampConfig::Reflect(context);
        if (auto* behavior = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behavior->EBus<TerrainMeshHeightStampRequestBus>("TerrainMeshHeightStampRequestBus")
                ->Attribute(AZ::Script::Attributes::Module, "terrain_compositor")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation)
                ->Event("GetStatusMessage", &TerrainMeshHeightStampRequestBus::Events::GetStatusMessage);
            behavior->Class<TerrainMeshHeightStampComponent>()->RequestBus("TerrainMeshHeightStampRequestBus");
        }
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<TerrainMeshHeightStampComponent, AZ::Component>()->Version(1)->Field(
                "Configuration", &TerrainMeshHeightStampComponent::m_configuration);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<TerrainMeshHeightStampComponent>(
                        "Terrain Mesh Height Stamp", "Convert a matching regular-grid mesh descendant into composed terrain height.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Terrain")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainMeshHeightStampComponent::m_configuration,
                        "Configuration",
                        "Terrain model, target composition, placement, blending, and ordering.")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, &TerrainMeshHeightStampComponent::OnConfigurationChanged);
            }
        }
    }

    void TerrainMeshHeightStampComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("TerrainMeshHeightStampService"));
    }

    void TerrainMeshHeightStampComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("TerrainMeshHeightStampService"));
        incompatible.push_back(AZ_CRC_CE("HeightmapStampService"));
        incompatible.push_back(AZ_CRC_CE("NonUniformScaleService"));
    }

    void TerrainMeshHeightStampComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("TransformService"));
    }

    void TerrainMeshHeightStampComponent::GetDependentServices([[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
    }

    void TerrainMeshHeightStampComponent::Activate()
    {
        StartStamp(GetEntityId());
    }
    void TerrainMeshHeightStampComponent::Deactivate()
    {
        StopStamp();
    }
    void TerrainMeshHeightStampComponent::EditorActivate(AZ::EntityId entityId)
    {
        StartStamp(entityId, true);
    }
    void TerrainMeshHeightStampComponent::EditorDeactivate([[maybe_unused]] AZ::EntityId entityId)
    {
        StopStamp();
    }

    void TerrainMeshHeightStampComponent::StartStamp(AZ::EntityId entityId, bool editor)
    {
        if (!m_placement.IsActive()) m_controlThread.BindForActivation();
        if (!m_controlThread.Check()) return;
        StopStamp();
        TerrainMeshHeightStampRequestBus::Handler::BusConnect(entityId);
        m_placement.Start(entityId, m_configuration.m_terrainMeshAsset.GetId(), editor);
    }

    void TerrainMeshHeightStampComponent::StopStamp()
    {
        if (!m_placement.IsActive() || !m_controlThread.Check()) return;
        m_placement.Stop();
        TerrainMeshHeightStampRequestBus::Handler::BusDisconnect();
        m_registration.Deactivate();
    }

    void TerrainMeshHeightStampComponent::SetStampConfiguration(const TerrainMeshHeightStampConfig& configuration)
    {
        if (!m_controlThread.Check())
            return;
        m_configuration = configuration;
        OnConfigurationChanged();
    }

    TerrainMeshHeightStampConfig TerrainMeshHeightStampComponent::GetStampConfiguration() const
    {
        return m_controlThread.Check() ? m_configuration : TerrainMeshHeightStampConfig{};
    }

    AZStd::string TerrainMeshHeightStampComponent::GetStatusMessage() const
    {
        if (!m_controlThread.Check())
            return "Unavailable off the control thread.";
        if (!m_configuration.m_terrainMeshAsset.GetId().IsValid())
            return "Select a Terrain Mesh model asset.";
        if (m_placement.GetMatchingMeshCount() == 0)
        {
            return m_placement.IsWaitingForPlacement()
                ? "Waiting for the matching Terrain Mesh instance to activate in this entity hierarchy."
                : "No Atom Mesh instance using Terrain Mesh exists on this entity or its descendants.";
        }
        if (m_placement.GetMatchingMeshCount() > 1)
        {
            return "More than one Atom Mesh instance uses Terrain Mesh in this hierarchy; placement is ambiguous.";
        }
        return m_registration.GetStatusMessage();
    }

    TerrainMeshHeightStampRegistrationData TerrainMeshHeightStampComponent::GetDiagnosticRegistration() const
    {
        return m_controlThread.Check() ? m_registration.GetRegistrationData() : TerrainMeshHeightStampRegistrationData{};
    }

    bool TerrainMeshHeightStampComponent::ReadInConfig(const AZ::ComponentConfig* configuration)
    {
        return Internal::ReadConfiguration<TerrainMeshHeightStampConfig>(configuration, [this](const auto& value)
        {
            SetStampConfiguration(value);
        });
    }

    bool TerrainMeshHeightStampComponent::WriteOutConfig(AZ::ComponentConfig* configuration) const
    {
        return Internal::WriteConfiguration(configuration, m_configuration);
    }

    AZ::u32 TerrainMeshHeightStampComponent::OnConfigurationChanged()
    {
        m_placement.Refresh(m_configuration.m_terrainMeshAsset.GetId());
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }

    void TerrainMeshHeightStampComponent::UpdateRegistration()
    {
        if (m_controlThread.Check()) m_placement.UpdateRegistration(m_registration, m_configuration);
    }

} // namespace TerrainCompositor
