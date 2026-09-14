#pragma once

#include <AzCore/Component/Component.h>

namespace TerrainCompositor::Internal
{
    template<class Configuration, class Apply>
    bool ReadConfiguration(const AZ::ComponentConfig* input, Apply&& apply)
    {
        if (const auto* value = azrtti_cast<const Configuration*>(input))
        {
            apply(*value);
            return true;
        }
        return false;
    }

    template<class Configuration, class Changed>
    void AssignConfiguration(Configuration& configuration, const Configuration& value, Changed changed)
    {
        configuration = value;
        changed();
    }

    template<class Configuration, class Changed>
    bool ReadConfiguration(const AZ::ComponentConfig* input, Configuration& configuration, Changed changed)
    {
        return ReadConfiguration<Configuration>(input, [&](const Configuration& value)
        {
            AssignConfiguration(configuration, value, changed);
        });
    }

    template<class Configuration>
    bool WriteConfiguration(AZ::ComponentConfig* output, const Configuration& value)
    {
        if (auto* destination = azrtti_cast<Configuration*>(output))
        {
            *destination = value;
            return true;
        }
        return false;
    }
}
