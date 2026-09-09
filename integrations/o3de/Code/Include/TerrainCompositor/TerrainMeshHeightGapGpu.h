#pragma once

#include <AzCore/Math/Vector4.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>

namespace TerrainCompositor
{
    // Keep this layout in sync with TerrainMaterialSrg::MeshHeightGap.
    struct alignas(16) TerrainMeshHeightGapGpuDescriptor
    {
        AZ::Vector4 m_worldBounds = AZ::Vector4::CreateZero();
        AZ::Vector4 m_originAndRotation = AZ::Vector4::CreateZero();
        AZ::Vector4 m_gridOriginAndInverseSpacing = AZ::Vector4::CreateZero();
        AZ::Vector4 m_inverseScaleAndDomainMax = AZ::Vector4::CreateZero();
        AZ::u32 m_cellWidth = 0;
        AZ::u32 m_cellHeight = 0;
        AZ::u32 m_maskOffset = 0;
        AZ::u32 m_tileOffset = 0;
        AZ::u32 m_tileWidth = 0;
        AZ::u32 m_revisionLow = 0;
        AZ::u32 m_revisionHigh = 0;
        AZ::u32 m_padding = 0;
    };
    static_assert(sizeof(TerrainMeshHeightGapGpuDescriptor) == 96);

    struct TerrainMeshHeightGapGpuLimits
    {
        AZ::u32 m_instances = 256;
        AZ::u32 m_uniqueMasks = 256;
        size_t m_maskBytes = 16 * 1024 * 1024;
    };

    struct TerrainMeshHeightGapGpuData
    {
        AZStd::vector<TerrainMeshHeightGapGpuDescriptor> m_descriptors;
        std::shared_ptr<const AZStd::vector<AZ::u32>> m_words;
        AZStd::vector<TerrainMeshHeightDataPtr> m_masks;
        AZStd::vector<AZ::u32> m_maskOffsets;
        AZStd::vector<AZ::u32> m_tileOffsets;
        AZStd::vector<PreparedTerrainMeshHeightGap> m_admitted;
        size_t m_maskBytes = 0;
        size_t m_tileBytes = 0;
        AZ::u32 m_rejected = 0;
        bool m_masksChanged = true;
    };

    TerrainMeshHeightGapGpuData PrepareTerrainMeshHeightGapGpuData(
        AZStd::span<const PreparedTerrainMeshHeightGap> gaps,
        const TerrainMeshHeightGapGpuData* previous = nullptr,
        TerrainMeshHeightGapGpuLimits limits = {});
    bool SampleTerrainMeshHeightGapGpuData(
        const AZ::Vector3& position, const TerrainMeshHeightGapGpuDescriptor& descriptor, AZStd::span<const AZ::u32> words);
} // namespace TerrainCompositor
