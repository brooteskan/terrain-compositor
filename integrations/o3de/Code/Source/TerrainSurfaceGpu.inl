// Initial editor path: upload immutable source labels on publication; evaluate weights in the graph.
// No texture baking, appearance cache, or per-frame geometry processing.
namespace TerrainCompositor
{
    void TerrainMeshCutoutFeatureProcessor::BindSurfaceData(const AZ::Data::Instance<AZ::RPI::Material>& material)
    {
        const auto snapshot = m_sceneChannel ? m_sceneChannel->m_snapshot.load(std::memory_order_acquire) : nullptr;
        if (!snapshot) return;
        if (!m_surfaceBuffer || m_surfaceRevision != snapshot->m_revision)
        {
            AZStd::vector<AZ::u32> words;
            const auto putFloat = [&words](size_t at, double value)
            { const float f = static_cast<float>(value); memcpy(&words[at], &f, sizeof(f)); };
            const auto append = [&words](size_t count)
            { const auto offset = aznumeric_cast<AZ::u32>(words.size()); words.resize(words.size() + count); return offset; };
            const AZ::u32 regions = aznumeric_cast<AZ::u32>(snapshot->m_renderGeometryQueries.size());
            append(regions * 10);
            for (AZ::u32 r = 0; r < regions; ++r)
            {
                const auto& state = snapshot->m_renderGeometryQueries[r].m_surfaces;
                if (!state || !state->m_surfacePalette.IsValid() || !state->m_regionBounds.IsValid()) continue;
                const size_t h = r * 10;
                putFloat(h, state->m_regionBounds.GetMin().GetX()); putFloat(h + 1, state->m_regionBounds.GetMin().GetY());
                putFloat(h + 2, state->m_regionBounds.GetMax().GetX()); putFloat(h + 3, state->m_regionBounds.GetMax().GetY());
                words[h + 4] = append(state->m_surfacePalette.m_entries.size() * 2);
                words[h + 5] = aznumeric_cast<AZ::u32>(state->m_surfacePalette.m_entries.size());
                for (size_t i = 0; i < state->m_surfacePalette.m_entries.size(); ++i)
                {
                    words[words[h + 4] + i * 2] = state->m_surfacePalette.m_entries[i].m_exportedId;
                    words[words[h + 4] + i * 2 + 1] = static_cast<AZ::u32>(state->m_surfacePalette.m_entries[i].m_surfaceTag);
                }
                const auto tag = [&state](AZ::u16 id) -> AZ::u32
                {
                    for (const auto& entry : state->m_surfacePalette.m_entries)
                        if (entry.m_exportedId == id) return static_cast<AZ::u32>(entry.m_surfaceTag);
                    return 0;
                };
                words[h + 8] = append(state->m_surfacePalette.m_baseWeights.size() * 2);
                words[h + 9] = aznumeric_cast<AZ::u32>(state->m_surfacePalette.m_baseWeights.size());
                for (size_t i = 0; i < state->m_surfacePalette.m_baseWeights.size(); ++i)
                {
                    const size_t b = words[h + 8] + i * 2;
                    words[b] = static_cast<AZ::u32>(state->m_surfacePalette.m_baseWeights[i].m_surfaceType);
                    putFloat(b + 1, state->m_surfacePalette.m_baseWeights[i].m_weight);
                }
                words[h + 6] = append(state->m_surfaceStamps.size() * 20);
                words[h + 7] = aznumeric_cast<AZ::u32>(state->m_surfaceStamps.size());
                for (size_t i = 0; i < state->m_surfaceStamps.size(); ++i)
                {
                    const auto& s = state->m_surfaceStamps[i];
                    const size_t d = words[h + 6] + i * 20;
                    if (s.m_mesh.m_data)
                    {
                        const auto& m = s.m_mesh; const auto& data = *m.m_data;
                        words[d] = 1; words[d + 2] = data.m_width - 1; words[d + 3] = data.m_height - 1;
                        putFloat(d + 4, m.m_originX); putFloat(d + 5, m.m_originY);
                        putFloat(d + 6, m.m_cosYaw); putFloat(d + 7, m.m_sinYaw); putFloat(d + 8, m.m_inverseScale);
                        putFloat(d + 9, m.m_localMinX); putFloat(d + 10, m.m_localMinY);
                        putFloat(d + 11, m.m_localMaxX); putFloat(d + 12, m.m_localMaxY);
                        putFloat(d + 13, m.m_strength); putFloat(d + 14, m.m_feather);
                        putFloat(d + 15, m.m_featherExponent); putFloat(d + 16, m.m_edgeInset);
                        putFloat(d + 17, 1.0f / data.m_gridSpacing.GetX()); putFloat(d + 18, 1.0f / data.m_gridSpacing.GetY());
                        words[d + 1] = append(GetTerrainMeshHeightCellCount(data) * 2);
                        for (size_t t = 0; t < data.m_cellMaterialSlots.size(); ++t)
                        {
                            const auto slot = data.m_cellMaterialSlots[t];
                            if (!IsTerrainMeshHeightCellUncovered(data, t / 2) && slot < s.m_meshSlotTags.size())
                                words[words[d + 1] + t] = static_cast<AZ::u32>(s.m_meshSlotTags[slot]);
                        }
                        words[d + 19] = append(data.m_cellDiagonalBits.size());
                        for (size_t w = 0; w < data.m_cellDiagonalBits.size(); ++w) words[words[d + 19] + w] = data.m_cellDiagonalBits[w];
                    }
                    else if (s.m_surfaceIdA)
                    {
                        const auto& p = s.m_placement; const auto& a = *s.m_surfaceIdA;
                        words[d + 2] = a.m_width; words[d + 3] = a.m_height;
                        putFloat(d + 4, p.m_centerX); putFloat(d + 5, p.m_centerY);
                        putFloat(d + 6, p.m_cosYaw); putFloat(d + 7, p.m_sinYaw); putFloat(d + 8, p.m_inverseScale);
                        putFloat(d + 9, -p.m_halfWidth); putFloat(d + 10, -p.m_halfDepth);
                        putFloat(d + 11, p.m_halfWidth); putFloat(d + 12, p.m_halfDepth);
                        putFloat(d + 13, s.m_strength); putFloat(d + 14, s.m_feather);
                        putFloat(d + 15, s.m_featherExponent); putFloat(d + 16, p.m_edgeInset);
                        words[d + 1] = append(a.m_rawSamples.size() * 3);
                        for (size_t t = 0; t < a.m_rawSamples.size(); ++t)
                        {
                            const size_t pixel = words[d + 1] + t * 3;
                            words[pixel] = tag(a.m_rawSamples[t]);
                            words[pixel + 1] = s.m_surfaceIdB ? tag(s.m_surfaceIdB->m_rawSamples[t]) : 0;
                            putFloat(pixel + 2, s.m_surfaceBlend ? double(s.m_surfaceBlend->m_rawSamples[t]) / 65535.0 : 0.0);
                        }
                    }
                }
            }
            if (words.empty()) words.push_back(0);
            m_surfaceBuffer = CreateReadOnlyBuffer("TerrainSurfaceLabels", sizeof(AZ::u32), aznumeric_cast<AZ::u32>(words.size()), words.data());
            m_surfaceRegionCount = m_surfaceBuffer ? regions : 0;
            m_surfaceRevision = snapshot->m_revision;
            AZ_Warning("TerrainSurface", m_surfaceBuffer, "Cannot upload terrain surface labels; graph weights are unavailable.");
        }
        const auto srg = material->GetShaderResourceGroup();
        const auto input = srg->FindShaderInputBufferIndex(AZ::Name("m_surfaceWords"));
        const auto count = material->FindPropertyIndex(AZ::Name("settings.surfaceRegionCount"));
        const auto revision = material->FindPropertyIndex(AZ::Name("settings.surfaceRevision"));
        if (!input.IsValid() || !count.IsValid() || !revision.IsValid()) return;
        const bool bound = m_surfaceBuffer && srg->SetBufferView(input, m_surfaceBuffer->GetBufferView());
        material->SetPropertyValue(count, bound ? m_surfaceRegionCount : 0u);
        material->SetPropertyValue(revision, static_cast<AZ::u32>(m_surfaceRevision));
    }
}
