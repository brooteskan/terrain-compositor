#include <TerrainCompositor/Components/TerrainMeshHeightStampComponent.h>
#include "MeshPlacementHelpers.h"
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

    TerrainMeshHeightStampComponent::TerrainMeshHeightStampComponent()
        : m_nonUniformScaleChangedHandler(
              [this]([[maybe_unused]] const AZ::Vector3& scale)
              {
                  UpdateRegistration();
              })
    {
    }

    TerrainMeshHeightStampComponent::TerrainMeshHeightStampComponent(const TerrainMeshHeightStampConfig& configuration)
        : m_configuration(configuration)
        , m_nonUniformScaleChangedHandler(
              [this]([[maybe_unused]] const AZ::Vector3& scale)
              {
                  UpdateRegistration();
              })
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
        if (!m_activeEntityId.IsValid())
            m_controlThread.BindForActivation();
        if (!m_controlThread.Check())
            return;
        StopStamp();
        m_activeEntityId = entityId;
        m_editor = editor;
        TerrainMeshHeightStampRequestBus::Handler::BusConnect(entityId);
        m_deferredUpdateState = AZStd::make_shared<DeferredUpdateState>();
        m_deferredUpdateState->m_component.store(this);
        if (editor)
            HeightmapStampIdentityNotificationBus::Handler::BusConnect();
        BindPlacementEntity();
        UpdateRegistration();
        SchedulePlacementEntityUpdate();
        if (!editor)
        {
            ScheduleRuntimeMeshVisibilityUpdate();
        }
    }

    void TerrainMeshHeightStampComponent::StopStamp()
    {
        if (!m_activeEntityId.IsValid() || !m_controlThread.Check())
            return;
        if (m_deferredUpdateState)
            m_deferredUpdateState->m_component.store(nullptr);
        AZ::SystemTickBus::Handler::BusDisconnect();
        m_placementRetriesRemaining = 0;
        m_runtimeVisibilityRetriesRemaining = 0;
        RestoreRuntimeSourceMeshVisibility();
        TerrainMeshHeightStampRequestBus::Handler::BusDisconnect();
        HeightmapStampIdentityNotificationBus::Handler::BusDisconnect();
        UnbindPlacementEntity();
        m_registration.Deactivate();
        m_deferredUpdateState.reset();
        m_matchingMeshCount = 0;
        m_activeEntityId.SetInvalid();
        m_editor = false;
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
        if (m_matchingMeshCount == 0)
        {
            return m_placementRetriesRemaining > 0 ? "Waiting for the matching Terrain Mesh instance to activate in this entity hierarchy."
                                                   : "No Atom Mesh instance using Terrain Mesh exists on this entity or its descendants.";
        }
        if (m_matchingMeshCount > 1)
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
        if (!m_editor)
        {
            RestoreRuntimeSourceMeshVisibility();
        }
        BindPlacementEntity();
        UpdateRegistration();
        SchedulePlacementEntityUpdate();
        if (!m_editor)
        {
            ScheduleRuntimeMeshVisibilityUpdate();
        }
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }

    AZ::EntityId TerrainMeshHeightStampComponent::ResolvePlacementEntity(size_t& matchingMeshCount) const
    {
        return Internal::ResolveUniqueModelEntity(m_activeEntityId, m_configuration.m_terrainMeshAsset.GetId(), matchingMeshCount);
    }

    void TerrainMeshHeightStampComponent::SchedulePlacementEntityUpdate()
    {
        m_placementRetriesRemaining = 8;
        if (!AZ::SystemTickBus::Handler::BusIsConnected())
            AZ::SystemTickBus::Handler::BusConnect();
    }

    void TerrainMeshHeightStampComponent::ScheduleRuntimeMeshVisibilityUpdate()
    {
        if (m_editor)
            return;
        m_runtimeVisibilityRetriesRemaining = 8;
        if (!AZ::SystemTickBus::Handler::BusIsConnected())
            AZ::SystemTickBus::Handler::BusConnect();
    }

    void TerrainMeshHeightStampComponent::QueuePlacementEntityUpdate()
    {
        const AZStd::weak_ptr<DeferredUpdateState> weak = m_deferredUpdateState;
        AZ::SystemTickBus::QueueFunction(
            [weak]()
            {
                if (const auto state = weak.lock())
                {
                    if (auto* component = state->m_component.load())
                    {
                        component->SchedulePlacementEntityUpdate();
                        component->ScheduleRuntimeMeshVisibilityUpdate();
                    }
                }
            });
    }

    void TerrainMeshHeightStampComponent::HideRuntimeSourceMesh()
    {
        if (m_editor) return;
        Internal::HideMatchingModel(m_boundPlacementEntityId, m_configuration.m_terrainMeshAsset.GetId(), m_runtimeMeshPreviousVisibility);
    }

    void TerrainMeshHeightStampComponent::RestoreRuntimeSourceMeshVisibility()
    {
        Internal::RestoreModelVisibility(m_runtimeMeshPreviousVisibility);
    }

    void TerrainMeshHeightStampComponent::OnSystemTick()
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
        if (m_runtimeVisibilityRetriesRemaining > 0)
        {
            HideRuntimeSourceMesh();
            --m_runtimeVisibilityRetriesRemaining;
        }
        if (m_placementRetriesRemaining == 0 && m_runtimeVisibilityRetriesRemaining == 0)
        {
            AZ::SystemTickBus::Handler::BusDisconnect();
        }
    }

    void TerrainMeshHeightStampComponent::BindPlacementEntity()
    {
        if (!m_activeEntityId.IsValid())
            return;
        size_t matchingMeshCount = 0;
        const AZ::EntityId desired = ResolvePlacementEntity(matchingMeshCount);
        if (desired == m_boundPlacementEntityId && matchingMeshCount == m_matchingMeshCount && m_transformNotificationsBound)
            return;

        RestoreRuntimeSourceMeshVisibility();
        UnbindPlacementEntity();
        m_matchingMeshCount = matchingMeshCount;
        m_boundPlacementEntityId = desired;
        AZ::TransformNotificationBus::MultiHandler::BusConnect(m_activeEntityId);
        m_transformNotificationsBound = true;
        if (!m_boundPlacementEntityId.IsValid())
            return;
        if (m_boundPlacementEntityId != m_activeEntityId)
        {
            AZ::TransformNotificationBus::MultiHandler::BusConnect(m_boundPlacementEntityId);
        }
        AZ::Render::MeshComponentNotificationBus::Handler::BusConnect(m_boundPlacementEntityId);
        if (AZ::NonUniformScaleRequestBus::HasHandlers(m_boundPlacementEntityId))
        {
            AZ::NonUniformScaleRequestBus::Event(
                m_boundPlacementEntityId, &AZ::NonUniformScaleRequests::RegisterScaleChangedEvent, m_nonUniformScaleChangedHandler);
        }
    }

    void TerrainMeshHeightStampComponent::UnbindPlacementEntity()
    {
        m_nonUniformScaleChangedHandler.Disconnect();
        AZ::Render::MeshComponentNotificationBus::Handler::BusDisconnect();
        AZ::TransformNotificationBus::MultiHandler::BusDisconnect();
        m_transformNotificationsBound = false;
        m_boundPlacementEntityId.SetInvalid();
    }

    void TerrainMeshHeightStampComponent::UpdateRegistration()
    {
        if (!m_controlThread.Check() || !m_activeEntityId.IsValid())
            return;
        if (m_matchingMeshCount != 1 || !m_boundPlacementEntityId.IsValid())
        {
            m_registration.Deactivate();
            return;
        }
        AZ::Transform world = AZ::Transform::CreateIdentity();
        const bool available = AZ::TransformBus::HasHandlers(m_boundPlacementEntityId);
        if (available)
        {
            AZ::TransformBus::EventResult(world, m_boundPlacementEntityId, &AZ::TransformBus::Events::GetWorldTM);
        }
        const auto configuration = GetRegistrationConfiguration();
        const bool pending = m_editor && HeightmapStampIdentityInterface::Get() && configuration.m_stableOrderKey.empty();
        const bool hasNonUniformScale = AZ::NonUniformScaleRequestBus::HasHandlers(m_boundPlacementEntityId);
        if (m_registration.IsActive())
        {
            m_registration.Update(configuration, world, available, pending, hasNonUniformScale);
        }
        else
        {
            m_registration.Activate(m_activeEntityId, configuration, world, available, pending, hasNonUniformScale);
        }
    }

    TerrainMeshHeightStampConfig TerrainMeshHeightStampComponent::GetRegistrationConfiguration() const
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

    void TerrainMeshHeightStampComponent::OnStampIdentitiesChanged()
    {
        UpdateRegistration();
    }
    void TerrainMeshHeightStampComponent::OnTransformChanged(
        [[maybe_unused]] const AZ::Transform& local, [[maybe_unused]] const AZ::Transform& world)
    {
        UpdateRegistration();
    }
    void TerrainMeshHeightStampComponent::OnParentChanged([[maybe_unused]] AZ::EntityId oldParent, [[maybe_unused]] AZ::EntityId newParent)
    {
        SchedulePlacementEntityUpdate();
    }
    void TerrainMeshHeightStampComponent::OnChildAdded([[maybe_unused]] AZ::EntityId child)
    {
        SchedulePlacementEntityUpdate();
    }
    void TerrainMeshHeightStampComponent::OnChildRemoved([[maybe_unused]] AZ::EntityId child)
    {
        SchedulePlacementEntityUpdate();
    }
    void TerrainMeshHeightStampComponent::OnModelReady(
        [[maybe_unused]] const AZ::Data::Asset<AZ::RPI::ModelAsset>& modelAsset,
        [[maybe_unused]] const AZ::Data::Instance<AZ::RPI::Model>& model)
    {
        QueuePlacementEntityUpdate();
    }
    void TerrainMeshHeightStampComponent::OnModelPreDestroy()
    {
        QueuePlacementEntityUpdate();
    }
} // namespace TerrainCompositor
