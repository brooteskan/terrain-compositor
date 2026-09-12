#pragma once

#include <TerrainCompositor/Internal/PreparedComposition.h>

namespace TerrainCompositor::Internal
{
    // Conservative capacity accounting: repeated shared values are counted again.
    // The dispatcher rejects a generation above its allowance before acquisition.
    template<class T> size_t CapacityBytes(const T& values) { return values.capacity() * sizeof(typename T::value_type); }
    inline size_t RetainedBytes(const HeightmapDataPtr& data)
    {
        return data ? sizeof(*data) + CapacityBytes(data->m_samples) + CapacityBytes(data->m_rawSamples) +
            CapacityBytes(data->m_uniqueNonzeroValues) + 128 : 0;
    }
    inline size_t RetainedBytes(const TerrainMeshHeightDataPtr& data)
    {
        return data ? sizeof(*data) + CapacityBytes(data->m_localHeights) + CapacityBytes(data->m_uncoveredCellBits) +
            CapacityBytes(data->m_cellDiagonalBits) + CapacityBytes(data->m_gapTileOccupancy) + 128 : 0;
    }
    inline size_t RetainedBytes(const TerrainMeshCutoutDataPtr& data)
    {
        return data ? sizeof(*data) + CapacityBytes(data->m_vertices) + CapacityBytes(data->m_indices) +
            CapacityBytes(data->m_triangles) + CapacityBytes(data->m_bvh) + 128 : 0;
    }
    inline size_t RetainedBytes(const PreparedTerrainMeshHeightGap& gap)
    {
        return RetainedBytes(gap.m_data) + gap.m_stableOrderKey.capacity() + (gap.m_collisionCells ?
            sizeof(*gap.m_collisionCells) + CapacityBytes(gap.m_collisionCells->m_cells) + 128 : 0);
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
            if (item.m_image.m_reconstruction)
                bytes += sizeof(*item.m_image.m_reconstruction) + CapacityBytes(item.m_image.m_reconstruction->m_samples) + 128;
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
