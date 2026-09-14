#include <AzTest/AzTest.h>
#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>

namespace TerrainCompositor
{
    // This target intentionally does not link TerrainCompositor.Static. Calling
    // the engine-facing helper must resolve under Terrain.dll's dependencies.
    TEST(TerrainEngineBoundaryTests, CollisionAdmissionLinksWithoutTheCompositorLibrary)
    {
        TerrainMeshCutoutRenderSnapshot snapshot;
        snapshot.m_revision = 11;
        auto cells = std::make_shared<PreparedTerrainHeightfieldCellMask>();
        cells->m_gridSpacing = 1.0f;
        cells->m_cells.push_back({ 1, 0 });
        PreparedTerrainMeshHeightGap gap;
        gap.m_entityId = AZ::EntityId(100);
        gap.m_collisionCells = cells;
        snapshot.m_meshHeightGaps.push_back(gap);
        auto activation = std::make_shared<TerrainMeshHeightGapActivation>();
        activation->m_revision = snapshot.m_revision;
        activation->m_gaps.push_back(gap);
        const AZ::Vector2 cell(1.0f, 0.0f), spacing(1.0f);

        EXPECT_TRUE(IsTerrainHeightfieldCellRemoved(snapshot, activation, cell, spacing));
        activation->m_gaps[0].m_entityId = AZ::EntityId(200);
        EXPECT_FALSE(IsTerrainHeightfieldCellRemoved(snapshot, activation, cell, spacing));
        activation->m_gaps[0] = gap;
        --activation->m_revision;
        EXPECT_FALSE(IsTerrainHeightfieldCellRemoved(snapshot, activation, cell, spacing));
        snapshot.m_meshHeightGaps[0].m_affectTerrainRendering = false;
        EXPECT_TRUE(IsTerrainHeightfieldCellRemoved(snapshot, {}, cell, spacing));
    }
}
