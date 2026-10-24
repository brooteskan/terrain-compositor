#include <TerrainCompositor/TerrainOverlap.h>
#include <TerrainCompositor/TerrainHeightPrecision.h>
#include <AzCore/std/algorithm.h>
#include <cmath>
#include <cstring>

namespace TerrainCompositor
{
    PreparedTerrainOverlap PrepareTerrainOverlap(const TerrainOverlapConfig& config,
        AZStd::span<const PreparedHeightContributor> contributors)
    {
        PreparedTerrainOverlap result;
        result.m_config = config;
        if (!config.m_enabled) return result;
        auto fail = [&](const char* message) { result.m_diagnostic = message; return result; };
        if (!std::isfinite(config.m_width) || config.m_width < 0 || !std::isfinite(config.m_groundHeight))
            return fail("Overlap width must be finite and nonnegative; ground height must be finite.");
        if (config.m_width == 0) return result;
        const size_t count = config.m_sources.size() + (config.m_flatGround ? 1 : 0);
        if (count < 2 || count > TerrainOverlapMaximumSources)
            return fail("Overlap requires two to four source instances including flat ground; existing shading is retained.");
        AZ::u32 slots = 0;
        const auto claim = [&slots](AZ::u32 slot)
        {
            if (slot < 1 || slot > 4 || (slots & (1u << slot))) return false;
            slots |= 1u << slot;
            return true;
        };
        if (config.m_flatGround && !claim(config.m_groundMaterialSlot))
            return fail("Ground material slot must be in [1,4] and unique.");
        for (const auto& source : contributors)
        {
            if (source.m_type == PreparedHeightContributor::Type::Mesh && !source.m_mesh.m_affectTerrainRendering) continue;
            if (AZStd::none_of(config.m_sources.begin(), config.m_sources.end(),
                [&](const auto& binding) { return binding.m_stampEntityId == source.GetEntityId(); }))
                return fail("Bind every rendered height stamp in this composition; an unbound source could hide a blended material. Split larger sets into nonoverlapping composition regions.");
        }
        for (size_t i = 0; i < config.m_sources.size(); ++i)
        {
            const auto& binding = config.m_sources[i];
            if (!binding.m_stampEntityId.IsValid() || !claim(binding.m_materialSlot))
                return fail("Each overlap source needs a stamp entity and a unique material slot in [1,4].");
            for (size_t j = 0; j < i; ++j)
                if (config.m_sources[j].m_stampEntityId == binding.m_stampEntityId)
                    return fail("A stamp instance may only be bound once in an overlap set.");
            const auto found = AZStd::find_if(contributors.begin(), contributors.end(),
                [&](const auto& c) { return c.GetEntityId() == binding.m_stampEntityId; });
            if (found == contributors.end())
                return fail("Overlap stamp is unavailable or belongs to another composition; check asset, strength, placement and target.");
            if (found->m_type == PreparedHeightContributor::Type::Mesh && !found->m_mesh.m_affectTerrainRendering)
                return fail("An overlap source is collision-only; enable terrain rendering or remove its material binding.");
        }
        return result;
    }

