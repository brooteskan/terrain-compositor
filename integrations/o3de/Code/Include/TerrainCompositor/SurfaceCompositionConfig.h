#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Memory/SystemAllocator.h>
#include <Atom/RPI.Reflect/Image/StreamingImageAsset.h>
#include <SurfaceData/SurfaceTag.h>

namespace TerrainCompositor
{
    //! Optional categorical surface channels authored with a heightmap stamp.
    //! ID A is required for any surface contribution. ID B and Blend are an all-or-nothing pair.
    class SurfaceMapSetConfig final
    {
    public:
        AZ_CLASS_ALLOCATOR(SurfaceMapSetConfig, AZ::SystemAllocator);
        AZ_TYPE_INFO(SurfaceMapSetConfig, "{EF87C951-77AE-423D-B24D-9C52730A7DF7}");

        static void Reflect(AZ::ReflectContext* context);

        AZ::Data::Asset<AZ::RPI::StreamingImageAsset> m_surfaceIdAAsset{ AZ::Data::AssetLoadBehavior::NoLoad };
        AZ::Data::Asset<AZ::RPI::StreamingImageAsset> m_surfaceIdBAsset{ AZ::Data::AssetLoadBehavior::NoLoad };
        AZ::Data::Asset<AZ::RPI::StreamingImageAsset> m_blendMaskAsset{ AZ::Data::AssetLoadBehavior::NoLoad };
    };

    //! Stable authoring label to O3DE surface-tag mapping. ID zero is reserved for transparency.
    class SurfacePaletteEntry final
    {
    public:
        AZ_CLASS_ALLOCATOR(SurfacePaletteEntry, AZ::SystemAllocator);
        AZ_TYPE_INFO(SurfacePaletteEntry, "{B06A0E88-92D2-4B5E-A9A5-DB57B0C91420}");

        static void Reflect(AZ::ReflectContext* context);

        AZ::u16 m_exportedId = 0;
        SurfaceData::SurfaceTag m_surfaceTag;
    };

    //! Explicit base distribution entry. Positive entries are merged by tag and normalized at publication.
    class SurfaceBaseWeight final
    {
    public:
        AZ_CLASS_ALLOCATOR(SurfaceBaseWeight, AZ::SystemAllocator);
        AZ_TYPE_INFO(SurfaceBaseWeight, "{ED1EC9C5-66D7-44A2-B674-4C1694907BBD}");

        static void Reflect(AZ::ReflectContext* context);

        AZ::u16 m_exportedId = 0;
        float m_weight = 1.0f;
    };
} // namespace TerrainCompositor
