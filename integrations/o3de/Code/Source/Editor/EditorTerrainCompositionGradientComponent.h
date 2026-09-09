#pragma once

#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>
#include <AzFramework/Entity/EntityDebugDisplayBus.h>
#include <TerrainCompositor/Components/TerrainCompositionGradientComponent.h>

namespace TerrainCompositor
{
    class EditorTerrainCompositionGradientComponent final
        : public AzToolsFramework::Components::EditorComponentBase
        , private AzFramework::EntityDebugDisplayEventBus::Handler
        , private AZ::TickBus::Handler
    {
    public:
        using BaseClass = AzToolsFramework::Components::EditorComponentBase;
        // Intentional reflected editor adapter; component identity and Configuration ownership must remain explicit.
        /* jscpd:ignore-start */
        AZ_COMPONENT(EditorTerrainCompositionGradientComponent, EditorTerrainCompositionGradientComponentTypeId, BaseClass);
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
        /* jscpd:ignore-end */

    private:
        AZ::u32 OnConfigurationChanged();
        AZStd::string GetStatusText() const { return m_status; }
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
        void DisplayEntityViewport(const AzFramework::ViewportInfo& viewportInfo,
            AzFramework::DebugDisplayRequests& debugDisplay) override;

        TerrainCompositionConfig m_configuration;
        AZStd::unique_ptr<TerrainCompositionGradientComponent> m_preview;
        AZStd::string m_status = "Inactive: no composed gradient.";
        float m_statusElapsed = 0.0f;
    };
} // namespace TerrainCompositor
