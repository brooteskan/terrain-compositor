#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Component/Component.h>
#include <AzFramework/Components/EditorEntityEvents.h>
#include <TerrainCompositor/Internal/MeshPlacementLifecycle.h>
#include <TerrainCompositor/TerrainMeshHeightStampRegistration.h>
#include <TerrainCompositor/TerrainMeshHeightStampBus.h>

namespace TerrainCompositor
{
    class TerrainMeshHeightStampComponent final
        : public AZ::Component
        , public AzFramework::EditorEntityEvents
        , public TerrainMeshHeightStampRequestBus::Handler
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
        AZ::u32 OnConfigurationChanged();
        void UpdateRegistration();

        TerrainMeshHeightStampConfig m_configuration;
        HeightmapControlThread m_controlThread;
        TerrainMeshHeightStampRegistration m_registration;
        Internal::MeshPlacementLifecycle m_placement{ [this] { UpdateRegistration(); } };
    };
} // namespace TerrainCompositor
