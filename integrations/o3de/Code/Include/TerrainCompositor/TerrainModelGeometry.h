#pragma once

#include <Atom/RPI.Reflect/Model/ModelAsset.h>
#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/vector.h>

namespace TerrainCompositor
{
    // A 1024 by 1024 triangulated grid contains about 6.28 million indices. Importers may also
    // split vertices at triangle/material/normal boundaries before the height-field weld pass.
    inline constexpr size_t TerrainModelGeometryMaximumVertices = 8'000'000;
    inline constexpr size_t TerrainModelGeometryMaximumIndices = 8'000'000;

    enum class TerrainModelGeometryValidation : AZ::u8
    {
        Valid,
        Empty,
        MissingPositionStream,
        UnsupportedPositionFormat,
        IndexCount,
        UnsupportedIndexFormat,
        BufferData,
        IndexOutOfRange,
        NonFinitePosition,
        ResourceLimit
    };

    //! Immutable-friendly LOD0 geometry extracted from every section of one Atom model product.
    //! Positions and bounds remain in model-local space; no centering or coordinate normalization occurs.
    struct TerrainModelGeometry
    {
        AZ::Data::AssetId m_assetId;
        AZ::u64 m_revision = 0;
        AZ::Aabb m_localBounds = AZ::Aabb::CreateNull();
        AZStd::vector<AZ::Vector3> m_positions;
        AZStd::vector<AZ::u32> m_indices;
    };

    TerrainModelGeometryValidation ExtractTerrainModelGeometry(
        AZ::RPI::ModelAsset& model, const AZ::Data::AssetId& assetId, TerrainModelGeometry& result);
    const char* GetTerrainModelGeometryValidationMessage(TerrainModelGeometryValidation validation);
} // namespace TerrainCompositor
