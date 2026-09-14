#include <TerrainCompositor/Components/TerrainMeshCutoutComponent.h>
#include "../ComponentConfiguration.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace TerrainCompositor
{
    AZ_COMPONENT_IMPL(TerrainMeshCutoutComponent, "TerrainMeshCutoutComponent",
        TerrainMeshCutoutComponentTypeId, AzFramework::EditorEntityEvents);

    TerrainMeshCutoutComponent::TerrainMeshCutoutComponent() = default;

    void TerrainMeshCutoutComponent::Reflect(AZ::ReflectContext* context)
    {
        TerrainMeshCutoutConfig::Reflect(context);
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<TerrainMeshCutoutComponent, AZ::Component>()
                ->Version(1)
                ->Field("Configuration", &TerrainMeshCutoutComponent::m_configuration);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<TerrainMeshCutoutComponent>("Terrain Mesh Cutout",
                    "Remove or restore composed terrain where its final surface lies inside a closed mesh volume.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Terrain")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainMeshCutoutComponent::m_configuration,
                        "Configuration", "Cutter model asset, component placement, ordering, operation, and target composition.")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, &TerrainMeshCutoutComponent::OnConfigurationChanged);
            }
        }
    }

    TerrainMeshCutoutComponent::TerrainMeshCutoutComponent(const TerrainMeshCutoutConfig& configuration)
        : m_configuration(configuration)
    {
    }

    void TerrainMeshCutoutComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("TerrainMeshCutoutService"));
    }
    void TerrainMeshCutoutComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("TerrainMeshCutoutService"));
        incompatible.push_back(AZ_CRC_CE("HeightmapStampService"));
        incompatible.push_back(AZ_CRC_CE("NonUniformScaleService"));
    }
    void TerrainMeshCutoutComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("TransformService"));
    }
    void TerrainMeshCutoutComponent::GetDependentServices(
        [[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
    }

    void TerrainMeshCutoutComponent::Activate() { StartCutout(GetEntityId()); }
    void TerrainMeshCutoutComponent::EditorActivate(AZ::EntityId entityId) { StartCutout(entityId, true); }
    void TerrainMeshCutoutComponent::EditorDeactivate([[maybe_unused]] AZ::EntityId entityId) { Deactivate(); }

    void TerrainMeshCutoutComponent::StartCutout(AZ::EntityId entityId, bool editor)
    {
        if (!m_placement.IsActive()) m_controlThread.BindForActivation();
        if (!m_controlThread.Check()) return;
        Deactivate();
        m_placement.Start(entityId, m_configuration.m_cutoutMeshAsset.GetId(), editor);
    }

    void TerrainMeshCutoutComponent::Deactivate()
    {
        if (!m_placement.IsActive() || !m_controlThread.Check()) return;
        m_placement.Stop();
        m_registration.Deactivate();
    }

    void TerrainMeshCutoutComponent::SetCutoutConfiguration(const TerrainMeshCutoutConfig& configuration)
    {
        if (!m_controlThread.Check()) return;
        Internal::AssignConfiguration(m_configuration, configuration, [this] { OnConfigurationChanged(); });
    }

    TerrainMeshCutoutConfig TerrainMeshCutoutComponent::GetCutoutConfiguration() const
    {
        return m_controlThread.Check() ? m_configuration : TerrainMeshCutoutConfig{};
    }

    AZStd::string TerrainMeshCutoutComponent::GetStatusMessage() const
    {
        if (!m_controlThread.Check()) return "Unavailable off the control thread.";
        if (!m_configuration.m_cutoutMeshAsset.GetId().IsValid())
        {
            return "Select a closed Cutout Mesh model asset.";
        }
        if (const char* status = m_placement.GetPlacementStatus(
            "Waiting for the matching Cutout Mesh instance to activate in this entity hierarchy.",
            "No Atom Mesh instance using Cutout Mesh exists on this entity or its descendants.",
            "More than one Atom Mesh instance uses Cutout Mesh in this entity hierarchy; placement is ambiguous."))
            return status;
        return m_registration.GetStatusMessage();
    }

    bool TerrainMeshCutoutComponent::ReadInConfig(const AZ::ComponentConfig* configuration)
    {
        return Internal::ReadConfiguration<TerrainMeshCutoutConfig>(configuration, [this](const auto& value)
        {
            SetCutoutConfiguration(value);
        });
    }

    bool TerrainMeshCutoutComponent::WriteOutConfig(AZ::ComponentConfig* configuration) const
    {
        return Internal::WriteConfiguration(configuration, m_configuration);
    }

    AZ::u32 TerrainMeshCutoutComponent::OnConfigurationChanged()
    {
        m_placement.Refresh(m_configuration.m_cutoutMeshAsset.GetId());
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }

    void TerrainMeshCutoutComponent::UpdateRegistration()
    {
        if (m_controlThread.Check()) m_placement.UpdateRegistration(m_registration, m_configuration);
    }

} // namespace TerrainCompositor
