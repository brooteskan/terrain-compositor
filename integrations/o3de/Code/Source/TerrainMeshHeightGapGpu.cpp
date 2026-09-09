#include <AzCore/std/algorithm.h>
#include <TerrainCompositor/TerrainMeshHeightGapGpu.h>
#include <TerrainCompositor/TerrainMeshHeightMapping.h>
#include <cmath>
#include <limits>

namespace TerrainCompositor
{
    namespace
    {
        bool ValidMask(const TerrainMeshHeightData& data)
        {
            const size_t cells = GetTerrainMeshHeightCellCount(data);
            return cells && cells <= TerrainMeshHeightMaximumGridSamples && data.m_uncoveredCellCount &&
                data.m_uncoveredCellBits.size() == (cells + 31) / 32 && data.m_gapTileWidth == (data.m_width - 1 + 7) / 8 &&
                data.m_gapTileHeight == (data.m_height - 1 + 7) / 8 &&
                data.m_gapTileOccupancy.size() == size_t(data.m_gapTileWidth) * data.m_gapTileHeight && data.m_localBounds.IsValid() &&
                data.m_localBounds.GetMax().IsFinite() && data.m_localOrigin.IsFinite() && data.m_gridSpacing.IsFinite() &&
                AZStd::all_of(
                       data.m_gapTileOccupancy.begin(),
                       data.m_gapTileOccupancy.end(),
                       [](auto tile)
                       {
                           return tile <= TerrainMeshHeightGapTileOccupancy::Uncovered;
                       }) &&
                data.m_gridSpacing.GetX() > 0.0f && data.m_gridSpacing.GetY() > 0.0f;
        }

        bool MakeDescriptor(const PreparedTerrainMeshHeightGap& gap, TerrainMeshHeightGapGpuDescriptor& gpu)
        {
            if (!gap.m_data || !ValidMask(*gap.m_data) || !gap.m_worldBounds.IsValid() || !gap.m_worldBounds.GetMin().IsFinite() ||
                !gap.m_worldBounds.GetMax().IsFinite())
                return false;
            const auto& data = *gap.m_data;
            gpu.m_worldBounds = AZ::Vector4(
                gap.m_worldBounds.GetMin().GetX(),
                gap.m_worldBounds.GetMin().GetY(),
                gap.m_worldBounds.GetMax().GetX(),
                gap.m_worldBounds.GetMax().GetY());
            gpu.m_originAndRotation = AZ::Vector4(float(gap.m_originX), float(gap.m_originY), float(gap.m_cosYaw), float(gap.m_sinYaw));
            gpu.m_gridOriginAndInverseSpacing = AZ::Vector4(
                data.m_localOrigin.GetX(), data.m_localOrigin.GetY(), 1.0f / data.m_gridSpacing.GetX(), 1.0f / data.m_gridSpacing.GetY());
            gpu.m_inverseScaleAndDomainMax =
                AZ::Vector4(float(gap.m_inverseScale), data.m_localBounds.GetMax().GetX(), data.m_localBounds.GetMax().GetY(), 0.0f);
            gpu.m_cellWidth = data.m_width - 1;
            gpu.m_cellHeight = data.m_height - 1;
            gpu.m_tileWidth = data.m_gapTileWidth;
            gpu.m_revisionLow = AZ::u32(data.m_revision);
            gpu.m_revisionHigh = AZ::u32(data.m_revision >> 32);
            return gpu.m_originAndRotation.IsFinite() && gpu.m_gridOriginAndInverseSpacing.IsFinite() &&
                std::isnormal(gpu.m_inverseScaleAndDomainMax.GetX()) && gpu.m_inverseScaleAndDomainMax.GetX() > 0.0f &&
                std::isnormal(gpu.m_gridOriginAndInverseSpacing.GetZ()) && std::isnormal(gpu.m_gridOriginAndInverseSpacing.GetW());
        }
    } // namespace

