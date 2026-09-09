#pragma once

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Math/Uuid.h>
#include <AzCore/std/string/string.h>
#include <Atom/RPI.Reflect/Image/StreamingImageAsset.h>
#include <TerrainCompositor/SurfaceCompositionConfig.h>
#include <TerrainCompositor/TerrainExistenceConfig.h>
#include <TerrainCompositor/TerrainCompositorTypeIds.h>

namespace TerrainCompositor
{
    enum class HeightmapSamplingMode : AZ::u8
    {
        Bilinear = 0,
        SmoothCubic
    };

    //! Serialized authoring contract. Placement comes from the stamp entity's world transform.
    //! The registration helper loads shared image data; editor keys are resolved from saved prefab aliases.
    class HeightmapStampConfig final : public AZ::ComponentConfig
    {
    public:
        AZ_CLASS_ALLOCATOR(HeightmapStampConfig, AZ::SystemAllocator);
        AZ_RTTI(HeightmapStampConfig, HeightmapStampConfigTypeId, AZ::ComponentConfig);

        static void Reflect(AZ::ReflectContext* context);
        float GetMaximumFeatherWidth() const;
        float GetMaximumEdgeInset() const;
        //! Call once when creating/duplicating a programmatic stamp, BEFORE registration, and persist this config.
        void AssignNewPersistentOrderingIdentity();
        //! Legacy UUIDs are read deterministically. A malformed explicit key never falls back to its legacy UUID.
        AZStd::string GetRuntimeOrderKey() const;

        AZ::EntityId m_targetCompositionEntityId;
        // Store only the reference; HeightmapDataCache owns asynchronous image/mip load requests.
        AZ::Data::Asset<AZ::RPI::StreamingImageAsset> m_heightmapAsset{ AZ::Data::AssetLoadBehavior::NoLoad };
        SurfaceMapSetConfig m_surfaceMaps;
        TerrainHoleMaskConfig m_holeMask;
        float m_footprintWidth = 100.0f;
        float m_footprintDepth = 100.0f;
        float m_heightScale = 100.0f;
        float m_verticalOffset = 0.0f;
        HeightmapSamplingMode m_samplingMode = HeightmapSamplingMode::Bilinear;
        float m_reconstructionRadius = 0.0f;
        float m_strength = 1.0f;
        float m_featherWidth = 10.0f;
        // These two defaults preserve the original feather mask.
        float m_featherExponent = 1.0f;
        float m_edgeInset = 0.0f;
        // Default on for the tapered relative-edge behavior; false restores pure Replace.
        bool m_relativeEdgeBlend = true;
        AZ::s32 m_priority = 0;

        //! Legacy serialized field: retain old data, but never synthesize identities during deserialization/activation.
        AZ::Uuid m_orderingId = AZ::Uuid::CreateNull();
        //! Exact baked key. Editor resolution operates on registration/export copies, never the saved template.
        AZStd::string m_stableOrderKey;
    };
} // namespace TerrainCompositor

namespace AZ
{
    AZ_TYPE_INFO_SPECIALIZE(TerrainCompositor::HeightmapSamplingMode, "{1FDF365F-61DC-41E8-96C9-B06A89F51019}");
}
