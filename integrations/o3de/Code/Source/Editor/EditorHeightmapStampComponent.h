#pragma once

#include <AzCore/Component/TickBus.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzFramework/Entity/EntityDebugDisplayBus.h>
#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>
#include <TerrainCompositor/Components/HeightmapStampComponent.h>

namespace TerrainCompositor
{
    //! Serialized authoring data is separate from the nonserialized runtime preview and its query state.
    class EditorHeightmapStampComponent final
        : public AzToolsFramework::Components::EditorComponentBase
        , private AzFramework::EntityDebugDisplayEventBus::Handler
        , private AZ::TickBus::Handler
    {
    public:
        using BaseClass = AzToolsFramework::Components::EditorComponentBase;
        // Intentional reflected editor adapter; component identity and Configuration ownership must remain explicit.
        /* jscpd:ignore-start */
        AZ_COMPONENT(EditorHeightmapStampComponent, EditorHeightmapStampComponentTypeId, BaseClass);
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

        HeightmapStampConfig GetStampConfiguration() const { return m_configuration; }
        //! Only the inactive conversion copy may receive a baked key. Never dirty the live authoring template.
        void SetExportOrderKey(const AZStd::string& key);

    private:
        AZ::u32 OnConfigurationChanged();
        AZStd::string GetStatusText() const { return m_status; }
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
        void DisplayEntityViewport(const AzFramework::ViewportInfo& viewportInfo,
            AzFramework::DebugDisplayRequests& debugDisplay) override;

        HeightmapStampConfig m_configuration;
        AZStd::unique_ptr<HeightmapStampComponent> m_preview;
        AZStd::string m_status = "Inactive: no stamp contribution.";
        float m_statusElapsed = 0.0f;
    };
} // namespace TerrainCompositor