    TerrainMeshHeightGapGpuData PrepareTerrainMeshHeightGapGpuData(
        AZStd::span<const PreparedTerrainMeshHeightGap> gaps,
        const TerrainMeshHeightGapGpuData* previous,
        TerrainMeshHeightGapGpuLimits limits)
    {
        TerrainMeshHeightGapGpuData result;
        for (const auto& gap : gaps)
        {
            if (!gap.m_affectTerrainRendering)
                continue;
            TerrainMeshHeightGapGpuDescriptor descriptor;
            if (!MakeDescriptor(gap, descriptor) || result.m_descriptors.size() >= limits.m_instances)
            {
                ++result.m_rejected;
                continue;
            }
            const bool unique = AZStd::find(result.m_masks.begin(), result.m_masks.end(), gap.m_data) == result.m_masks.end();
            const size_t maskBytes = gap.m_data->m_uncoveredCellBits.size() * sizeof(AZ::u32);
            const size_t tileBytes = ((gap.m_data->m_gapTileOccupancy.size() + 15) / 16) * sizeof(AZ::u32);
            if (unique &&
                (result.m_masks.size() >= limits.m_uniqueMasks ||
                 maskBytes + tileBytes > limits.m_maskBytes - AZStd::min(limits.m_maskBytes, result.m_maskBytes + result.m_tileBytes)))
            {
                ++result.m_rejected;
                continue;
            }
            if (unique)
            {
                result.m_masks.push_back(gap.m_data);
                result.m_maskBytes += maskBytes;
                result.m_tileBytes += tileBytes;
            }
            result.m_descriptors.push_back(descriptor);
            result.m_admitted.push_back(gap);
        }

        // Preserve the old table order even when instance ordering changes. Moving,
        // duplicating or changing priority never repacks an unchanged asset set.
        if (previous && previous->m_masks.size() == result.m_masks.size() &&
            AZStd::all_of(
                result.m_masks.begin(),
                result.m_masks.end(),
                [&](const auto& data)
                {
                    return AZStd::find(previous->m_masks.begin(), previous->m_masks.end(), data) != previous->m_masks.end();
                }))
        {
            result.m_masks = previous->m_masks;
            result.m_maskOffsets = previous->m_maskOffsets;
            result.m_tileOffsets = previous->m_tileOffsets;
            result.m_words = previous->m_words;
            result.m_masksChanged = false;
        }
        else
        {
            auto words = std::make_shared<AZStd::vector<AZ::u32>>();
            words->reserve((result.m_maskBytes + result.m_tileBytes) / sizeof(AZ::u32));
            for (const auto& data : result.m_masks)
            {
                result.m_maskOffsets.push_back(aznumeric_cast<AZ::u32>(words->size()));
                words->insert(words->end(), data->m_uncoveredCellBits.begin(), data->m_uncoveredCellBits.end());
                // Tail bits may never remove cells in the next asset's slice.
                const size_t tail = GetTerrainMeshHeightCellCount(*data) % 32;
                if (tail)
                    words->back() &= (AZ::u32(1) << tail) - 1;
                result.m_tileOffsets.push_back(aznumeric_cast<AZ::u32>(words->size()));
                const size_t start = words->size();
                words->resize(start + (data->m_gapTileOccupancy.size() + 15) / 16, 0);
                for (size_t tile = 0; tile < data->m_gapTileOccupancy.size(); ++tile)
                {
                    (*words)[start + tile / 16] |= AZ::u32(data->m_gapTileOccupancy[tile]) << ((tile % 16) * 2);
                }
            }
            result.m_words = AZStd::move(words);
        }
        for (size_t index = 0; index < result.m_admitted.size(); ++index)
        {
            const size_t mask =
                AZStd::find(result.m_masks.begin(), result.m_masks.end(), result.m_admitted[index].m_data) - result.m_masks.begin();
            result.m_descriptors[index].m_maskOffset = result.m_maskOffsets[mask];
            result.m_descriptors[index].m_tileOffset = result.m_tileOffsets[mask];
        }
        return result;
    }

    bool SampleTerrainMeshHeightGapGpuData(
        const AZ::Vector3& position, const TerrainMeshHeightGapGpuDescriptor& gpu, AZStd::span<const AZ::u32> words)
    {
        if (!position.IsFinite() || !gpu.m_cellWidth || !gpu.m_cellHeight || position.GetX() < gpu.m_worldBounds.GetX() ||
            position.GetY() < gpu.m_worldBounds.GetY() || position.GetX() > gpu.m_worldBounds.GetZ() ||
            position.GetY() > gpu.m_worldBounds.GetW())
            return false;
        float x, y;
        MapTerrainMeshHeightXY(
            position.GetX(),
            position.GetY(),
            gpu.m_originAndRotation.GetX(),
            gpu.m_originAndRotation.GetY(),
            gpu.m_originAndRotation.GetZ(),
            gpu.m_originAndRotation.GetW(),
            gpu.m_inverseScaleAndDomainMax.GetX(),
            x,
            y);
        if (!(x >= gpu.m_gridOriginAndInverseSpacing.GetX() && y >= gpu.m_gridOriginAndInverseSpacing.GetY() &&
              x <= gpu.m_inverseScaleAndDomainMax.GetY() && y <= gpu.m_inverseScaleAndDomainMax.GetZ()))
            return false;
        x = TerrainMeshHeightGridCoordinate(x, gpu.m_gridOriginAndInverseSpacing.GetX(), gpu.m_gridOriginAndInverseSpacing.GetZ());
        y = TerrainMeshHeightGridCoordinate(y, gpu.m_gridOriginAndInverseSpacing.GetY(), gpu.m_gridOriginAndInverseSpacing.GetW());
        if (!std::isfinite(x) || !std::isfinite(y))
            return false;
        const AZ::u32 cellX = AZ::u32(AZStd::clamp(x, 0.0f, float(gpu.m_cellWidth - 1)));
        const AZ::u32 cellY = AZ::u32(AZStd::clamp(y, 0.0f, float(gpu.m_cellHeight - 1)));
        const size_t tile = size_t(cellY / 8) * gpu.m_tileWidth + cellX / 8;
        const size_t tileWord = gpu.m_tileOffset + tile / 16;
        if (tileWord >= words.size())
            return false;
        const AZ::u32 occupancy = (words[tileWord] >> ((tile % 16) * 2)) & 3;
        if (!occupancy)
            return false;
        if (occupancy == 2)
            return true;
        const size_t cell = size_t(cellY) * gpu.m_cellWidth + cellX;
        const size_t word = gpu.m_maskOffset + cell / 32;
        return word < words.size() && (words[word] & (AZ::u32(1) << (cell % 32)));
    }
} // namespace TerrainCompositor
