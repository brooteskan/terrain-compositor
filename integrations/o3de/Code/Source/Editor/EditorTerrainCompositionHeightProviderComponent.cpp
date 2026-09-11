#include "EditorTerrainCompositionHeightProviderComponent.h"
#include "../ComponentConfiguration.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>

namespace TerrainCompositor
{
    void EditorTerrainCompositionHeightProviderComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection()) { TerrainCompositionHeightProviderConfig::Reflect(context); }
            serialize->Class<EditorTerrainCompositionHeightProviderComponent, BaseClass>()
                ->Version(1)
                ->Field("Configuration", &EditorTerrainCompositionHeightProviderComponent::m_configuration);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<EditorTerrainCompositionHeightProviderComponent>("Terrain Composition Height Provider",
                    "Provides the composition's world height and explicit terrain holes on this Terrain Layer Spawner region.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Terrain")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default,
                        &EditorTerrainCompositionHeightProviderComponent::m_configuration,
                        "Configuration", "Composition to query for height and terrain existence.")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify,
                        &EditorTerrainCompositionHeightProviderComponent::OnConfigurationChanged)
                    ->UIElement(AZ::Edit::UIHandlers::Label, "Status", "Read-only provider status.")
                    ->Attribute(AZ::Edit::Attributes::ValueText,
                        &EditorTerrainCompositionHeightProviderComponent::GetStatusText);
            }
        }
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
        return Internal::ReadConfiguration<TerrainCompositionHeightProviderConfig>(configuration, [this](const auto& value)
        {
            m_configuration = value;
            OnConfigurationChanged();
        });
    }

    bool EditorTerrainCompositionHeightProviderComponent::WriteOutConfig(AZ::ComponentConfig* configuration) const
    {
        return Internal::WriteConfiguration(configuration, m_configuration);
    }

    AZ::u32 EditorTerrainCompositionHeightProviderComponent::OnConfigurationChanged()
    {
        m_preview.Refresh(m_configuration);
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }

} // namespace TerrainCompositor