    bool AppendTerrainOverlapRegion(AZStd::vector<AZ::u32>& words, size_t header, const AZ::Aabb& bounds,
        const PreparedTerrainOverlap& overlap, AZStd::span<const PreparedHeightContributor> contributors, size_t maximumWords)
    {
        if (header + TerrainOverlapRegionWords > words.size() || !bounds.IsValid() || !overlap.IsActive()) return false;
        const size_t start = words.size();
        const auto& config = overlap.m_config;
        const size_t count = config.m_sources.size() + (config.m_flatGround ? 1 : 0);
        const auto sampleWords = [&overlap](size_t n) { return overlap.m_heightFp16 ? (n + 1) / 2 : n; };
        size_t required = count * TerrainOverlapSourceWords;
        for (const auto& c : contributors)
        {
            const auto binding = AZStd::find_if(config.m_sources.begin(), config.m_sources.end(),
                [&](const auto& b) { return b.m_stampEntityId == c.GetEntityId(); });
            if (binding == config.m_sources.end()) continue;
            if (c.m_type == PreparedHeightContributor::Type::Mesh)
            {
                if (!c.m_mesh.m_data) return false;
                const auto& d = *c.m_mesh.m_data;
                required += sampleWords(d.m_localHeights.size()) + d.m_uncoveredCellBits.size() + d.m_cellDiagonalBits.size();
            }
            else
            {
                if (!c.m_image.m_image) return false;
                required += sampleWords(c.m_image.m_reconstruction ? c.m_image.m_reconstruction->m_samples.size() : c.m_image.m_image->m_samples.size());
            }
        }
        if (start > maximumWords || required > maximumWords - start) return false;
        words.resize(start + count * TerrainOverlapSourceWords);
        const auto f = [&words](size_t at, double value)
        { const float v = static_cast<float>(value); memcpy(&words[at], &v, sizeof(v)); };
        const auto floats = [&words, &overlap, &sampleWords](const auto& values)
        {
            auto offset = aznumeric_cast<AZ::u32>(words.size());
            words.resize(words.size() + sampleWords(values.size()), 0);
            if (overlap.m_heightFp16)
            {
                for (size_t i = 0; i < values.size(); ++i)
                    words[offset + i / 2] |= AZ::u32(PackTerrainHeight16(values[i])) << ((i & 1) * 16);
            }
            else if (!values.empty()) memcpy(words.data() + offset, values.data(), values.size() * sizeof(float));
            return offset;
        };
        const auto integers = [&words](const auto& values)
        {
            auto offset = aznumeric_cast<AZ::u32>(words.size());
            words.insert(words.end(), values.begin(), values.end());
            return offset;
        };
        f(header, bounds.GetMin().GetX()); f(header + 1, bounds.GetMin().GetY());
        f(header + 2, bounds.GetMax().GetX()); f(header + 3, bounds.GetMax().GetY());
        words[header + 4] = aznumeric_cast<AZ::u32>(start); words[header + 5] = aznumeric_cast<AZ::u32>(count);
        f(header + 6, config.m_width); words[header + 7] = (config.m_retainedSurfaces ? 1u : 0u) | (overlap.m_heightFp16 ? 2u : 0u);
        size_t index = 0;
        if (config.m_flatGround)
        {
            const auto p = start;
            words[p] = 0; words[p + 1] = config.m_groundMaterialSlot - 1;
            f(p + 6, 1); f(p + 8, 1); f(p + 9, bounds.GetMin().GetX()); f(p + 10, bounds.GetMin().GetY());
            f(p + 11, bounds.GetMax().GetX()); f(p + 12, bounds.GetMax().GetY()); f(p + 13, config.m_groundHeight);
            f(p + 15, 1); f(p + 17, 1);
            ++index;
        }
        // Prepared contributors already use priority + persistent key, independent of binding/list/registration order.
        for (const auto& c : contributors)
        {
            const auto binding = AZStd::find_if(config.m_sources.begin(), config.m_sources.end(),
                [&](const auto& b) { return b.m_stampEntityId == c.GetEntityId(); });
            if (binding == config.m_sources.end()) continue;
            const auto p = start + index++ * TerrainOverlapSourceWords;
            words[p + 1] = binding->m_materialSlot - 1;
            if (c.m_type == PreparedHeightContributor::Type::Mesh)
            {
                const auto& s = c.m_mesh; const auto& d = *s.m_data;
                words[p] = 2; words[p + 2] = d.m_width; words[p + 3] = d.m_height;
                f(p + 4, s.m_originX); f(p + 5, s.m_originY); f(p + 6, s.m_cosYaw); f(p + 7, s.m_sinYaw);
                f(p + 8, s.m_inverseScale); f(p + 9, s.m_localMinX); f(p + 10, s.m_localMinY);
                f(p + 11, s.m_localMaxX); f(p + 12, s.m_localMaxY); f(p + 13, s.m_heightOrigin); f(p + 14, s.m_scale);
                f(p + 15, s.m_strength); f(p + 16, s.m_feather); f(p + 17, s.m_featherExponent); f(p + 18, s.m_edgeInset);
                words[p + 19] = floats(d.m_localHeights); words[p + 20] = integers(d.m_uncoveredCellBits);
                words[p + 21] = integers(d.m_cellDiagonalBits);
                f(p + 22, 1.0 / d.m_gridSpacing.GetX()); f(p + 23, 1.0 / d.m_gridSpacing.GetY());
            }
            else
            {
                const auto& s = c.m_image; const auto& d = *s.m_image; const auto& t = s.m_placement;
                words[p] = 1; words[p + 2] = d.m_width; words[p + 3] = d.m_height;
                f(p + 4, t.m_centerX); f(p + 5, t.m_centerY); f(p + 6, t.m_cosYaw); f(p + 7, t.m_sinYaw);
                f(p + 8, t.m_inverseScale); f(p + 9, -t.m_halfWidth); f(p + 10, -t.m_halfDepth);
                f(p + 11, t.m_halfWidth); f(p + 12, t.m_halfDepth); f(p + 13, s.m_heightOrigin); f(p + 14, s.m_heightRange);
                f(p + 15, s.m_strength); f(p + 16, s.m_feather); f(p + 17, s.m_featherExponent); f(p + 18, t.m_edgeInset);
                words[p + 19] = floats(s.m_reconstruction ? s.m_reconstruction->m_samples : d.m_samples);
            }
        }
        if (index != count)
        {
            words.resize(start);
            words[header + 5] = 0;
            return false;
        }
        return true;
    }
}
