#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/span.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>

namespace TerrainCompositor
{
    enum class TerrainMeshCutoutDataStatus : AZ::u8
    {
        Unassigned,
        Loading,
        Missing,
        Error,
        Unsupported,
        InvalidGeometry,
        Ready
    };

    enum class TerrainMeshCutoutValidation : AZ::u8
    {
        Valid,
        Empty,
        IndexCount,
        NonFinitePosition,
        IndexOutOfRange,
        DegenerateTriangle,
        OpenOrNonManifold,
        InconsistentWinding,
        ZeroVolume,
        SelfIntersection,
        ResourceLimit
    };

    struct TerrainMeshCutoutTriangle
    {
        AZ::Vector3 m_a;
        AZ::Vector3 m_b;
        AZ::Vector3 m_c;
        AZ::Aabb m_bounds = AZ::Aabb::CreateNull();
    };

    //! Preorder, stackless BVH. Internal nodes have count zero; escape skips the complete subtree.
    struct TerrainMeshCutoutBvhNode
    {
        AZ::Aabb m_bounds = AZ::Aabb::CreateNull();
        AZ::u32 m_firstTriangle = 0;
        AZ::u32 m_triangleCount = 0;
        AZ::u32 m_escapeIndex = 0;
    };

    struct TerrainMeshCutoutData
    {
        AZ::Data::AssetId m_assetId;
        AZ::u64 m_revision = 0;
        AZ::Aabb m_localBounds = AZ::Aabb::CreateNull();
        //! Canonical, welded, outward-wound geometry used for immutable GPU uploads.
        AZStd::vector<AZ::Vector3> m_vertices;
        AZStd::vector<AZ::u32> m_indices;
        AZStd::vector<TerrainMeshCutoutTriangle> m_triangles;
        AZStd::vector<TerrainMeshCutoutBvhNode> m_bvh;
    };
    using TerrainMeshCutoutDataPtr = AZStd::shared_ptr<const TerrainMeshCutoutData>;

    struct TerrainMeshCutoutDataSnapshot
    {
        TerrainMeshCutoutDataStatus m_status = TerrainMeshCutoutDataStatus::Unassigned;
        TerrainMeshCutoutValidation m_validation = TerrainMeshCutoutValidation::Valid;
        AZ::u64 m_revision = 0;
        TerrainMeshCutoutDataPtr m_data;
        AZ::Data::AssetId m_assetId;
    };

    TerrainMeshCutoutValidation BuildTerrainMeshCutoutData(
        AZStd::span<const AZ::Vector3> positions,
        AZStd::span<const AZ::u32> indices,
        TerrainMeshCutoutData& result);
    const char* GetTerrainMeshCutoutValidationMessage(TerrainMeshCutoutValidation validation);
} // namespace TerrainCompositor
