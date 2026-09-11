#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/TransformBus.h>
#include <AzFramework/Components/EditorEntityEvents.h>
#include <TerrainCompositor/HeightmapStampRegistration.h>
#include <TerrainCompositor/HeightmapStampIdentity.h>

namespace TerrainCompositor
{
    //! Authoring/control-plane component. Height queries only read the compositor's published value records.
    class HeightmapStampComponent final
        : public AZ::Component
        , public AzFramework::EditorEntityEvents
        , private AZ::TransformNotificationBus::Handler
        , private HeightmapStampIdentityNotificationBus::Handler
    {
    public:
        AZ_COMPONENT_DECL(HeightmapStampComponent);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent);

        HeightmapStampComponent() = default;
        explicit HeightmapStampComponent(const HeightmapStampConfig& configuration);
        void Activate() override;
        void Deactivate() override;
        void EditorActivate(AZ::EntityId entityId) override;
        void EditorDeactivate(AZ::EntityId entityId) override;
        bool ReadInConfig(const AZ::ComponentConfig* baseConfig) override;
        bool WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const override;

        //! Main-thread runtime update entry point, also used by configuration loading and the inspector.
        //! Invalid settings remain editable but remove the contribution; correcting them restores it.
        void SetStampConfiguration(const HeightmapStampConfig& configuration);
        HeightmapStampConfig GetStampConfiguration() const;
        AZStd::string GetStatusMessage() const;

    private:
        void StartStamp(AZ::EntityId entityId, bool editor = false);
        AZ::u32 OnConfigurationChanged();
        void UpdateRegistration();
        HeightmapStampConfig GetRegistrationConfiguration() const;
        void OnStampIdentitiesChanged() override;
        void OnTransformChanged(const AZ::Transform& local, const AZ::Transform& world) override;
        void OnParentChanged(AZ::EntityId oldParent, AZ::EntityId newParent) override;

        HeightmapStampConfig m_configuration;
        HeightmapControlThread m_controlThread;
        HeightmapStampRegistration m_registration;
        AZ::EntityId m_activeEntityId{};
        bool m_editor = false;
    };
} // namespace TerrainCompositor
