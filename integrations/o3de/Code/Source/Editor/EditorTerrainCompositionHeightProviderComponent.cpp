#include "EditorTerrainCompositionHeightProviderComponent.h"
#include "../ComponentConfiguration.h"
#include "EditorConfiguration.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>

namespace TerrainCompositor
{
    void EditorTerrainCompositionHeightProviderComponent::Reflect(AZ::ReflectContext* context)
    {
        Internal::ReflectEditorConfiguration<EditorTerrainCompositionHeightProviderComponent, BaseClass>(context,
            &EditorTerrainCompositionHeightProviderComponent::m_configuration, &EditorTerrainCompositionHeightProviderComponent::OnConfigurationChanged,
            &EditorTerrainCompositionHeightProviderComponent::GetStatusText, "Terrain Composition Height Provider",
            "Provides the composition's world height and explicit terrain holes on this Terrain Layer Spawner region.",
            "Composition to query for height and terrain existence.",
            "Read-only provider status.");
    }

    void EditorTerrainCompositionHeightProviderComponent::GetProvidedServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainCompositionHeightProviderComponent::GetProvidedServices(services); }
    void EditorTerrainCompositionHeightProviderComponent::GetIncompatibleServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainCompositionHeightProviderComponent::GetIncompatibleServices(services); }
    void EditorTerrainCompositionHeightProviderComponent::GetRequiredServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainCompositionHeightProviderComponent::GetRequiredServices(services); }
    void EditorTerrainCompositionHeightProviderComponent::GetDependentServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainCompositionHeightProviderComponent::GetDependentServices(services); }

    void EditorTerrainCompositionHeightProviderComponent::Activate()
    {
        BaseClass::Activate();
        m_preview.Activate(m_configuration);
    }

    void EditorTerrainCompositionHeightProviderComponent::Deactivate()
    {
        m_preview.Deactivate();
        BaseClass::Deactivate();
    }

    void EditorTerrainCompositionHeightProviderComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        gameEntity->CreateComponent<TerrainCompositionHeightProviderComponent>(m_configuration);
    }

    AZ::TypeId EditorTerrainCompositionHeightProviderComponent::GetUnderlyingComponentType() const
    {
        return azrtti_typeid<TerrainCompositionHeightProviderComponent>();
    }

    bool EditorTerrainCompositionHeightProviderComponent::ReadInConfig(const AZ::ComponentConfig* configuration)
    {
        return Internal::ReadConfiguration(configuration, m_configuration, [this] { OnConfigurationChanged(); });
    }

    bool EditorTerrainCompositionHeightProviderComponent::WriteOutConfig(AZ::ComponentConfig* configuration) const
    {
        return Internal::WriteConfiguration(configuration, m_configuration);
    }

    AZ::u32 EditorTerrainCompositionHeightProviderComponent::OnConfigurationChanged()
    {
        return Internal::RefreshEditorConfiguration(m_preview, m_configuration);
    }

} // namespace TerrainCompositor
