#pragma once
#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/any.h>
#include <AzFramework/Components/EditorEntityEvents.h>
#include <AtomLyIntegration/CommonFeatures/Material/MaterialComponentBus.h>
#include <TerrainCompositor/TerrainCompositionBus.h>

namespace TerrainCompositor
{
    class TerrainMeshBlendRequests : public AZ::ComponentBus
    {
    public:
        virtual float GetBlendDistance() const = 0;
        virtual void SetBlendDistance(float distance) = 0;
        virtual bool IsRegistered() const = 0;
        virtual AZ::u32 GetAppliedMaterialCount() const = 0;
        virtual float GetAppliedBlendDistance() const = 0;
        virtual AZStd::string GetStatus() const = 0;
    };
    using TerrainMeshBlendRequestBus = AZ::EBus<TerrainMeshBlendRequests>;

    //! Event-driven prefab-root authoring and runtime owner for retained mesh blending.
    class TerrainMeshBlendComponent final
        : public AZ::Component
        , public AzFramework::EditorEntityEvents
        , private TerrainMeshBlendRequestBus::Handler
        , private TerrainCompositionNotificationBus::Handler
        , private AZ::TransformNotificationBus::Handler
        , private AZ::SystemTickBus::Handler
        , private AZ::Render::MaterialComponentNotificationBus::MultiHandler
    {
    public:
        AZ_COMPONENT(TerrainMeshBlendComponent,"{D69DFED0-57AC-595C-922A-343D90C4B20B}",AzFramework::EditorEntityEvents);
        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        void Activate() override;
        void Deactivate() override;
        void EditorActivate(AZ::EntityId id) override;
        void EditorDeactivate(AZ::EntityId) override;

    private:
        struct PreviousOverride
        {
            AZ::EntityId m_entity;
            AZ::Render::MaterialAssignmentId m_slot;
            float m_value=0.f;
        };
        float GetBlendDistance() const override { return m_blendDistance; }
        void SetBlendDistance(float distance) override;
        bool IsRegistered() const override { return m_registered; }
        AZ::u32 GetAppliedMaterialCount() const override { return m_appliedCount; }
        float GetAppliedBlendDistance() const override { return m_lastAppliedDistance; }
        AZStd::string GetStatus() const override;
        AZ::u32 OnConfigurationChanged();
        void ConnectTarget();
        void DisconnectTarget();
        void Refresh();
        void RestoreOverrides();
        void OnCompositionAvailable(const AZ::Uuid&) override { Refresh(); }
        void OnCompositionUnavailable(const AZ::Uuid&) override { m_session={}; m_registered=false; Refresh(); }
        void QueueRefresh();
        void OnSystemTick() override;
        void OnTransformChanged(const AZ::Transform&,const AZ::Transform&) override { QueueRefresh(); }
        void OnChildAdded(AZ::EntityId) override { QueueRefresh(); }
        void OnChildRemoved(AZ::EntityId) override { QueueRefresh(); }
        void OnMaterialsCreated(const AZ::Render::MaterialAssignmentMap&) override { Refresh(); }
        void OnMaterialsUpdated(const AZ::Render::MaterialAssignmentMap&) override { Refresh(); }
        void OnMaterialSlotLayoutChanged() override { Refresh(); }
        AZ::EntityId m_targetComposition;
        float m_blendDistance=.15f;
        AZ::EntityId m_owner;
        TerrainCompositionAddress m_address;
        AZ::Uuid m_session{};
        bool m_active=false;
        bool m_refreshing=false;
        bool m_registered=false;
        AZ::u32 m_appliedCount=0;
        float m_lastAppliedDistance=-1.f;
        AZStd::vector<PreviousOverride> m_previous;
        AZStd::vector<AZ::EntityId> m_subscriptions;
    };
}
