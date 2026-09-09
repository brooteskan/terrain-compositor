#pragma once

#include <AzCore/std/containers/span.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>
#include <TerrainCompositor/StampPlacement.h>
#include <TerrainCompositor/TerrainMeshHeightStampSampling.h>

namespace TerrainCompositor
{
    struct HeightmapReconstructionData
    {
        AZ::u32 m_width = 0;
        AZ::u32 m_height = 0;
        AZStd::vector<float> m_samples;
    };
    using HeightmapReconstructionDataPtr = AZStd::shared_ptr<const HeightmapReconstructionData>;

    //! Value-only query record. Construct on the main thread, then publish through const shared ownership.
    //! Bounds describe XY at Z=0; neither queries nor dirty-region code use stamp-origin Z for coverage.
    struct PreparedHeightmapStamp
    {
        PreparedStampPlacement m_placement;
        HeightmapDataPtr m_image;
        HeightmapReconstructionDataPtr m_reconstruction;
        AZ::u64 m_imageRevision = 0;
        double m_heightOrigin = 0.0;
        double m_heightRange = 0.0;
        double m_strength = 0.0;
        double m_feather = 0.0;
        double m_featherExponent = 1.0;
        bool m_relativeEdgeBlend = true;
    };

    struct HeightmapRegionMapping
    {
        double m_minZ = 0.0;
        double m_range = 0.0; //!< Nonpositive means unavailable/invalid.
    };

    //! Image and mesh height records share one sorted stream so priority and stable-key ordering is global.
    struct PreparedHeightContributor
    {
        enum class Type : AZ::u8
        {
            Image,
            Mesh
        };

        AZ::s32 GetPriority() const
        {
            return m_type == Type::Image ? m_image.m_placement.m_priority : m_mesh.m_priority;
        }
        const AZStd::string& GetStableOrderKey() const
        {
            return m_type == Type::Image ? m_image.m_placement.m_stableOrderKey : m_mesh.m_stableOrderKey;
        }
        AZ::EntityId GetEntityId() const
        {
            return m_type == Type::Image ? m_image.m_placement.m_stampEntityId : m_mesh.m_stampEntityId;
        }
        const AZ::Aabb& GetWorldBounds() const
        {
            return m_type == Type::Image ? m_image.m_placement.m_worldBounds : m_mesh.m_worldBounds;
        }

        Type m_type = Type::Image;
        PreparedHeightmapStamp m_image;
        PreparedTerrainMeshHeightStamp m_mesh;
    };
    HeightmapRegionMapping PrepareHeightmapRegionMapping(const AZ::Aabb& bounds);

    //! Pure preparation: no buses, asset acquisition, logging, or mutation of image data.
    //! The caller resolves the separate NonUniformScaleService on the main thread.
    HeightmapStampValidation PrepareHeightmapStamp(
        const HeightmapStampRegistrationData& registration, bool hasNonUniformScale, PreparedHeightmapStamp& result);
    const char* GetHeightmapStampValidationMessage(HeightmapStampValidation validation);

    //! Control-plane preprocessing for Smooth Cubic. The returned samples are immutable and safe for query snapshots.
    //! Radius zero returns null because the unmodified source is already available on PreparedHeightmapStamp.
    HeightmapReconstructionDataPtr CreateHeightmapReconstruction(const HeightmapData& image, float radius);

    //! Shared scalar/batch implementation. Records must come from successful preparation and be sorted by priority/key.
    //! Returns baseValue directly if no nonzero weight contributes, including absent/degenerate region mappings.
    float ComposeHeightmapStamps(
        const AZ::Vector3& position, float baseValue, const AZ::Aabb& regionBounds, AZStd::span<const PreparedHeightmapStamp> stamps);
    float ComposeHeightmapStamps(
        const AZ::Vector3& position,
        float baseValue,
        const HeightmapRegionMapping& mapping,
        AZStd::span<const PreparedHeightmapStamp> stamps);
    float ComposeHeightContributors(
        const AZ::Vector3& position,
        float baseValue,
        const HeightmapRegionMapping& mapping,
        AZStd::span<const PreparedHeightContributor> contributors);
} // namespace TerrainCompositor
