#pragma once

#include <Atom/RHI.Reflect/ShaderResourceGroupLayout.h>
#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <Atom/RPI.Public/FeatureProcessor.h>
#include <Atom/RPI.Public/Material/Material.h>
#include <AzCore/Math/Vector4.h>
#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>
#include <TerrainCompositor/TerrainMeshHeightGapGpu.h>

namespace TerrainCompositor
{
    struct TerrainMeshCutoutRenderStatistics
    {
        AZ::u64 m_snapshotRevision = 0;
        AZ::u64 m_uploadCount = 0;
        AZ::u32 m_activeCutoutCount = 0;
        AZ::u32 m_vertexCount = 0;
        AZ::u32 m_triangleCount = 0;
        AZ::u32 m_resourceLimitRejectCount = 0;
        AZ::u32 m_activeGapInstances = 0;
        AZ::u32 m_uniqueGapMasks = 0;
        AZ::u64 m_gapMaskBytes = 0;
        AZ::u64 m_gapTileBytes = 0;
        AZ::u64 m_gapMaskUploads = 0;
        AZ::u64 m_gapDescriptorUploads = 0;
        AZ::u64 m_gapPublicationRevision = 0;
        AZ::u32 m_gapRejectedContributions = 0;
        AZ::u64 m_gapResourceFailures = 0;
        AZ::u64 m_gapStaleResults = 0;
    };

    class TerrainMeshCutoutFeatureProcessor final : public AZ::RPI::FeatureProcessor
    {
    public:
        AZ_CLASS_ALLOCATOR(TerrainMeshCutoutFeatureProcessor, AZ::SystemAllocator);
        AZ_RTTI(TerrainMeshCutoutFeatureProcessor, "{D90A5F21-869F-4F4C-94CB-7DDB7CC4F75A}", AZ::RPI::FeatureProcessor);
        AZ_FEATURE_PROCESSOR(TerrainMeshCutoutFeatureProcessor);

        static void Reflect(AZ::ReflectContext* context);

        void Activate() override;
        void Deactivate() override;
        void OnBeginPrepareRender() override;
        void OnRenderEnd() override;
        const TerrainMeshCutoutRenderStatistics& GetStatistics() const
        {
            return m_statistics;
        }

    private:
        struct GpuCutout
        {
            AZ::Vector4 m_inverseRotation;
            AZ::Vector4 m_translationAndInverseScale;
            AZ::Vector4 m_localBoundsMinAndMargin;
            AZ::Vector4 m_localBoundsMax;
            AZ::u32 m_firstIndex = 0;
            AZ::u32 m_indexCount = 0;
            AZ::u32 m_operation = 0;
            AZ::u32 m_padding = 0;
        };

        void RebuildGpuData();
        AZ::Data::Instance<AZ::RPI::Buffer> CreateReadOnlyBuffer(
            const char* name, AZ::u32 elementSize, AZ::u32 elementCount, const void* data) const;

        AZ::RHI::ShaderInputNameIndex m_cutoutCountIndex{ "m_meshCutoutCount" };
        AZ::RHI::ShaderInputNameIndex m_cutoutsIndex{ "m_meshCutouts" };
        AZ::RHI::ShaderInputNameIndex m_verticesIndex{ "m_meshCutoutVertices" };
        AZ::RHI::ShaderInputNameIndex m_indicesIndex{ "m_meshCutoutIndices" };
        AZ::Data::Instance<AZ::RPI::Buffer> m_cutoutsBuffer;
        AZ::Data::Instance<AZ::RPI::Buffer> m_verticesBuffer;
        AZ::Data::Instance<AZ::RPI::Buffer> m_indicesBuffer;
        TerrainMeshCutoutRenderChannelPtr m_sceneChannel;
        TerrainMeshCutoutRenderStatistics m_statistics;
        TerrainMeshHeightGapGpuData m_gapData;
        TerrainMeshCutoutRenderSnapshotPtr m_gapPreparedSnapshot;
        TerrainMeshCutoutRenderSnapshotPtr m_gapBoundSnapshot;
        AZ::Data::Instance<AZ::RPI::Material> m_gapBoundMaterial;
        AZ::Data::Instance<AZ::RPI::ShaderResourceGroup> m_gapBoundSrg;
        AZ::Data::Instance<AZ::RPI::Buffer> m_gapDescriptorsBuffer;
        AZ::Data::Instance<AZ::RPI::Buffer> m_gapMaskBuffer;
        AZ::Data::Instance<AZ::RPI::Buffer> m_emptyGapDescriptors;
        AZ::Data::Instance<AZ::RPI::Buffer> m_emptyGapWords;
        bool m_gapBuffersReady = false;
        bool m_gapAwaitingActivation = false;
        bool m_reportedGapBindingFailure = false;
    };
} // namespace TerrainCompositor
