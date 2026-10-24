#pragma once

#include <TerrainCompositor/TerrainOverlapConfig.h>
#include <TerrainCompositor/HeightmapStampSampling.h>

namespace TerrainCompositor
{
    inline constexpr AZ::u32 TerrainOverlapMaximumSources = 4;
    inline constexpr AZ::u32 TerrainOverlapMaximumRegions = 64;
    inline constexpr size_t TerrainOverlapMaximumWords = 16 * 1024 * 1024;
    // Versioned extension at the end of the existing surface word buffer. The legacy prefix is unchanged.
    inline constexpr AZ::u32 TerrainOverlapMagic = 0x54434f31;
    inline constexpr AZ::u32 TerrainOverlapRegionWords = 8;
    inline constexpr AZ::u32 TerrainOverlapSourceWords = 24;

    struct PreparedTerrainOverlap
    {
        TerrainOverlapConfig m_config;
        bool m_heightFp16 = false;
        AZStd::string m_diagnostic;
        bool IsActive() const { return m_config.m_enabled && m_config.m_width > 0 && m_diagnostic.empty(); }
    };
    PreparedTerrainOverlap PrepareTerrainOverlap(const TerrainOverlapConfig& config,
        AZStd::span<const PreparedHeightContributor> contributors);

    //! Append a region descriptor and at most four source records. No GPU work, assets or terrain queries.
    //! On failure rolls back the region's data, preserving the complete legacy surface publication.
    bool AppendTerrainOverlapRegion(AZStd::vector<AZ::u32>& words, size_t header,
        const AZ::Aabb& bounds, const PreparedTerrainOverlap& overlap,
        AZStd::span<const PreparedHeightContributor> contributors, size_t maximumWords = TerrainOverlapMaximumWords);
}
