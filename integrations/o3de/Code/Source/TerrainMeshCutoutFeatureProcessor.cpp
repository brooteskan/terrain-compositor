#include <TerrainCompositor/TerrainMeshCutoutFeatureProcessor.h>

#include <Atom/RPI.Public/Buffer/BufferSystemInterface.h>
#include <Atom/RPI.Public/Material/Material.h>
#include <Atom/RPI.Public/Scene.h>
#include <Atom/RPI.Public/Shader/ShaderResourceGroup.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/limits.h>
#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>

#include <TerrainRenderer/TerrainFeatureProcessor.h>
#include <cstring>

namespace TerrainCompositor
{
    namespace
    {
        constexpr AZ::u32 MaximumGpuCutouts = 256;
        constexpr AZ::u32 MaximumGpuTriangles = 262144;
    } // namespace

    void TerrainMeshCutoutFeatureProcessor::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<TerrainMeshCutoutFeatureProcessor, AZ::RPI::FeatureProcessor>()->Version(0);
        }
    }

    void TerrainMeshCutoutFeatureProcessor::Activate()
    {
        const TerrainMeshHeightGapGpuDescriptor empty{};
        const AZ::u32 zero = 0;
        m_emptyGapDescriptors = CreateReadOnlyBuffer("TerrainMeshGapEmptyDescriptors", sizeof(empty), 1, &empty);
        m_emptyGapWords = CreateReadOnlyBuffer("TerrainMeshGapEmptyWords", sizeof(zero), 1, &zero);
        if (auto* registry = AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get())
        {
            m_sceneChannel = registry->AcquireSceneChannel(GetParentScene());
        }
        EnableSceneNotification();
    }

    void TerrainMeshCutoutFeatureProcessor::Deactivate()
    {
        if (m_sceneChannel && m_observedTerrainMaterial)
            m_sceneChannel->m_materialChanged.Signal({});
        m_observedTerrainMaterial.reset();
        DisableSceneNotification();
        if (auto* registry = AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get())
            registry->RemoveScene(GetParentScene(), false);
        if (m_gapBoundMaterial)
        {
            const auto count = m_gapBoundMaterial->FindPropertyIndex(AZ::Name("settings.meshHeightGapCount"));
            if (count.IsValid())
                m_gapBoundMaterial->SetPropertyValue(count, AZ::u32(0));
            const auto srg = m_gapBoundMaterial->GetShaderResourceGroup();
            if (srg)
            {
                const auto gapCountIndex = srg->FindShaderInputConstantIndex(AZ::Name("m_meshHeightGapCount"));
                if (gapCountIndex.IsValid())
                    srg->SetConstant(gapCountIndex, AZ::u32(0));
                const auto descriptorsIndex = srg->FindShaderInputBufferIndex(AZ::Name("m_meshHeightGaps"));
                const auto wordsIndex = srg->FindShaderInputBufferIndex(AZ::Name("m_meshHeightGapWords"));
                if (descriptorsIndex.IsValid() && m_emptyGapDescriptors)
                    srg->SetBufferView(descriptorsIndex, m_emptyGapDescriptors->GetBufferView());
                if (wordsIndex.IsValid() && m_emptyGapWords)
                    srg->SetBufferView(wordsIndex, m_emptyGapWords->GetBufferView());
            }
            m_gapBoundMaterial->Compile();
        }
        m_gapBoundMaterial = {};
        m_gapBoundSrg = {};
        m_gapBoundSnapshot.reset();
        m_gapPreparedSnapshot.reset();
        m_gapDescriptorsBuffer = {};
        m_gapMaskBuffer = {};
        m_emptyGapDescriptors = {};
        m_emptyGapWords = {};
        m_gapData = {};
        m_gapBuffersReady = false;
        m_gapAwaitingActivation = false;
        m_reportedGapBindingFailure = false;
        m_cutoutsBuffer = {};
        m_verticesBuffer = {};
        m_indicesBuffer = {};
        m_sceneChannel.reset();
        m_statistics = {};
    }

    AZ::Data::Instance<AZ::RPI::Buffer> TerrainMeshCutoutFeatureProcessor::CreateReadOnlyBuffer(
        const char* name, AZ::u32 elementSize, AZ::u32 elementCount, const void* data) const
    {
        AZ::RPI::CommonBufferDescriptor descriptor;
        descriptor.m_poolType = AZ::RPI::CommonBufferPoolType::ReadOnly;
        descriptor.m_bufferName = name;
        descriptor.m_elementSize = elementSize;
        descriptor.m_elementFormat = AZ::RHI::Format::Unknown;
        descriptor.m_byteCount = AZ::u64(elementSize) * AZStd::max<AZ::u32>(elementCount, 1);
        descriptor.m_bufferData = data;
        auto* buffers = AZ::RPI::BufferSystemInterface::Get();
        return buffers ? buffers->CreateBufferFromCommonPool(descriptor) : AZ::Data::Instance<AZ::RPI::Buffer>{};
    }

    void TerrainMeshCutoutFeatureProcessor::RebuildGpuData()
    {
        const auto snapshot =
            m_sceneChannel ? m_sceneChannel->m_snapshot.load(std::memory_order_acquire) : TerrainMeshCutoutRenderSnapshotPtr{};
        if (!snapshot ||
            (snapshot->m_revision == m_statistics.m_snapshotRevision && m_cutoutsBuffer && m_verticesBuffer && m_indicesBuffer))
        {
            return;
        }

        AZStd::vector<GpuCutout> cutouts;
        AZStd::vector<AZ::Vector4> vertices;
        AZStd::vector<AZ::u32> indices;
        cutouts.reserve(AZStd::min<size_t>(snapshot->m_cutouts.size(), MaximumGpuCutouts));
        AZ::u32 rejected = 0;
        for (const auto& source : snapshot->m_cutouts)
        {
            if (!source.m_affectTerrainRendering || !source.m_data)
            {
                continue;
            }
            if (cutouts.size() >= MaximumGpuCutouts || indices.size() / 3 + source.m_data->m_indices.size() / 3 > MaximumGpuTriangles)
            {
                ++rejected;
                continue;
            }

            const AZ::u32 baseVertex = aznumeric_cast<AZ::u32>(vertices.size());
            const AZ::u32 firstIndex = aznumeric_cast<AZ::u32>(indices.size());
            for (const auto& vertex : source.m_data->m_vertices)
            {
                vertices.emplace_back(vertex.GetX(), vertex.GetY(), vertex.GetZ(), 0.0f);
            }
            for (const AZ::u32 index : source.m_data->m_indices)
            {
                indices.push_back(baseVertex + index);
            }

            const AZ::Quaternion inverseRotation = source.m_worldFromLocal.GetRotation().GetConjugate();
            const AZ::Vector3 translation = source.m_worldFromLocal.GetTranslation();
            const float inverseScale = 1.0f / source.m_worldFromLocal.GetUniformScale();
            const AZ::Vector3 boundsMin = source.m_data->m_localBounds.GetMin();
            const AZ::Vector3 boundsMax = source.m_data->m_localBounds.GetMax();
            GpuCutout gpu;
            gpu.m_inverseRotation =
                AZ::Vector4(inverseRotation.GetX(), inverseRotation.GetY(), inverseRotation.GetZ(), inverseRotation.GetW());
            gpu.m_translationAndInverseScale = AZ::Vector4(translation.GetX(), translation.GetY(), translation.GetZ(), inverseScale);
            gpu.m_localBoundsMinAndMargin =
                AZ::Vector4(boundsMin.GetX(), boundsMin.GetY(), boundsMin.GetZ(), aznumeric_cast<float>(source.m_renderLocalMargin));
            gpu.m_localBoundsMax = AZ::Vector4(boundsMax.GetX(), boundsMax.GetY(), boundsMax.GetZ(), 0.0f);
            gpu.m_firstIndex = firstIndex;
            gpu.m_indexCount = aznumeric_cast<AZ::u32>(source.m_data->m_indices.size());
            gpu.m_operation = source.m_operation == TerrainExistenceOperation::RestoreTerrain ? 1u : 0u;
            cutouts.push_back(gpu);
        }

        const GpuCutout emptyCutout{};
        const AZ::Vector4 emptyVertex = AZ::Vector4::CreateZero();
        const AZ::u32 emptyIndex = 0;
        m_cutoutsBuffer = CreateReadOnlyBuffer(
            "TerrainMeshCutouts",
            sizeof(GpuCutout),
            aznumeric_cast<AZ::u32>(cutouts.size()),
            cutouts.empty() ? &emptyCutout : cutouts.data());
        m_verticesBuffer = CreateReadOnlyBuffer(
            "TerrainMeshCutoutVertices",
            sizeof(AZ::Vector4),
            aznumeric_cast<AZ::u32>(vertices.size()),
            vertices.empty() ? &emptyVertex : vertices.data());
        m_indicesBuffer = CreateReadOnlyBuffer(
            "TerrainMeshCutoutIndices",
            sizeof(AZ::u32),
            aznumeric_cast<AZ::u32>(indices.size()),
            indices.empty() ? &emptyIndex : indices.data());

        m_statistics.m_snapshotRevision = snapshot->m_revision;
        ++m_statistics.m_uploadCount;
        m_statistics.m_activeCutoutCount = aznumeric_cast<AZ::u32>(cutouts.size());
        m_statistics.m_vertexCount = aznumeric_cast<AZ::u32>(vertices.size());
        m_statistics.m_triangleCount = aznumeric_cast<AZ::u32>(indices.size() / 3);
        m_statistics.m_resourceLimitRejectCount = rejected;
        AZ_TracePrintf(
            "TerrainMeshCutout",
            "Published render snapshot %llu: %u cutouts, %u vertices, %u triangles, %zu render-topology queries.\n",
            m_statistics.m_snapshotRevision,
            m_statistics.m_activeCutoutCount,
            m_statistics.m_vertexCount,
            m_statistics.m_triangleCount,
            snapshot->m_renderGeometryQueries.size());
        if (rejected != 0)
        {
            AZ_Warning(
                "TerrainMeshCutout",
                false,
                "%u render cutouts exceeded the GPU limits (%u cutouts, %u triangles) and were skipped.",
                rejected,
                MaximumGpuCutouts,
                MaximumGpuTriangles);
        }
    }

    void TerrainMeshCutoutFeatureProcessor::OnBeginPrepareRender()
    {
        RebuildGpuData();
        auto* terrain = GetParentScene()->GetFeatureProcessor<Terrain::TerrainFeatureProcessor>();
        const auto material = terrain ? terrain->GetMaterial() : nullptr;
        if (material != m_observedTerrainMaterial)
        {
            m_observedTerrainMaterial = material;
            if (m_sceneChannel) m_sceneChannel->m_materialChanged.Signal(material);
        }
        const auto cutoutSrg = material ? material->GetShaderResourceGroup() : nullptr;
        auto* registry = AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get();
        if (!cutoutSrg || !material->CanCompile())
        {
            if (registry)
                registry->ClearGapActivation(GetParentScene());
            m_gapAwaitingActivation = false;
            return;
        }
        const auto countProperty = material->FindPropertyIndex(AZ::Name("settings.meshCutoutCount"));
        const auto revisionProperty = material->FindPropertyIndex(AZ::Name("settings.meshCutoutRevision"));
        const bool propertiesAvailable = countProperty.IsValid() && revisionProperty.IsValid();
        if (propertiesAvailable)
        {
            material->SetPropertyValue(countProperty, m_statistics.m_activeCutoutCount);
            material->SetPropertyValue(
                revisionProperty, aznumeric_cast<AZ::u32>(m_statistics.m_snapshotRevision & AZStd::numeric_limits<AZ::u32>::max()));
        }
        const bool cutoutsBound = m_cutoutsBuffer && cutoutSrg->SetBufferView(m_cutoutsIndex, m_cutoutsBuffer->GetBufferView());
        const bool verticesBound = m_verticesBuffer && cutoutSrg->SetBufferView(m_verticesIndex, m_verticesBuffer->GetBufferView());
        const bool indicesBound = m_indicesBuffer && cutoutSrg->SetBufferView(m_indicesIndex, m_indicesBuffer->GetBufferView());
        const AZ::u32 closedCount = cutoutsBound && verticesBound && indicesBound ? m_statistics.m_activeCutoutCount : 0;
        if (countProperty.IsValid())
            material->SetPropertyValue(countProperty, closedCount);
        const bool countBound = cutoutSrg->SetConstant(m_cutoutCountIndex, closedCount);
        AZ_Error(
            "TerrainMeshCutout",
            propertiesAvailable && countBound && cutoutsBound && verticesBound && indicesBound,
            "The active terrain material does not expose the TG mesh-cutout SRG inputs. "
            "Verify that the project terrain material type and shaders were processed successfully.");
        const auto snapshot =
            m_sceneChannel ? m_sceneChannel->m_snapshot.load(std::memory_order_acquire) : TerrainMeshCutoutRenderSnapshotPtr{};
        const bool materialChanged = material != m_gapBoundMaterial || cutoutSrg != m_gapBoundSrg;
        if (materialChanged && registry)
        {
            // A new material/SRG has not yet compiled these bindings, even when
            // the retained asset/placement publication itself is unchanged.
            registry->ClearGapActivation(GetParentScene());
        }
        if (snapshot && (snapshot != m_gapPreparedSnapshot || (materialChanged && !m_gapBuffersReady)))
        {
            auto prepared = PrepareTerrainMeshHeightGapGpuData(snapshot->m_meshHeightGaps, &m_gapData);
            auto descriptors = m_emptyGapDescriptors;
            auto masks = prepared.m_descriptors.empty() || prepared.m_masksChanged ? m_emptyGapWords : m_gapMaskBuffer;
            if (!prepared.m_descriptors.empty())
            {
                const bool sameDescriptors = m_gapDescriptorsBuffer && m_gapData.m_descriptors.size() == prepared.m_descriptors.size() &&
                    std::memcmp(
                        m_gapData.m_descriptors.data(),
                        prepared.m_descriptors.data(),
                        prepared.m_descriptors.size() * sizeof(TerrainMeshHeightGapGpuDescriptor)) == 0;
                if (sameDescriptors)
                    descriptors = m_gapDescriptorsBuffer;
                else
                {
                    descriptors = CreateReadOnlyBuffer(
                        "TerrainMeshGapDescriptors",
                        sizeof(TerrainMeshHeightGapGpuDescriptor),
                        aznumeric_cast<AZ::u32>(prepared.m_descriptors.size()),
                        prepared.m_descriptors.data());
                    if (descriptors)
                        ++m_statistics.m_gapDescriptorUploads;
                }
                if (prepared.m_masksChanged || !masks)
                {
                    masks = CreateReadOnlyBuffer(
                        "TerrainMeshGapWords",
                        sizeof(AZ::u32),
                        aznumeric_cast<AZ::u32>(prepared.m_words->size()),
                        prepared.m_words->data());
                    if (masks)
                        ++m_statistics.m_gapMaskUploads;
                }
            }
            m_gapBuffersReady = descriptors && masks;
            if (!m_gapBuffersReady)
            {
                ++m_statistics.m_gapResourceFailures;
                AZ_Warning(
                    "TerrainMeshCutout",
                    false,
                    "Gap GPU allocation failed; rendering and coupled CPU gaps are neutral until a new publication or material reload.");
            }
            m_gapDescriptorsBuffer = descriptors;
            m_gapMaskBuffer = masks;
            m_gapData = AZStd::move(prepared);
            m_gapPreparedSnapshot = snapshot;
            m_statistics.m_gapRejectedContributions = m_gapData.m_rejected;
            if (m_gapData.m_rejected)
                AZ_Warning(
                    "TerrainMeshCutout",
                    false,
                    "%u mesh-height gaps rejected by GPU format/resource limits; coupled CPU removal is neutral.",
                    m_gapData.m_rejected);
        }

        const auto gapCountProperty = material->FindPropertyIndex(AZ::Name("settings.meshHeightGapCount"));
        const auto gapRevisionProperty = material->FindPropertyIndex(AZ::Name("settings.meshHeightGapRevision"));
        const auto descriptorBuffer = m_gapBuffersReady ? m_gapDescriptorsBuffer : m_emptyGapDescriptors;
        const auto maskBuffer = m_gapBuffersReady ? m_gapMaskBuffer : m_emptyGapWords;
        const auto descriptorIndex = cutoutSrg->FindShaderInputBufferIndex(AZ::Name("m_meshHeightGaps"));
        const auto wordsIndex = cutoutSrg->FindShaderInputBufferIndex(AZ::Name("m_meshHeightGapWords"));
        const auto gapCountIndex = cutoutSrg->FindShaderInputConstantIndex(AZ::Name("m_meshHeightGapCount"));
        const bool descriptorsBound =
            descriptorBuffer && descriptorIndex.IsValid() && cutoutSrg->SetBufferView(descriptorIndex, descriptorBuffer->GetBufferView());
        const bool masksBound = maskBuffer && wordsIndex.IsValid() && cutoutSrg->SetBufferView(wordsIndex, maskBuffer->GetBufferView());
        const bool current = snapshot && m_sceneChannel->m_snapshot.load(std::memory_order_acquire) == snapshot;
        const bool ready =
            current && m_gapBuffersReady && descriptorsBound && masksBound && gapCountProperty.IsValid() && gapRevisionProperty.IsValid();
        const AZ::u32 gapCount = ready ? aznumeric_cast<AZ::u32>(m_gapData.m_descriptors.size()) : 0;
        if (gapCountProperty.IsValid())
            material->SetPropertyValue(gapCountProperty, gapCount);
        if (gapRevisionProperty.IsValid())
            material->SetPropertyValue(gapRevisionProperty, snapshot ? AZ::u32(snapshot->m_revision) : 0u);
        const bool gapCountBound = gapCountIndex.IsValid() && cutoutSrg->SetConstant(gapCountIndex, gapCount);
        const bool bindingsAvailable =
            descriptorsBound && masksBound && gapCountBound && gapCountProperty.IsValid() && gapRevisionProperty.IsValid();
        if (!bindingsAvailable && !m_reportedGapBindingFailure)
        {
            ++m_statistics.m_gapResourceFailures;
            AZ_Warning(
                "TerrainMeshCutout",
                false,
                "The active terrain material cannot bind TG mesh-height-gap resources. Coupled CPU gaps are neutral. "
                "Reprocess PbrTerrain.materialtype and terrain forward/depth shaders, then reload the material.");
        }
        m_reportedGapBindingFailure = !bindingsAvailable;
        if (!ready || !gapCountBound)
        {
            if (registry)
                registry->ClearGapActivation(GetParentScene());
            if (!current)
                ++m_statistics.m_gapStaleResults;
        }
        const auto activation =
            m_sceneChannel ? m_sceneChannel->m_activation.load(std::memory_order_acquire) : TerrainMeshHeightGapActivationPtr{};
        m_gapAwaitingActivation = ready && gapCountBound &&
            (m_gapAwaitingActivation || snapshot != m_gapBoundSnapshot || materialChanged || !activation ||
             activation->m_revision != snapshot->m_revision);
        m_gapBoundSnapshot = ready && gapCountBound ? snapshot : TerrainMeshCutoutRenderSnapshotPtr{};
        m_gapBoundMaterial = material;
        m_gapBoundSrg = cutoutSrg;
        m_statistics.m_activeGapInstances = gapCount;
        m_statistics.m_uniqueGapMasks = ready ? aznumeric_cast<AZ::u32>(m_gapData.m_masks.size()) : 0;
        m_statistics.m_gapMaskBytes = ready ? m_gapData.m_maskBytes : 0;
        m_statistics.m_gapTileBytes = ready ? m_gapData.m_tileBytes : 0;
        // Material properties are compiled by TerrainFeatureProcessor, and the
        // material system queues the SRG later in FrameUpdate. Do not queue it twice.
    }

    void TerrainMeshCutoutFeatureProcessor::OnRenderEnd()
    {
        if (!m_gapAwaitingActivation || !m_gapBoundSnapshot || !m_gapBoundMaterial || !m_gapBoundSrg ||
            m_gapBoundSrg->IsQueuedForCompile() || m_gapBoundMaterial->NeedsCompile())
            return;
        auto* terrain = GetParentScene()->GetFeatureProcessor<Terrain::TerrainFeatureProcessor>();
        if (!terrain || terrain->GetMaterial() != m_gapBoundMaterial || m_gapBoundMaterial->GetShaderResourceGroup() != m_gapBoundSrg)
            return;
        if (auto* registry = AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get())
        {
            if (registry->ActivateGaps(GetParentScene(), m_gapBoundSnapshot, m_gapData.m_admitted))
            {
                m_statistics.m_gapPublicationRevision = m_gapBoundSnapshot->m_revision;
                AZ_TracePrintf(
                    "TerrainMeshCutout",
                    "Activated gap publication %llu: %u instances, %u unique masks, %llu mask bytes, "
                    "%llu tile bytes, %llu mask uploads, %llu descriptor uploads.\n",
                    m_statistics.m_gapPublicationRevision,
                    m_statistics.m_activeGapInstances,
                    m_statistics.m_uniqueGapMasks,
                    m_statistics.m_gapMaskBytes,
                    m_statistics.m_gapTileBytes,
                    m_statistics.m_gapMaskUploads,
                    m_statistics.m_gapDescriptorUploads);
            }
            else
                ++m_statistics.m_gapStaleResults;
        }
        m_gapAwaitingActivation = false;
    }
} // namespace TerrainCompositor
