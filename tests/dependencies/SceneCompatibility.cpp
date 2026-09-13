// Both consumers must see compatible generic APIs in the same translation unit.
#include <scene_polytree/scene_polytree.hpp>
#include <TerrainCompositor/TerrainSectorCoverage.h>

static_assert(std::is_move_constructible_v<TerrainCompositor::TerrainSectorCoverageScratch>);
