#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Memory/SystemAllocator.h>
#include <Atom/RPI.Reflect/Image/StreamingImageAsset.h>

namespace TerrainCompositor
{
    enum class TerrainExistenceOperation : AZ::u8
    {
        RemoveTerrain,
        RestoreTerrain
    };

    //! Optional explicit terrain-existence channel for one transformed stamp.
    //! Zero/below-threshold samples are transparent and leave lower layers unchanged.
    class TerrainHoleMaskConfig final
    {
    public:
        AZ_CLASS_ALLOCATOR(TerrainHoleMaskConfig, AZ::SystemAllocator);
        AZ_TYPE_INFO(TerrainHoleMaskConfig, "{2A18A017-CCEC-4555-8886-A49B29C3E7D4}");

        static void Reflect(AZ::ReflectContext* context);

        AZ::Data::Asset<AZ::RPI::StreamingImageAsset> m_maskAsset{ AZ::Data::AssetLoadBehavior::NoLoad };
        float m_threshold = 0.5f;
        TerrainExistenceOperation m_operation = TerrainExistenceOperation::RemoveTerrain;
    };
} // namespace TerrainCompositor

namespace AZ
{
    AZ_TYPE_INFO_SPECIALIZE(TerrainCompositor::TerrainExistenceOperation, "{71F6ADBF-EFA2-4266-813B-D94529E9A6B5}");
}
