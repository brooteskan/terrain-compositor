#pragma once

#include <AzCore/Component/TickBus.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzFramework/Entity/EntityDebugDisplayBus.h>
#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>
#include <TerrainCompositor/Components/TerrainMeshHeightStampComponent.h>

namespace TerrainCompositor
{
    class EditorTerrainMeshHeightStampComponent final
        : public AzToolsFramework::Components::EditorComponentBase
        , private AzFramework::EntityDebugDisplayEventBus::Handler
        , private AZ::TickBus::Handler
    {
    public:
        using BaseClass = AzToolsFramework::Components::EditorComponentBase;
        AZ_COMPONENT(EditorTerrainMeshHeightStampComponent, EditorTerrainMeshHeightStampComponentTypeId, BaseClass);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& services);

        void Activate() override;
        void Deactivate() override;
        void BuildGameEntity(AZ::Entity* gameEntity) override;
        AZ::TypeId GetUnderlyingComponentType() const override;
        bool ReadInConfig(const AZ::ComponentConfig* configuration) override;
        bool WriteOutConfig(AZ::ComponentConfig* configuration) const override;

        TerrainMeshHeightStampConfig GetStampConfiguration() const
        {
            return m_configuration;
        }
        void SetExportData(const AZStd::string& key);

    private:
        AZ::u32 OnConfigurationChanged();
        AZStd::string GetStatusText() const
        {
            return m_status;
        }
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
        void DisplayEntityViewport(
            const AzFramework::ViewportInfo& viewportInfo, AzFramework::DebugDisplayRequests& debugDisplay) override;

        TerrainMeshHeightStampConfig m_configuration;
        AZStd::unique_ptr<TerrainMeshHeightStampComponent> m_preview;
        AZStd::string m_status = "Inactive: no terrain mesh height contribution.";
        float m_statusElapsed = 0.0f;
    };
} // namespace TerrainCompositor
