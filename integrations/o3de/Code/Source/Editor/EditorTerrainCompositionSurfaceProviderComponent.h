#pragma once

#include "EditorPreviewStatus.h"
#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>
#include <TerrainCompositor/Components/TerrainCompositionSurfaceProviderComponent.h>

namespace TerrainCompositor
{
    class EditorTerrainCompositionSurfaceProviderComponent final
        : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        using BaseClass = AzToolsFramework::Components::EditorComponentBase;
        // Intentional reflected editor adapter; component identity and Configuration ownership must remain explicit.
        AZ_COMPONENT(EditorTerrainCompositionSurfaceProviderComponent,
            EditorTerrainCompositionSurfaceProviderComponentTypeId, BaseClass);

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

    private:
        template<class> friend class TerrainEditorPreviewLifecycleTests;
        AZ::u32 OnConfigurationChanged();
        AZStd::string GetStatusText() const { return m_preview.GetStatus(); }

        TerrainCompositionSurfaceProviderConfig m_configuration;
        EditorPreview<TerrainCompositionSurfaceProviderComponent> m_preview{ *this, "Inactive: no terrain surface provider." };
    };
} // namespace TerrainCompositor
