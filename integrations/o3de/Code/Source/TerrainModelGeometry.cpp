#include <TerrainCompositor/TerrainModelGeometry.h>

#include <Atom/RPI.Reflect/Model/ModelLodAsset.h>
#include <AzCore/Name/Name.h>
#include <AzCore/std/limits.h>

namespace TerrainCompositor
{
    TerrainModelGeometryValidation ExtractTerrainModelGeometry(
        AZ::RPI::ModelAsset& model, const AZ::Data::AssetId& assetId, TerrainModelGeometry& result)
    {
        result = {};
        result.m_assetId = assetId;
        const auto lods = model.GetLodAssets();
        if (lods.empty() || !lods.front().IsReady())
        {
            return TerrainModelGeometryValidation::Empty;
        }

        model.AddRefBufferAssets();
        struct BufferReferenceGuard
        {
            AZ::RPI::ModelAsset& m_model;
            ~BufferReferenceGuard()
            {
                m_model.ReleaseRefBufferAssets();
            }
        } guard{ model };

        const AZ::Name positionSemantic("POSITION");
        for (const auto& mesh : lods.front()->GetMeshes())
        {
            const AZ::u32 vertexCount = mesh.GetVertexCount();
            if (vertexCount == 0)
            {
                return TerrainModelGeometryValidation::Empty;
            }
            if (vertexCount > TerrainModelGeometryMaximumVertices ||
                result.m_positions.size() > TerrainModelGeometryMaximumVertices - vertexCount)
            {
                return TerrainModelGeometryValidation::ResourceLimit;
            }
            const auto* positionView = mesh.GetSemanticBufferAssetView(positionSemantic);
            if (!positionView)
            {
                return TerrainModelGeometryValidation::MissingPositionStream;
            }
            if (positionView->GetBufferViewDescriptor().m_elementSize != sizeof(float) * 3)
            {
                return TerrainModelGeometryValidation::UnsupportedPositionFormat;
            }
            const auto values = mesh.GetSemanticBufferTyped<float>(positionSemantic);
            if (values.size() < size_t(vertexCount) * 3)
            {
                return TerrainModelGeometryValidation::BufferData;
            }

            const AZ::u32 baseVertex = aznumeric_cast<AZ::u32>(result.m_positions.size());
            result.m_positions.reserve(result.m_positions.size() + vertexCount);
            for (AZ::u32 vertex = 0; vertex < vertexCount; ++vertex)
            {
                const AZ::Vector3 position(values[vertex * 3], values[vertex * 3 + 1], values[vertex * 3 + 2]);
                if (!position.IsFinite())
                {
                    return TerrainModelGeometryValidation::NonFinitePosition;
                }
                result.m_positions.push_back(position);
                result.m_localBounds.AddPoint(position);
            }

            const AZ::u32 indexCount = mesh.GetIndexCount();
            if (indexCount == 0 || indexCount % 3 != 0)
            {
                return TerrainModelGeometryValidation::IndexCount;
            }
            if (indexCount > TerrainModelGeometryMaximumIndices ||
                result.m_indices.size() > TerrainModelGeometryMaximumIndices - indexCount)
            {
                return TerrainModelGeometryValidation::ResourceLimit;
            }
            const auto& descriptor = mesh.GetIndexBufferAssetView().GetBufferViewDescriptor();
            if (descriptor.m_elementSize != sizeof(AZ::u16) && descriptor.m_elementSize != sizeof(AZ::u32))
            {
                return TerrainModelGeometryValidation::UnsupportedIndexFormat;
            }
            result.m_indices.reserve(result.m_indices.size() + indexCount);
            const auto appendIndices = [&](const auto& source)
            {
                if (source.size() < indexCount)
                {
                    return false;
                }
                for (AZ::u32 index = 0; index < indexCount; ++index)
                {
                    const AZ::u32 localIndex = source[index];
                    if (localIndex >= vertexCount)
                    {
                        return false;
                    }
                    result.m_indices.push_back(baseVertex + localIndex);
                }
                return true;
            };
            const bool valid = descriptor.m_elementSize == sizeof(AZ::u16) ? appendIndices(mesh.GetIndexBufferTyped<AZ::u16>())
                                                                           : appendIndices(mesh.GetIndexBufferTyped<AZ::u32>());
            if (!valid)
            {
                return TerrainModelGeometryValidation::IndexOutOfRange;
            }
        }
        return result.m_positions.empty() || result.m_indices.empty() ? TerrainModelGeometryValidation::Empty
                                                                      : TerrainModelGeometryValidation::Valid;
    }

    const char* GetTerrainModelGeometryValidationMessage(TerrainModelGeometryValidation validation)
    {
        switch (validation)
        {
        case TerrainModelGeometryValidation::Valid:
            return "Valid";
        case TerrainModelGeometryValidation::Empty:
            return "LOD0 contains no ready indexed mesh geometry.";
        case TerrainModelGeometryValidation::MissingPositionStream:
            return "An LOD0 mesh section has no POSITION stream.";
        case TerrainModelGeometryValidation::UnsupportedPositionFormat:
            return "POSITION data must use three 32-bit floats.";
        case TerrainModelGeometryValidation::IndexCount:
            return "Every LOD0 mesh section must contain a nonempty triangle index list.";
        case TerrainModelGeometryValidation::UnsupportedIndexFormat:
            return "Indices must be 16-bit or 32-bit unsigned integers.";
        case TerrainModelGeometryValidation::BufferData:
            return "An LOD0 model buffer is shorter than its declared element count.";
        case TerrainModelGeometryValidation::IndexOutOfRange:
            return "An LOD0 index is outside its mesh section's vertex range.";
        case TerrainModelGeometryValidation::NonFinitePosition:
            return "Model positions must all be finite.";
        case TerrainModelGeometryValidation::ResourceLimit:
            return "LOD0 exceeds the terrain geometry resource limit.";
        }
        return "Unsupported model geometry.";
    }
} // namespace TerrainCompositor
