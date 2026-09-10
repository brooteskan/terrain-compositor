#include <TerrainCompositor/Components/TerrainMeshCutoutComponent.h>
#include "MeshPlacementHelpers.h"
#include "../ComponentConfiguration.h"

#include <AtomLyIntegration/CommonFeatures/Mesh/MeshComponentBus.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace TerrainCompositor
{
    AZ_COMPONENT_IMPL(TerrainMeshCutoutComponent, "TerrainMeshCutoutComponent",
        TerrainMeshCutoutComponentTypeId, AzFramework::EditorEntityEvents);

    TerrainMeshCutoutComponent::TerrainMeshCutoutComponent()
        : m_nonUniformScaleChangedHandler(
            [this]([[maybe_unused]] const AZ::Vector3& scale) { UpdateRegistration(); })
    {
    }

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
        , m_nonUniformScaleChangedHandler(
            [this]([[maybe_unused]] const AZ::Vector3& scale) { UpdateRegistration(); })
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
    void TerrainMeshCutoutComponent::Deactivate() { StopCutout(); }
    void TerrainMeshCutoutComponent::EditorActivate(AZ::EntityId entityId) { StartCutout(entityId, true); }
    void TerrainMeshCutoutComponent::EditorDeactivate([[maybe_unused]] AZ::EntityId entityId) { StopCutout(); }

    void TerrainMeshCutoutComponent::StartCutout(AZ::EntityId entityId, bool editor)
    {
        if (!m_activeEntityId.IsValid()) m_controlThread.BindForActivation();
        if (!m_controlThread.Check()) return;
        StopCutout();
        m_activeEntityId = entityId;
        m_editor = editor;
        m_deferredUpdateState = AZStd::make_shared<DeferredUpdateState>();
        m_deferredUpdateState->m_component.store(this);
        if (editor) HeightmapStampIdentityNotificationBus::Handler::BusConnect();
        BindPlacementEntity();
        UpdateRegistration();
        SchedulePlacementEntityUpdate();
        if (!editor)
        {
            ScheduleRuntimeMeshVisibilityUpdate();
        }
    }

    void TerrainMeshCutoutComponent::StopCutout()
    {
        if (!m_activeEntityId.IsValid() || !m_controlThread.Check()) return;
        if (m_deferredUpdateState) m_deferredUpdateState->m_component.store(nullptr);
        AZ::SystemTickBus::Handler::BusDisconnect();
        m_placementRetriesRemaining = 0;
        m_runtimeVisibilityRetriesRemaining = 0;
        RestoreRuntimeCutoutMeshVisibility();
        HeightmapStampIdentityNotificationBus::Handler::BusDisconnect();
        UnbindPlacementEntity();
        m_registration.Deactivate();
        m_deferredUpdateState.reset();
        m_matchingMeshCount = 0;
        m_activeEntityId.SetInvalid();
        m_editor = false;
    }

    void TerrainMeshCutoutComponent::SetCutoutConfiguration(const TerrainMeshCutoutConfig& configuration)
    {
        if (!m_controlThread.Check()) return;
        m_configuration = configuration;
        OnConfigurationChanged();
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
        if (m_matchingMeshCount == 0)
        {
            return m_placementRetriesRemaining > 0
                ? "Waiting for the matching Cutout Mesh instance to activate in this entity hierarchy."
                : "No Atom Mesh instance using Cutout Mesh exists on this entity or its descendants.";
        }
        if (m_matchingMeshCount > 1)
        {
            return "More than one Atom Mesh instance uses Cutout Mesh in this entity hierarchy; placement is ambiguous.";
        }
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
        if (!m_editor && m_activeEntityId.IsValid())
        {
            RestoreRuntimeCutoutMeshVisibility();
        }
        BindPlacementEntity();
        UpdateRegistration();
        SchedulePlacementEntityUpdate();
        if (!m_editor && m_activeEntityId.IsValid())
        {
            ScheduleRuntimeMeshVisibilityUpdate();
        }
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }

    AZ::EntityId TerrainMeshCutoutComponent::ResolvePlacementEntity(size_t& matchingMeshCount) const
    {
        return Internal::ResolveUniqueModelEntity(m_activeEntityId, m_configuration.m_cutoutMeshAsset.GetId(), matchingMeshCount);
    }

    void TerrainMeshCutoutComponent::ScheduleRuntimeMeshVisibilityUpdate()
    {
        // Child runtime entities can activate after their parent. Retry briefly so Mesh request handlers and the
        // transform hierarchy are available without relying on prefab entity activation order.
        m_runtimeVisibilityRetriesRemaining = 8;
        if (!AZ::SystemTickBus::Handler::BusIsConnected())
        {
            AZ::SystemTickBus::Handler::BusConnect();
        }
    }

    void TerrainMeshCutoutComponent::SchedulePlacementEntityUpdate()
    {
        // Imported child entities and their Mesh request handlers can activate after the component owner.
        m_placementRetriesRemaining = 8;
        if (!AZ::SystemTickBus::Handler::BusIsConnected())
        {
            AZ::SystemTickBus::Handler::BusConnect();
        }
    }

    void TerrainMeshCutoutComponent::QueuePlacementEntityUpdate()
    {
        const AZStd::weak_ptr<DeferredUpdateState> weak = m_deferredUpdateState;
        AZ::SystemTickBus::QueueFunction([weak]()
        {
            if (const auto state = weak.lock())
            {
                if (auto* component = state->m_component.load())
                {
                    component->SchedulePlacementEntityUpdate();
                    if (!component->m_editor)
                    {
                        component->ScheduleRuntimeMeshVisibilityUpdate();
                    }
                }
            }
        });
    }

    void TerrainMeshCutoutComponent::HideRuntimeCutoutMeshInstances()
    {
        if (!m_configuration.m_cutoutMeshAsset.GetId().IsValid()) return;
        Internal::HideMatchingModel(m_boundPlacementEntityId, m_configuration.m_cutoutMeshAsset.GetId(), m_runtimeMeshPreviousVisibility);
    }

    void TerrainMeshCutoutComponent::RestoreRuntimeCutoutMeshVisibility()
    {
        Internal::RestoreModelVisibility(m_runtimeMeshPreviousVisibility);
    }

    void TerrainMeshCutoutComponent::OnSystemTick()
    {
        if (!m_activeEntityId.IsValid())
        {
            AZ::SystemTickBus::Handler::BusDisconnect();
            return;
        }
        if (m_placementRetriesRemaining > 0)
        {
            BindPlacementEntity();
            UpdateRegistration();
            --m_placementRetriesRemaining;
        }
        if (!m_editor && m_runtimeVisibilityRetriesRemaining > 0)
        {
            HideRuntimeCutoutMeshInstances();
            --m_runtimeVisibilityRetriesRemaining;
        }
        if (m_placementRetriesRemaining == 0 && m_runtimeVisibilityRetriesRemaining == 0)
        {
            AZ::SystemTickBus::Handler::BusDisconnect();
        }
    }

    void TerrainMeshCutoutComponent::BindPlacementEntity()
    {
        if (!m_activeEntityId.IsValid()) return;
        size_t matchingMeshCount = 0;
        const AZ::EntityId desired = ResolvePlacementEntity(matchingMeshCount);
        if (desired == m_boundPlacementEntityId && matchingMeshCount == m_matchingMeshCount &&
            m_transformNotificationsBound)
        {
            return;
        }

        if (!m_editor)
        {
            RestoreRuntimeCutoutMeshVisibility();
        }
        UnbindPlacementEntity();
        m_matchingMeshCount = matchingMeshCount;
        m_boundPlacementEntityId = desired;
        AZ::TransformNotificationBus::MultiHandler::BusConnect(m_activeEntityId);
        m_transformNotificationsBound = true;
        if (!m_boundPlacementEntityId.IsValid()) return;
        if (m_boundPlacementEntityId != m_activeEntityId)
        {
            AZ::TransformNotificationBus::MultiHandler::BusConnect(m_boundPlacementEntityId);
        }
        AZ::Render::MeshComponentNotificationBus::Handler::BusConnect(m_boundPlacementEntityId);
        if (AZ::NonUniformScaleRequestBus::HasHandlers(m_boundPlacementEntityId))
        {
            AZ::NonUniformScaleRequestBus::Event(
                m_boundPlacementEntityId, &AZ::NonUniformScaleRequests::RegisterScaleChangedEvent,
                m_nonUniformScaleChangedHandler);
        }
    }

    void TerrainMeshCutoutComponent::UnbindPlacementEntity()
    {
        m_nonUniformScaleChangedHandler.Disconnect();
        AZ::Render::MeshComponentNotificationBus::Handler::BusDisconnect();
        AZ::TransformNotificationBus::MultiHandler::BusDisconnect();
        m_transformNotificationsBound = false;
        m_boundPlacementEntityId.SetInvalid();
    }

    void TerrainMeshCutoutComponent::UpdateRegistration()
    {
        if (!m_controlThread.Check() || !m_activeEntityId.IsValid()) return;
        if (m_matchingMeshCount != 1 || !m_boundPlacementEntityId.IsValid())
        {
            m_registration.Deactivate();
            return;
        }
        const AZ::EntityId placementEntityId = m_boundPlacementEntityId;
        AZ::Transform world = AZ::Transform::CreateIdentity();
        const bool available = AZ::TransformBus::HasHandlers(placementEntityId);
        if (available) AZ::TransformBus::EventResult(world, placementEntityId, &AZ::TransformBus::Events::GetWorldTM);
        const auto configuration = GetRegistrationConfiguration();
        const bool pending = m_editor && HeightmapStampIdentityInterface::Get() && configuration.m_stableOrderKey.empty();
        const bool hasNonUniformScale = AZ::NonUniformScaleRequestBus::HasHandlers(placementEntityId);
        if (m_registration.IsActive())
        {
            m_registration.Update(configuration, world, available, pending, hasNonUniformScale);
        }
        else
        {
            m_registration.Activate(m_activeEntityId, configuration, world, available, pending, hasNonUniformScale);
        }
    }

    TerrainMeshCutoutConfig TerrainMeshCutoutComponent::GetRegistrationConfiguration() const
    {
        auto configuration = m_configuration;
        if (m_editor)
        {
            configuration.m_orderingId = {};
            const auto* resolver = HeightmapStampIdentityInterface::Get();
            configuration.m_stableOrderKey = resolver ? resolver->ResolveStampOrderKey(m_activeEntityId) : AZStd::string{};
        }
        return configuration;
    }

    void TerrainMeshCutoutComponent::OnStampIdentitiesChanged() { UpdateRegistration(); }
    void TerrainMeshCutoutComponent::OnTransformChanged(
        [[maybe_unused]] const AZ::Transform& local, [[maybe_unused]] const AZ::Transform& world)
    {
        UpdateRegistration();
    }
    void TerrainMeshCutoutComponent::OnParentChanged(
        [[maybe_unused]] AZ::EntityId oldParent, [[maybe_unused]] AZ::EntityId newParent)
    {
        SchedulePlacementEntityUpdate();
    }

    void TerrainMeshCutoutComponent::OnChildAdded([[maybe_unused]] AZ::EntityId child)
    {
        SchedulePlacementEntityUpdate();
    }

    void TerrainMeshCutoutComponent::OnChildRemoved([[maybe_unused]] AZ::EntityId child)
    {
        SchedulePlacementEntityUpdate();
    }

    void TerrainMeshCutoutComponent::OnModelReady(
        [[maybe_unused]] const AZ::Data::Asset<AZ::RPI::ModelAsset>& modelAsset,
        [[maybe_unused]] const AZ::Data::Instance<AZ::RPI::Model>& model)
    {
        QueuePlacementEntityUpdate();
    }

    void TerrainMeshCutoutComponent::OnModelPreDestroy()
    {
        QueuePlacementEntityUpdate();
    }

} // namespace TerrainCompositor
