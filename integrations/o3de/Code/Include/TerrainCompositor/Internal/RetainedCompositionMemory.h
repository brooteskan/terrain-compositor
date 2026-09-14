#pragma once

#include <TerrainCompositor/Internal/PreparedComposition.h>

namespace TerrainCompositor::Internal
{
    // Conservative capacity accounting: repeated shared values are counted again.
    // The dispatcher rejects a generation above its allowance before acquisition.
    template<class T> size_t CapacityBytes(const T& values) { return values.capacity() * sizeof(typename T::value_type); }
    template<class Data, class... Members>
    size_t RetainedAllocationBytes(const Data& data, Members... members)
    {
        return data ? sizeof(*data) + (CapacityBytes(data.get()->*members) + ... + size_t{ 0 }) + 128 : 0;
    }

    inline size_t RetainedBytes(const HeightmapDataPtr& data)
    {
        return RetainedAllocationBytes(data,
            &HeightmapData::m_samples, &HeightmapData::m_rawSamples, &HeightmapData::m_uniqueNonzeroValues);
    }
    inline size_t RetainedBytes(const TerrainMeshHeightDataPtr& data)
    {
        return RetainedAllocationBytes(data,
            &TerrainMeshHeightData::m_localHeights, &TerrainMeshHeightData::m_uncoveredCellBits,
            &TerrainMeshHeightData::m_cellDiagonalBits, &TerrainMeshHeightData::m_gapTileOccupancy);
    }
    inline size_t RetainedBytes(const TerrainMeshCutoutDataPtr& data)
    {
        return RetainedAllocationBytes(data,
            &TerrainMeshCutoutData::m_vertices, &TerrainMeshCutoutData::m_indices,
            &TerrainMeshCutoutData::m_triangles, &TerrainMeshCutoutData::m_bvh);
    }
    inline size_t RetainedBytes(const PreparedTerrainMeshHeightGap& gap)
    {
        return RetainedBytes(gap.m_data) + gap.m_stableOrderKey.capacity() +
            RetainedAllocationBytes(gap.m_collisionCells, &PreparedTerrainHeightfieldCellMask::m_cells);
    }
    inline size_t RetainedBytes(const PreparedTerrainMeshCutout& cutout)
    {
        return RetainedBytes(cutout.m_data) + cutout.m_stableOrderKey.capacity();
    }
    inline size_t RetainedBytes(const PreparedComposition& state)
    {
        // Allowance includes QueryState, immutable procedural kernel configuration,
        // callback copies/control blocks and small allocator metadata.
        size_t bytes = sizeof(state) + 16384 + CapacityBytes(state.m_heightContributors) +
            CapacityBytes(state.m_existenceContributors) + CapacityBytes(state.m_surfaceStamps) +
            CapacityBytes(state.m_meshHeightGaps) + CapacityBytes(state.m_surfacePalette.m_entries) +
            CapacityBytes(state.m_surfacePalette.m_baseWeights) + state.m_surfacePalette.m_error.capacity();
        for (const auto& item : state.m_heightContributors)
        {
            bytes += RetainedBytes(item.m_image.m_image) + item.m_image.m_placement.m_stableOrderKey.capacity() +
                RetainedBytes(item.m_mesh.m_data) + item.m_mesh.m_stableOrderKey.capacity();
            bytes += RetainedAllocationBytes(item.m_image.m_reconstruction, &HeightmapReconstructionData::m_samples);
        }
        for (const auto& item : state.m_existenceContributors)
            bytes += RetainedBytes(item.m_imageMask.m_mask) + item.m_imageMask.m_placement.m_stableOrderKey.capacity() +
                RetainedBytes(item.m_meshCutout) + RetainedBytes(item.m_meshHeightGap);
        for (const auto& item : state.m_surfaceStamps)
            bytes += item.m_placement.m_stableOrderKey.capacity() + RetainedBytes(item.m_surfaceIdA) +
                RetainedBytes(item.m_surfaceIdB) + RetainedBytes(item.m_surfaceBlend);
        for (const auto& gap : state.m_meshHeightGaps) bytes += RetainedBytes(gap);
        return bytes;
    }
}
