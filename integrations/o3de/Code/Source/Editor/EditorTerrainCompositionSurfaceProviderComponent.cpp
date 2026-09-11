#include "EditorTerrainCompositionSurfaceProviderComponent.h"
#include "../ComponentConfiguration.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>

namespace TerrainCompositor
{
    void EditorTerrainCompositionSurfaceProviderComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection())
            {
                TerrainCompositionSurfaceProviderConfig::Reflect(context);
            }
            serialize->Class<EditorTerrainCompositionSurfaceProviderComponent, BaseClass>()
                ->Version(1)
                ->Field("Configuration", &EditorTerrainCompositionSurfaceProviderComponent::m_configuration);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<EditorTerrainCompositionSurfaceProviderComponent>("Terrain Composition Surface Provider",
                    "Provides the composition's categorical surface weights on this Terrain Layer Spawner region.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Terrain")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default,
                        &EditorTerrainCompositionSurfaceProviderComponent::m_configuration,
                        "Configuration", "Composition to query for the region's surface weights.")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify,
                        &EditorTerrainCompositionSurfaceProviderComponent::OnConfigurationChanged)
                    ->UIElement(AZ::Edit::UIHandlers::Label, "Status", "Read-only provider status.")
                    ->Attribute(AZ::Edit::Attributes::ValueText,
                        &EditorTerrainCompositionSurfaceProviderComponent::GetStatusText);
            }
        }
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
        return Internal::ReadConfiguration<TerrainCompositionSurfaceProviderConfig>(configuration, [this](const auto& value)
        {
            m_configuration = value;
            OnConfigurationChanged();
        });
    }

    bool EditorTerrainCompositionSurfaceProviderComponent::WriteOutConfig(AZ::ComponentConfig* configuration) const
    {
        return Internal::WriteConfiguration(configuration, m_configuration);
    }

    AZ::u32 EditorTerrainCompositionSurfaceProviderComponent::OnConfigurationChanged()
    {
        m_preview.Refresh(m_configuration);
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }

} // namespace TerrainCompositor
