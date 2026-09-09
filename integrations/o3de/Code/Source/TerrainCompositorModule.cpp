#include "TerrainCompositorModule.h"

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), TerrainCompositor::TerrainCompositorModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_TerrainCompositor, TerrainCompositor::TerrainCompositorModule)
#endif

