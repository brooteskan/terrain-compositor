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
#include <TerrainCompositor/TerrainMeshHeightStampBus.h>
#include <TerrainCompositor/TerrainMeshHeightStampRegistration.h>

namespace TerrainCompositor
{
    class TerrainMeshHeightStampComponent final
        : public AZ::Component
        , public AzFramework::EditorEntityEvents
        , public TerrainMeshHeightStampRequestBus::Handler
        , private AZ::TransformNotificationBus::MultiHandler
        , private AZ::Render::MeshComponentNotificationBus::Handler
        , private HeightmapStampIdentityNotificationBus::Handler
        , private AZ::SystemTickBus::Handler
    {
    public:
        AZ_COMPONENT_DECL(TerrainMeshHeightStampComponent);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent);

        TerrainMeshHeightStampComponent();
        explicit TerrainMeshHeightStampComponent(const TerrainMeshHeightStampConfig& configuration);
        void Activate() override;
        void Deactivate() override;
        void EditorActivate(AZ::EntityId entityId) override;
        void EditorDeactivate(AZ::EntityId entityId) override;
        bool ReadInConfig(const AZ::ComponentConfig* configuration) override;
        bool WriteOutConfig(AZ::ComponentConfig* configuration) const override;

        void SetStampConfiguration(const TerrainMeshHeightStampConfig& configuration);
        TerrainMeshHeightStampConfig GetStampConfiguration() const;
        AZStd::string GetStatusMessage() const override;
        TerrainMeshHeightStampRegistrationData GetDiagnosticRegistration() const;

    private:
        void StartStamp(AZ::EntityId entityId, bool editor = false);
        void StopStamp();
        AZ::EntityId ResolvePlacementEntity(size_t& matchingMeshCount) const;
        void BindPlacementEntity();
        void UnbindPlacementEntity();
        void SchedulePlacementEntityUpdate();
        void QueuePlacementEntityUpdate();
        void ScheduleRuntimeMeshVisibilityUpdate();
        void HideRuntimeSourceMesh();
        void RestoreRuntimeSourceMeshVisibility();
        void OnSystemTick() override;
        AZ::u32 OnConfigurationChanged();
        void UpdateRegistration();
        TerrainMeshHeightStampConfig GetRegistrationConfiguration() const;
        void OnStampIdentitiesChanged() override;
        void OnTransformChanged(const AZ::Transform& local, const AZ::Transform& world) override;
        void OnParentChanged(AZ::EntityId oldParent, AZ::EntityId newParent) override;
        void OnChildAdded(AZ::EntityId child) override;
        void OnChildRemoved(AZ::EntityId child) override;
        void OnModelReady(const AZ::Data::Asset<AZ::RPI::ModelAsset>& modelAsset, const AZ::Data::Instance<AZ::RPI::Model>& model) override;
        void OnModelPreDestroy() override;

        struct DeferredUpdateState
        {
            AZStd::atomic<TerrainMeshHeightStampComponent*> m_component{ nullptr };
        };

        TerrainMeshHeightStampConfig m_configuration;
        HeightmapControlThread m_controlThread;
        TerrainMeshHeightStampRegistration m_registration;
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
