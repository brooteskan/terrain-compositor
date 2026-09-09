#pragma once

#include <AtomLyIntegration/CommonFeatures/Mesh/MeshComponentBus.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/parallel/atomic.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>
#include <AzFramework/Components/EditorEntityEvents.h>
#include <TerrainCompositor/HeightmapStampIdentity.h>
#include <TerrainCompositor/TerrainMeshCutoutRegistration.h>

namespace TerrainCompositor
{
    class TerrainMeshCutoutComponent final
        : public AZ::Component
        , public AzFramework::EditorEntityEvents
        , private AZ::TransformNotificationBus::MultiHandler
        , private AZ::Render::MeshComponentNotificationBus::Handler
        , private HeightmapStampIdentityNotificationBus::Handler
        , private AZ::SystemTickBus::Handler
    {
    public:
        AZ_COMPONENT_DECL(TerrainMeshCutoutComponent);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent);

        TerrainMeshCutoutComponent();
        explicit TerrainMeshCutoutComponent(const TerrainMeshCutoutConfig& configuration);
        void Activate() override;
        void Deactivate() override;
        void EditorActivate(AZ::EntityId entityId) override;
        void EditorDeactivate(AZ::EntityId entityId) override;
        bool ReadInConfig(const AZ::ComponentConfig* configuration) override;
        bool WriteOutConfig(AZ::ComponentConfig* configuration) const override;

        void SetCutoutConfiguration(const TerrainMeshCutoutConfig& configuration);
        TerrainMeshCutoutConfig GetCutoutConfiguration() const;
        AZStd::string GetStatusMessage() const;

    private:
        void StartCutout(AZ::EntityId entityId, bool editor = false);
        void StopCutout();
        AZ::EntityId ResolvePlacementEntity(size_t& matchingMeshCount) const;
        void BindPlacementEntity();
        void UnbindPlacementEntity();
        void SchedulePlacementEntityUpdate();
        void QueuePlacementEntityUpdate();
        void ScheduleRuntimeMeshVisibilityUpdate();
        void HideRuntimeCutoutMeshInstances();
        void RestoreRuntimeCutoutMeshVisibility();
        void OnSystemTick() override;
        AZ::u32 OnConfigurationChanged();
        void UpdateRegistration();
        TerrainMeshCutoutConfig GetRegistrationConfiguration() const;
        void OnStampIdentitiesChanged() override;
        void OnTransformChanged(const AZ::Transform& local, const AZ::Transform& world) override;
        void OnParentChanged(AZ::EntityId oldParent, AZ::EntityId newParent) override;
        void OnChildAdded(AZ::EntityId child) override;
        void OnChildRemoved(AZ::EntityId child) override;
        void OnModelReady(const AZ::Data::Asset<AZ::RPI::ModelAsset>& modelAsset,
            const AZ::Data::Instance<AZ::RPI::Model>& model) override;
        void OnModelPreDestroy() override;

        struct DeferredUpdateState
        {
            AZStd::atomic<TerrainMeshCutoutComponent*> m_component{ nullptr };
        };

        TerrainMeshCutoutConfig m_configuration;
        HeightmapControlThread m_controlThread;
        TerrainMeshCutoutRegistration m_registration;
        AZ::EntityId m_activeEntityId{};
        AZ::EntityId m_boundPlacementEntityId{};
        AZ::NonUniformScaleChangedEvent::Handler m_nonUniformScaleChangedHandler;
        AZStd::shared_ptr<DeferredUpdateState> m_deferredUpdateState;
        AZStd::unordered_map<AZ::EntityId, bool> m_runtimeMeshPreviousVisibility;
        size_t m_matchingMeshCount = 0;
        AZ::u8 m_placementRetriesRemaining = 0;
        AZ::u8 m_runtimeVisibilityRetriesRemaining = 0;
        bool m_transformNotificationsBound = false;
        bool m_editor = false;
    };
} // namespace TerrainCompositor
