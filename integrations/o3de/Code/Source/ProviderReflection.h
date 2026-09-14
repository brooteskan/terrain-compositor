#pragma once

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace TerrainCompositor::Internal
{
    template<class Configuration>
    void ReflectProviderConfiguration(AZ::ReflectContext* context, const char* name, const char* description)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection() && serialize->FindClassData(azrtti_typeid<Configuration>())) return;
            serialize->Class<Configuration, AZ::ComponentConfig>()->Version(1)
                ->Field("CompositionEntityId", &Configuration::m_compositionEntityId);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<Configuration>(name, "Composition reference for this terrain region.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &Configuration::m_compositionEntityId,
                        "Terrain Composition", description)
                    ->Attribute(AZ::Edit::Attributes::RequiredService, AZ_CRC_CE("TerrainCompositionService"));
            }
        }
    }

    template<class Component, class Configuration>
    void ReflectConfiguredComponent(AZ::ReflectContext* context, Configuration Component::* configuration)
    {
        Configuration::Reflect(context);
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
            serialize->Class<Component, AZ::Component>()->Version(1)->Field("Configuration", configuration);
    }
}
