#include <TerrainCompositor/Components/HeightmapStampComponent.h>
#include "../ComponentConfiguration.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace TerrainCompositor
{
    AZ_COMPONENT_IMPL(HeightmapStampComponent, "HeightmapStampComponent", HeightmapStampComponentTypeId,
        AzFramework::EditorEntityEvents);

    void HeightmapStampComponent::Reflect(AZ::ReflectContext* context)
    {
        HeightmapStampConfig::Reflect(context);
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<HeightmapStampComponent, AZ::Component>()
                ->Version(1)
                ->Field("Configuration", &HeightmapStampComponent::m_configuration);
            if (AZ::EditContext* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<HeightmapStampComponent>("Heightmap Stamp",
                    "Place a yaw-only heightmap over procedural ground using feathered Replace blending.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Terrain")
                    // Legacy generic wrappers remain readable; new authoring uses the dedicated editor component.
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampComponent::m_configuration,
                        "Configuration", "Local-meter placement and accumulated Replace settings. Invalid settings contribute nothing.")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, &HeightmapStampComponent::OnConfigurationChanged);
            }
        }
    }

    HeightmapStampComponent::HeightmapStampComponent(const HeightmapStampConfig& configuration)
        : m_configuration(configuration)
    {
    }

    void HeightmapStampComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("HeightmapStampService"));
    }

    void HeightmapStampComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("HeightmapStampService"));
        incompatible.push_back(AZ_CRC_CE("NonUniformScaleService"));
    }

    void HeightmapStampComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("TransformService"));
    }

    void HeightmapStampComponent::GetDependentServices(
        [[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
    }

    void HeightmapStampComponent::Activate()
    {
        StartStamp(GetEntityId());
    }

    void HeightmapStampComponent::EditorActivate(AZ::EntityId entityId)
    {
        // GenericComponentWrapper calls this instead of Activate; the wrapped component has no editor entity.
        StartStamp(entityId, true);
    }

    void HeightmapStampComponent::EditorDeactivate([[maybe_unused]] AZ::EntityId entityId)
    {
        Deactivate();
    }

    void HeightmapStampComponent::StartStamp(AZ::EntityId entityId, bool editor)
    {
        if (!m_activeEntityId.IsValid())
        {
            m_controlThread.BindForActivation();
        }
        if (!m_controlThread.Check()) { return; }
        Deactivate();
        m_activeEntityId = entityId;
        m_editor = editor;
        if (editor)
        {
            HeightmapStampIdentityNotificationBus::Handler::BusConnect();
        }
        AZ::TransformNotificationBus::Handler::BusConnect(entityId);
        AZ::Transform world = AZ::Transform::CreateIdentity();
        const bool available = AZ::TransformBus::HasHandlers(entityId);
        if (available)
        {
            AZ::TransformBus::EventResult(world, entityId, &AZ::TransformBus::Events::GetWorldTM);
        }
        const auto configuration = GetRegistrationConfiguration();
        const bool pending = m_editor && HeightmapStampIdentityInterface::Get() && configuration.m_stableOrderKey.empty();
        m_registration.Activate(entityId, configuration, world, available, pending);
    }

    void HeightmapStampComponent::Deactivate()
    {
        if (!m_activeEntityId.IsValid()) { return; }
        if (!m_controlThread.Check()) { return; }
        HeightmapStampIdentityNotificationBus::Handler::BusDisconnect();
        AZ::TransformNotificationBus::Handler::BusDisconnect();
        m_registration.Deactivate();
        m_activeEntityId.SetInvalid();
        m_editor = false;
    }

    void HeightmapStampComponent::SetStampConfiguration(const HeightmapStampConfig& configuration)
    {
        if (!m_controlThread.Check()) { return; }
        m_configuration = configuration;
        OnConfigurationChanged();
    }

    HeightmapStampConfig HeightmapStampComponent::GetStampConfiguration() const
    {
        return m_controlThread.Check() ? m_configuration : HeightmapStampConfig{};
    }

    AZStd::string HeightmapStampComponent::GetStatusMessage() const
    {
        return m_controlThread.Check() ? m_registration.GetStatusMessage() : "Unavailable off the control thread.";
    }

    bool HeightmapStampComponent::ReadInConfig(const AZ::ComponentConfig* baseConfig)
    {
        if (!m_controlThread.Check()) { return false; }
        return Internal::ReadConfiguration<HeightmapStampConfig>(baseConfig, [this](const auto& value)
        {
            SetStampConfiguration(value);
        });
    }

    bool HeightmapStampComponent::WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const
    {
        if (!m_controlThread.Check()) { return false; }
        return Internal::WriteConfiguration(outBaseConfig, m_configuration);
    }

    AZ::u32 HeightmapStampComponent::OnConfigurationChanged()
    {
        UpdateRegistration();
        // Refresh the dependent feather/inset maxima after dimensions or either edge width changes.
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }

    void HeightmapStampComponent::UpdateRegistration()
    {
        if (!m_controlThread.Check() || !m_activeEntityId.IsValid())
        {
            return;
        }
        AZ::Transform world = AZ::Transform::CreateIdentity();
        const bool available = AZ::TransformBus::HasHandlers(m_activeEntityId);
        if (available)
        {
            AZ::TransformBus::EventResult(world, m_activeEntityId, &AZ::TransformBus::Events::GetWorldTM);
        }
        const auto configuration = GetRegistrationConfiguration();
        const bool pending = m_editor && HeightmapStampIdentityInterface::Get() && configuration.m_stableOrderKey.empty();
        m_registration.Update(configuration, world, available, pending);
    }

    void HeightmapStampComponent::OnTransformChanged(
        [[maybe_unused]] const AZ::Transform& local, const AZ::Transform& world)
    {
        // The supplied world transform includes parent yaw/scale and any unsupported inherited pitch/roll.
        if (m_controlThread.Check() && m_activeEntityId.IsValid())
        {
            const auto configuration = GetRegistrationConfiguration();
            const bool pending = m_editor && HeightmapStampIdentityInterface::Get() && configuration.m_stableOrderKey.empty();
            m_registration.Update(configuration, world, true, pending);
        }
    }

    void HeightmapStampComponent::OnParentChanged(
        [[maybe_unused]] AZ::EntityId oldParent, [[maybe_unused]] AZ::EntityId newParent)
    {
        UpdateRegistration();
    }
    HeightmapStampConfig HeightmapStampComponent::GetRegistrationConfiguration() const
    {
        auto configuration = m_configuration;
        if (m_editor)
        {
            // Copied legacy/baked keys cannot identify a prefab instance. Never dirty its saved template.
            configuration.m_orderingId = {};
            const auto* resolver = HeightmapStampIdentityInterface::Get();
            configuration.m_stableOrderKey = resolver ? resolver->ResolveStampOrderKey(m_activeEntityId) : AZStd::string{};
        }
        return configuration;
    }

    void HeightmapStampComponent::OnStampIdentitiesChanged()
    {
        UpdateRegistration();
    }
} // namespace TerrainCompositor
