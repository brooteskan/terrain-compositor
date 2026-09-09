#include "EditorTerrainCompositionHeightProviderComponent.h"
#include "EditorPreviewStatus.h"

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
        m_preview = AZStd::make_unique<TerrainCompositionHeightProviderComponent>(m_configuration);
        m_preview->EditorActivate(GetEntityId());
        m_status = m_preview->GetStatusMessage();
        m_statusElapsed = 0.0f;
        AZ::TickBus::Handler::BusConnect();
    }

    void EditorTerrainCompositionHeightProviderComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        if (m_preview)
        {
            m_preview->EditorDeactivate(GetEntityId());
            m_preview.reset();
        }
        m_status = "Inactive: no terrain height provider.";
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
        if (const auto* provider = azrtti_cast<const TerrainCompositionHeightProviderConfig*>(configuration))
        {
            m_configuration = *provider;
            OnConfigurationChanged();
            return true;
        }
        return false;
    }

    bool EditorTerrainCompositionHeightProviderComponent::WriteOutConfig(AZ::ComponentConfig* configuration) const
    {
        if (auto* provider = azrtti_cast<TerrainCompositionHeightProviderConfig*>(configuration))
        {
            *provider = m_configuration;
            return true;
        }
        return false;
    }

    AZ::u32 EditorTerrainCompositionHeightProviderComponent::OnConfigurationChanged()
    {
        if (m_preview)
        {
            m_preview->ReadInConfig(&m_configuration);
            m_status = m_preview->GetStatusMessage();
        }
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }

    void EditorTerrainCompositionHeightProviderComponent::OnTick(
        float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        PollEditorPreviewStatus(*this, m_preview.get(), deltaTime, m_statusElapsed, m_status);
    }
} // namespace TerrainCompositor
