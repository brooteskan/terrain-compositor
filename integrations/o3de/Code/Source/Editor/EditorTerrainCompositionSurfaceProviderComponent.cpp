#include "EditorTerrainCompositionSurfaceProviderComponent.h"
#include "../ComponentConfiguration.h"
#include "EditorConfiguration.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>

namespace TerrainCompositor
{
    void EditorTerrainCompositionSurfaceProviderComponent::Reflect(AZ::ReflectContext* context)
    {
        Internal::ReflectEditorConfiguration<EditorTerrainCompositionSurfaceProviderComponent, BaseClass>(context,
            &EditorTerrainCompositionSurfaceProviderComponent::m_configuration, &EditorTerrainCompositionSurfaceProviderComponent::OnConfigurationChanged,
            &EditorTerrainCompositionSurfaceProviderComponent::GetStatusText, "Terrain Composition Surface Provider",
            "Provides the composition's categorical surface weights on this Terrain Layer Spawner region.",
            "Composition to query for the region's surface weights.",
            "Read-only provider status.");
    }

    void EditorTerrainCompositionSurfaceProviderComponent::GetProvidedServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainCompositionSurfaceProviderComponent::GetProvidedServices(services); }
    void EditorTerrainCompositionSurfaceProviderComponent::GetIncompatibleServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainCompositionSurfaceProviderComponent::GetIncompatibleServices(services); }
    void EditorTerrainCompositionSurfaceProviderComponent::GetRequiredServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainCompositionSurfaceProviderComponent::GetRequiredServices(services); }
    void EditorTerrainCompositionSurfaceProviderComponent::GetDependentServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainCompositionSurfaceProviderComponent::GetDependentServices(services); }

    void EditorTerrainCompositionSurfaceProviderComponent::Activate()
    {
        BaseClass::Activate();
        m_preview.Activate(m_configuration);
    }

    void EditorTerrainCompositionSurfaceProviderComponent::Deactivate()
    {
        m_preview.Deactivate();
        BaseClass::Deactivate();
    }

    void EditorTerrainCompositionSurfaceProviderComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        gameEntity->CreateComponent<TerrainCompositionSurfaceProviderComponent>(m_configuration);
    }

    AZ::TypeId EditorTerrainCompositionSurfaceProviderComponent::GetUnderlyingComponentType() const
    {
        return azrtti_typeid<TerrainCompositionSurfaceProviderComponent>();
    }

    bool EditorTerrainCompositionSurfaceProviderComponent::ReadInConfig(const AZ::ComponentConfig* configuration)
    {
        return Internal::ReadConfiguration(configuration, m_configuration, [this] { OnConfigurationChanged(); });
    }

    bool EditorTerrainCompositionSurfaceProviderComponent::WriteOutConfig(AZ::ComponentConfig* configuration) const
    {
        return Internal::WriteConfiguration(configuration, m_configuration);
    }

    AZ::u32 EditorTerrainCompositionSurfaceProviderComponent::OnConfigurationChanged()
    {
        return Internal::RefreshEditorConfiguration(m_preview, m_configuration);
    }

} // namespace TerrainCompositor
