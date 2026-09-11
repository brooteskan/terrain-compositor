#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Component/Component.h>
#include <AzFramework/Components/EditorEntityEvents.h>
#include <TerrainCompositor/Internal/MeshPlacementLifecycle.h>
#include <TerrainCompositor/TerrainMeshCutoutRegistration.h>

namespace TerrainCompositor
{
    class TerrainMeshCutoutComponent final
        : public AZ::Component
        , public AzFramework::EditorEntityEvents
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
        AZ::u32 OnConfigurationChanged();
        void UpdateRegistration();

        TerrainMeshCutoutConfig m_configuration;
        HeightmapControlThread m_controlThread;
        TerrainMeshCutoutRegistration m_registration;
        Internal::MeshPlacementLifecycle m_placement{ [this] { UpdateRegistration(); } };
    };
} // namespace TerrainCompositor
