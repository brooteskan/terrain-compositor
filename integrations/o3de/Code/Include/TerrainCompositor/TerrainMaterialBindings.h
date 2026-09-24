#pragma once
#include <Atom/RHI.Reflect/ShaderResourceGroupLayout.h>

namespace TerrainCompositor
{
    // Additional graph fields and resource order are deliberately unrestricted.
    inline bool ValidateTerrainRendererBindings(
        const AZ::RHI::ShaderResourceGroupLayout* layout,
        const AZ::RHI::ShaderResourceGroupLayout* required)
    {
        if (!layout || !required) return false;
        // Graph constants and textures may vary; renderer resources must keep their kinds and element strides.
        for (const char* name : { "m_meshCutouts", "m_meshCutoutVertices", "m_meshCutoutIndices", "m_meshHeightGaps", "m_meshHeightGapWords" })
        {
            const auto actualIndex = layout->FindShaderInputBufferIndex(AZ::Name(name));
            const auto requiredIndex = required->FindShaderInputBufferIndex(AZ::Name(name));
            if (!actualIndex.IsValid() || !requiredIndex.IsValid()) return false;
            const auto& actual = layout->GetShaderInput(actualIndex);
            const auto& expected = required->GetShaderInput(requiredIndex);
            if (actual.m_type != expected.m_type || actual.m_access != expected.m_access ||
                actual.m_count != expected.m_count || actual.m_strideSize != expected.m_strideSize) return false;
        }
        for (const char* name : { "m_meshCutoutCount", "m_meshCutoutRevision", "m_meshHeightGapCount", "m_meshHeightGapRevision" })
        {
            const auto index = layout->FindShaderInputConstantIndex(AZ::Name(name));
            if (!index.IsValid() || layout->GetShaderInput(index).m_constantByteCount != sizeof(AZ::u32)) return false;
        }
        return true;
    }
}
