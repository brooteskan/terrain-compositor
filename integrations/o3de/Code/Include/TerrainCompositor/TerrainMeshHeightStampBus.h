#pragma once

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/std/string/string.h>

namespace TerrainCompositor
{
    //! Control-thread status used by editor automation and authoring tools.
    class TerrainMeshHeightStampRequests : public AZ::ComponentBus
    {
    public:
        virtual AZStd::string GetStatusMessage() const = 0;
    };
    using TerrainMeshHeightStampRequestBus = AZ::EBus<TerrainMeshHeightStampRequests>;
} // namespace TerrainCompositor
