#pragma once

#include <AzCore/Math/Crc.h>
#include <AzCore/std/containers/span.h>
#include <AzCore/std/containers/vector.h>
#include <AzFramework/SurfaceData/SurfaceData.h>
#include <TerrainCompositor/HeightmapStampSampling.h>
#include <TerrainCompositor/SurfaceCompositionConfig.h>

namespace TerrainCompositor
{
    struct PreparedSurfacePaletteEntry
    {
        AZ::u16 m_exportedId = 0;
        AZ::Crc32 m_surfaceTag;
    };

    struct PreparedSurfacePalette
    {
        AZStd::vector<PreparedSurfacePaletteEntry> m_entries; //!< Sorted by exported ID.
        AZStd::vector<AzFramework::SurfaceData::SurfaceTagWeight> m_baseWeights; //!< Merged and normalized.
        AZStd::string m_error;

        bool IsValid() const { return m_error.empty(); }
    };

    enum class SurfaceStampValidation
    {
        Valid,
        Absent,
        Placement,
        Palette,
        IncompletePair,
        DataUnavailable,
        ImageData,
        DimensionMismatch,
        UnknownId
    };

    //! Value-only surface query record. Placement and ordering match PreparedHeightmapStamp exactly.
    struct PreparedSurfaceStamp
    {
        PreparedStampPlacement m_placement;
        HeightmapDataPtr m_surfaceIdA;
        HeightmapDataPtr m_surfaceIdB;
        HeightmapDataPtr m_surfaceBlend;
        double m_strength = 0.0;
        double m_feather = 0.0;
        double m_featherExponent = 1.0;
    };

    PreparedSurfacePalette PrepareSurfacePalette(
        AZStd::span<const SurfacePaletteEntry> entries, AZStd::span<const SurfaceBaseWeight> baseWeights);
    bool SurfacePalettesEqual(const PreparedSurfacePalette& left, const PreparedSurfacePalette& right);

    SurfaceStampValidation PrepareSurfaceStamp(const HeightmapStampRegistrationData& registration,
        bool hasNonUniformScale, const PreparedSurfacePalette& palette, PreparedSurfaceStamp& result,
        HeightmapStampValidation* placementValidation = nullptr);
    const char* GetSurfaceStampValidationMessage(SurfaceStampValidation validation);

    //! Complete accumulated Replace composition followed by one deterministic provider-boundary top-16 reduction.
    void ComposeSurfaceStamps(const AZ::Vector3& position, const PreparedSurfacePalette& palette,
        AZStd::span<const PreparedSurfaceStamp> stamps,
        AzFramework::SurfaceData::SurfaceTagWeightList& outSurfaceWeights);
} // namespace TerrainCompositor
