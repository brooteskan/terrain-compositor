#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>

namespace TerrainCompositor
{
    //! CPU-only, mip-zero samples in image row order. Published exclusively through shared_ptr<const>.
    //! Retain the shared pointer for the entire query; no asset access, decoding, or logging is needed to read it.
    //! Placement, height scale, strength, and blend order deliberately do not belong to shared image data.
    struct HeightmapData
    {
        AZ::Data::AssetId m_assetId;
        AZ::u64 m_revision = 0;
        AZ::u32 m_width = 0;
        AZ::u32 m_height = 0;
        AZStd::vector<float> m_samples; //!< Row-major, unsigned source value / 65535. No range normalization or Y flip.
        AZStd::vector<AZ::u16> m_rawSamples; //!< Exact row-major R16 values for categorical surface IDs and blend decoding.
        AZStd::vector<AZ::u16> m_uniqueNonzeroValues; //!< Sorted unique values, built once during decoding.
    };
    using HeightmapDataPtr = AZStd::shared_ptr<const HeightmapData>;

    enum class HeightmapDataStatus
    {
        Unassigned,
        Loading,
        Ready,
        Missing,
        Error,
        Unsupported
    };

    struct HeightmapDataSnapshot
    {
        HeightmapDataStatus m_status = HeightmapDataStatus::Unassigned;
        AZ::u64 m_revision = 0; //!< Changes on readiness transitions as well as replacement of image data.
        HeightmapDataPtr m_data; //!< Null unless Ready. A missing image is never a valid zero-height image.
        AZ::Data::AssetId m_assetId; //!< Canonical source even while Loading/Missing; groups shared-image updates.
    };
} // namespace TerrainCompositor
