#pragma once

#include <TerrainCompositor/HeightmapStampIdentity.h>

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace TerrainCompositor::Internal
{
    template<class Component, class Base, class Configuration, class ChangeNotify, class StatusText>
    void ReflectEditorConfiguration(AZ::ReflectContext* context, Configuration Component::* configuration,
        ChangeNotify changed, StatusText status, const char* name, const char* description,
        const char* configurationDescription, const char* statusDescription)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            // Runtime owns shared config unreflection, even if the editor is reflected first.
            if (!serialize->IsRemovingReflection()) Configuration::Reflect(context);
            serialize->Class<Component, Base>()->Version(1)->Field("Configuration", configuration);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<Component>(name, description)
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Terrain")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, configuration, "Configuration", configurationDescription)
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, changed)
                    ->UIElement(AZ::Edit::UIHandlers::Label, "Status", statusDescription)
                    ->Attribute(AZ::Edit::Attributes::ValueText, status);
            }
        }
    }

    template<class Runtime, class Configuration>
    void BuildOrderedGameEntity(AZ::Entity* entity, const Configuration& configuration, const char* window, const char* diagnostic)
    {
        if (!IsValidStampOrderKey(configuration.m_stableOrderKey))
        {
            AZ_Error(window, false, "%s", diagnostic);
            return;
        }
        entity->CreateComponent<Runtime>(configuration);
    }

    template<class Preview, class Configuration>
    void SetEditorExportKey(const Preview& preview, Configuration& configuration, const AZStd::string& key, const char* diagnostic)
    {
        AZ_Assert(!preview, "%s", diagnostic);
        if (!preview) configuration.m_stableOrderKey = key;
    }

    template<class Preview, class Configuration>
    AZ::u32 RefreshEditorConfiguration(Preview& preview, const Configuration& configuration)
    {
        preview.Refresh(configuration);
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }
}
