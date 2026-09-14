#include <TerrainCompositor/Internal/MeshPlacementLifecycle.h>
#include "Components/MeshPlacementHelpers.h"

#include <AzCore/std/smart_ptr/make_shared.h>

namespace TerrainCompositor::Internal
{
    const char* MeshPlacementLifecycle::GetPlacementStatus(const char* waiting, const char* missing, const char* ambiguous) const
    {
        if (m_matchingMeshCount == 0) return IsWaitingForPlacement() ? waiting : missing;
        return m_matchingMeshCount > 1 ? ambiguous : nullptr;
    }

    MeshPlacementLifecycle::MeshPlacementLifecycle(AZStd::function<void()> updateRegistration)
        : m_updateRegistration(AZStd::move(updateRegistration))
        , m_scaleChangedHandler([this](const AZ::Vector3&) { m_updateRegistration(); })
    {
    }

    MeshPlacementLifecycle::~MeshPlacementLifecycle()
    {
        Stop();
    }

    void MeshPlacementLifecycle::Start(AZ::EntityId entityId, const AZ::Data::AssetId& assetId, bool editor)
    {
        Stop();
        m_entityId = entityId;
        m_assetId = assetId;
        m_editor = editor;
        m_deferredUpdateState = AZStd::make_shared<DeferredUpdateState>();
        m_deferredUpdateState->m_owner.store(this);
        if (editor) HeightmapStampIdentityNotificationBus::Handler::BusConnect();
        BindPlacementEntity();
        m_updateRegistration();
        SchedulePlacementUpdate();
        ScheduleVisibilityUpdate();
    }

    void MeshPlacementLifecycle::Stop()
    {
        if (m_deferredUpdateState) m_deferredUpdateState->m_owner.store(nullptr);
        AZ::SystemTickBus::Handler::BusDisconnect();
        m_placementRetriesRemaining = m_visibilityRetriesRemaining = 0;
        RestoreModelVisibility(m_previousVisibility);
        HeightmapStampIdentityNotificationBus::Handler::BusDisconnect();
        UnbindPlacementEntity();
        m_deferredUpdateState.reset();
        m_matchingMeshCount = 0;
        m_entityId.SetInvalid();
        m_editor = false;
    }

    void MeshPlacementLifecycle::Refresh(const AZ::Data::AssetId& assetId)
    {
        m_assetId = assetId;
        if (!m_editor) RestoreModelVisibility(m_previousVisibility);
        BindPlacementEntity();
        m_updateRegistration();
        SchedulePlacementUpdate();
        ScheduleVisibilityUpdate();
    }

    void MeshPlacementLifecycle::SchedulePlacementUpdate()
    {
        // Child entities and Mesh request handlers can activate after the owner.
        m_placementRetriesRemaining = 8;
        if (!AZ::SystemTickBus::Handler::BusIsConnected()) AZ::SystemTickBus::Handler::BusConnect();
    }

    void MeshPlacementLifecycle::ScheduleVisibilityUpdate()
    {
        if (m_editor) return;
        m_visibilityRetriesRemaining = 8;
        if (!AZ::SystemTickBus::Handler::BusIsConnected()) AZ::SystemTickBus::Handler::BusConnect();
    }

    void MeshPlacementLifecycle::QueueModelUpdate()
    {
        // Model notifications may run on workers. A token per activation prevents stale callbacks from reviving a new session.
        const AZStd::weak_ptr<DeferredUpdateState> weak = m_deferredUpdateState;
        AZ::SystemTickBus::QueueFunction([weak]()
        {
            if (const auto state = weak.lock())
            {
                if (auto* owner = state->m_owner.load())
                {
                    owner->SchedulePlacementUpdate();
                    owner->ScheduleVisibilityUpdate();
                }
            }
        });
    }

    void MeshPlacementLifecycle::OnModelReady(
        const AZ::Data::Asset<AZ::RPI::ModelAsset>&, const AZ::Data::Instance<AZ::RPI::Model>&)
    {
        QueueModelUpdate();
    }

    void MeshPlacementLifecycle::OnSystemTick()
    {
        if (!IsActive())
        {
            AZ::SystemTickBus::Handler::BusDisconnect();
            return;
        }
        if (m_placementRetriesRemaining > 0)
        {
            BindPlacementEntity();
            m_updateRegistration();
            --m_placementRetriesRemaining;
        }
        if (m_visibilityRetriesRemaining > 0)
        {
            HideMatchingModel(m_placementEntityId, m_assetId, m_previousVisibility);
            --m_visibilityRetriesRemaining;
        }
        if (m_placementRetriesRemaining == 0 && m_visibilityRetriesRemaining == 0)
            AZ::SystemTickBus::Handler::BusDisconnect();
    }

    void MeshPlacementLifecycle::BindPlacementEntity()
    {
        if (!IsActive()) return;
        size_t matchingMeshCount = 0;
        const AZ::EntityId desired = ResolveUniqueModelEntity(m_entityId, m_assetId, matchingMeshCount);
        if (desired == m_placementEntityId && matchingMeshCount == m_matchingMeshCount && m_transformNotificationsBound)
            return;

        if (!m_editor) RestoreModelVisibility(m_previousVisibility);
        UnbindPlacementEntity();
        m_matchingMeshCount = matchingMeshCount;
        m_placementEntityId = desired;
        AZ::TransformNotificationBus::MultiHandler::BusConnect(m_entityId);
        m_transformNotificationsBound = true;
        if (!m_placementEntityId.IsValid()) return;
        if (m_placementEntityId != m_entityId)
            AZ::TransformNotificationBus::MultiHandler::BusConnect(m_placementEntityId);
        AZ::Render::MeshComponentNotificationBus::Handler::BusConnect(m_placementEntityId);
        if (AZ::NonUniformScaleRequestBus::HasHandlers(m_placementEntityId))
        {
            AZ::NonUniformScaleRequestBus::Event(m_placementEntityId,
                &AZ::NonUniformScaleRequests::RegisterScaleChangedEvent, m_scaleChangedHandler);
        }
    }

    void MeshPlacementLifecycle::UnbindPlacementEntity()
    {
        m_scaleChangedHandler.Disconnect();
        AZ::Render::MeshComponentNotificationBus::Handler::BusDisconnect();
        AZ::TransformNotificationBus::MultiHandler::BusDisconnect();
        m_transformNotificationsBound = false;
        m_placementEntityId.SetInvalid();
    }
}
