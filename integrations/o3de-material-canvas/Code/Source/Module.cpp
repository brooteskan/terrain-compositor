#include "TerrainTintMaterialComponent.h"
#include <AzCore/Module/Module.h>

namespace TerrainCompositorCanvas
{
    class Module final : public AZ::Module
    {
    public:
        AZ_RTTI(Module, "{B3CBF2BE-CCFA-4A22-9326-8C8F6D551ED2}", AZ::Module);
        AZ_CLASS_ALLOCATOR(Module, AZ::SystemAllocator);
        Module() { m_descriptors.push_back(TerrainTintMaterialComponent::CreateDescriptor()); }
    };
}
#if defined(TERRAIN_CANVAS_EDITOR)
AZ_DECLARE_MODULE_CLASS(Gem_TerrainCompositorCanvas_Editor, TerrainCompositorCanvas::Module)
#else
AZ_DECLARE_MODULE_CLASS(Gem_TerrainCompositorCanvas, TerrainCompositorCanvas::Module)
#endif
