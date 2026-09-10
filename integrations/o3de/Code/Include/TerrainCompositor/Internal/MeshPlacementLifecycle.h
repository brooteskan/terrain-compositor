#pragma once

#include <AtomLyIntegration/CommonFeatures/Mesh/MeshComponentBus.h>
#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/function/function_template.h>
#include <AzCore/std/parallel/atomic.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>
#include <TerrainCompositor/HeightmapStampIdentity.h>

namespace TerrainCompositor::Internal
{
    // Owns mesh placement and source visibility on the control thread. Components retain configuration and typed clients.
    class MeshPlacementLifecycle final
        : private AZ::TransformNotificationBus::MultiHandler
        , private AZ::Render::MeshComponentNotificationBus::Handler
        , private HeightmapStampIdentityNotificationBus::Handler
        , private AZ::SystemTickBus::Handler
    {
    public:
        explicit MeshPlacementLifecycle(AZStd::function<void()> updateRegistration);
        ~MeshPlacementLifecycle();
        MeshPlacementLifecycle(const MeshPlacementLifecycle&) = delete;
        MeshPlacementLifecycle& operator=(const MeshPlacementLifecycle&) = delete;

        void Start(AZ::EntityId entityId, const AZ::Data::AssetId& assetId, bool editor);
        void Stop();
        void Refresh(const AZ::Data::AssetId& assetId);
        bool IsActive() const { return m_entityId.IsValid(); }
        size_t GetMatchingMeshCount() const { return m_matchingMeshCount; }
        bool IsWaitingForPlacement() const { return m_placementRetriesRemaining > 0; }

        template<class Registration, class Configuration>
        void UpdateRegistration(Registration& registration, Configuration configuration) const
        {
            if (!IsActive()) return;
            if (m_matchingMeshCount != 1 || !m_placementEntityId.IsValid())
            {
                registration.Deactivate();
                return;
            }
            AZ::Transform world = AZ::Transform::CreateIdentity();
            const bool available = AZ::TransformBus::HasHandlers(m_placementEntityId);
            if (available)
                AZ::TransformBus::EventResult(world, m_placementEntityId, &AZ::TransformBus::Events::GetWorldTM);
            if (m_editor)
            {
                configuration.m_orderingId = {};
                const auto* resolver = HeightmapStampIdentityInterface::Get();
                configuration.m_stableOrderKey = resolver ? resolver->ResolveStampOrderKey(m_entityId) : AZStd::string{};
            }
            const bool pending = m_editor && HeightmapStampIdentityInterface::Get() && configuration.m_stableOrderKey.empty();
            const bool nonUniformScale = AZ::NonUniformScaleRequestBus::HasHandlers(m_placementEntityId);
            if (registration.IsActive())
                registration.Update(configuration, world, available, pending, nonUniformScale);
            else
                registration.Activate(m_entityId, configuration, world, available, pending, nonUniformScale);
        }

    private:
        void BindPlacementEntity();
        void UnbindPlacementEntity();
        void SchedulePlacementUpdate();
        void ScheduleVisibilityUpdate();
        void QueueModelUpdate();
        void OnSystemTick() override;
        void OnStampIdentitiesChanged() override { m_updateRegistration(); }
        void OnTransformChanged(const AZ::Transform&, const AZ::Transform&) override { m_updateRegistration(); }
        void OnParentChanged(AZ::EntityId, AZ::EntityId) override { SchedulePlacementUpdate(); }
        void OnChildAdded(AZ::EntityId) override { SchedulePlacementUpdate(); }
        void OnChildRemoved(AZ::EntityId) override { SchedulePlacementUpdate(); }
        void OnModelReady(const AZ::Data::Asset<AZ::RPI::ModelAsset>&, const AZ::Data::Instance<AZ::RPI::Model>&) override;
        void OnModelPreDestroy() override { QueueModelUpdate(); }

        struct DeferredUpdateState
        {
            AZStd::atomic<MeshPlacementLifecycle*> m_owner{ nullptr };
        };

        AZStd::function<void()> m_updateRegistration;
        AZ::NonUniformScaleChangedEvent::Handler m_scaleChangedHandler;
        AZStd::shared_ptr<DeferredUpdateState> m_deferredUpdateState;
        AZStd::unordered_map<AZ::EntityId, bool> m_previousVisibility;
        AZ::EntityId m_entityId, m_placementEntityId;
        AZ::Data::AssetId m_assetId;
        size_t m_matchingMeshCount = 0;
        AZ::u8 m_placementRetriesRemaining = 0;
        AZ::u8 m_visibilityRetriesRemaining = 0;
        bool m_transformNotificationsBound = false;
        bool m_editor = false;
    };
}
