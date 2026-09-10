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
