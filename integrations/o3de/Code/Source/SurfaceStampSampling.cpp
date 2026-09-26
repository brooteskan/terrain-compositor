#include <TerrainCompositor/SurfaceStampSampling.h>
#include "StampMath.h"
#include "ImageSampling.h"
#include <TerrainCompositor/TerrainMeshHeightMapping.h>

#include <AzCore/std/algorithm.h>
#include <AzCore/std/limits.h>
#include <algorithm>
#include <cmath>

namespace TerrainCompositor
{
    namespace
    {
        struct AccumulatedWeight
        {
            AZ::Crc32 m_tag;
            double m_weight = 0.0;
        };

        const PreparedSurfacePaletteEntry* FindPaletteEntry(const PreparedSurfacePalette& palette, AZ::u16 exportedId)
        {
            const auto found = AZStd::lower_bound(palette.m_entries.begin(), palette.m_entries.end(), exportedId,
                [](const PreparedSurfacePaletteEntry& entry, AZ::u16 id)
                {
                    return entry.m_exportedId < id;
                });
            return found != palette.m_entries.end() && found->m_exportedId == exportedId ? &*found : nullptr;
        }

        void AddWeight(AZStd::vector<AccumulatedWeight>& weights, AZ::Crc32 tag, double weight)
        {
            if (!(weight > 0.0) || !std::isfinite(weight))
            {
                return;
            }
            for (auto& entry : weights)
            {
                if (entry.m_tag == tag)
                {
                    entry.m_weight += weight;
                    return;
                }
            }
            weights.push_back({ tag, weight });
        }

        bool HasValidRawImage(const HeightmapDataPtr& image)
        {
            return image && Internal::HasCompleteImageBuffer(image->m_width, image->m_height, image->m_rawSamples.size());
        }

        bool AllIdsResolve(const HeightmapData& image, const PreparedSurfacePalette& palette)
        {
            for (const AZ::u16 id : image.m_uniqueNonzeroValues)
            {
                if (!FindPaletteEntry(palette, id))
                {
                    return false;
                }
            }
            return true;
        }

        void AddDecodedId(const PreparedSurfacePalette& palette, AZ::u16 id, double weight,
            const char* diagnostic, AZStd::vector<AccumulatedWeight>& sampled, double& sampleMass)
        {
            if (id == 0) return;
            const auto* mapped = FindPaletteEntry(palette, id);
            AZ_Assert(mapped, "%s", diagnostic);
            if (mapped)
            {
                AddWeight(sampled, mapped->m_surfaceTag, weight);
                sampleMass += weight;
            }
        }

        void AddDecodedPixel(const PreparedSurfaceStamp& stamp, const PreparedSurfacePalette& palette,
            size_t index, double spatialWeight, AZStd::vector<AccumulatedWeight>& sampled, double& sampleMass)
        {
            const AZ::u16 idA = stamp.m_surfaceIdA->m_rawSamples[index];
            constexpr const char* InvalidA = "Prepared surface stamp contains an unresolved ID A value.";
            constexpr const char* InvalidB = "Prepared surface stamp contains an unresolved ID B value.";
            if (!stamp.m_surfaceIdB)
            {
                AddDecodedId(palette, idA, spatialWeight, InvalidA, sampled, sampleMass);
                return;
            }
            const AZ::u16 idB = stamp.m_surfaceIdB->m_rawSamples[index];
            const double blend = double(stamp.m_surfaceBlend->m_rawSamples[index]) / 65535.0;
            const double weightA = spatialWeight * (1.0 - blend);
            const double weightB = spatialWeight * blend;
            if (weightA > 0.0) AddDecodedId(palette, idA, weightA, InvalidA, sampled, sampleMass);
            if (weightB > 0.0) AddDecodedId(palette, idB, weightB, InvalidB, sampled, sampleMass);
        }

        void SampleSurface(const PreparedSurfaceStamp& stamp, const PreparedSurfacePalette& palette,
            double u, double v, AZStd::vector<AccumulatedWeight>& sampled, double& sampleMass)
        {
            const auto& image = *stamp.m_surfaceIdA;
            const auto c = Internal::GetBilinearCoordinates(image.m_width, image.m_height, u, v);

            // Fixed neighbor order keeps scalar and batch accumulation bit-identical.
            AddDecodedPixel(stamp, palette, c.m_topLeft, (1.0 - c.m_tx) * (1.0 - c.m_ty), sampled, sampleMass);
            AddDecodedPixel(stamp, palette, c.m_topRight, c.m_tx * (1.0 - c.m_ty), sampled, sampleMass);
            AddDecodedPixel(stamp, palette, c.m_bottomLeft, (1.0 - c.m_tx) * c.m_ty, sampled, sampleMass);
            AddDecodedPixel(stamp, palette, c.m_bottomRight, c.m_tx * c.m_ty, sampled, sampleMass);
        }
    } // namespace

    PreparedSurfacePalette PrepareSurfacePalette(
        AZStd::span<const SurfacePaletteEntry> entries, AZStd::span<const SurfaceBaseWeight> baseWeights)
    {
        PreparedSurfacePalette result;
        result.m_entries.reserve(entries.size());
        for (const auto& entry : entries)
        {
            const AZ::Crc32 tag = entry.m_surfaceTag;
            if (entry.m_exportedId == 0)
            {
                result.m_error = "Surface palette ID zero is reserved for transparency.";
                return result;
            }
            if (tag == AzFramework::SurfaceData::Constants::UnassignedTagCrc)
            {
                result.m_error = AZStd::string::format("Surface palette ID %u has an unassigned surface tag.", entry.m_exportedId);
                return result;
            }
            result.m_entries.push_back({ entry.m_exportedId, tag });
        }
        AZStd::sort(result.m_entries.begin(), result.m_entries.end(), [](const auto& left, const auto& right)
        {
            return left.m_exportedId < right.m_exportedId;
        });
        for (size_t index = 1; index < result.m_entries.size(); ++index)
        {
            if (result.m_entries[index - 1].m_exportedId == result.m_entries[index].m_exportedId)
            {
                result.m_error = AZStd::string::format(
                    "Surface palette ID %u is defined more than once.", result.m_entries[index].m_exportedId);
                return result;
            }
        }

        AZStd::vector<AccumulatedWeight> base;
        double total = 0.0;
        for (const auto& weight : baseWeights)
        {
            if (!std::isfinite(weight.m_weight))
            {
                result.m_error = "Base surface weights must be finite.";
                return result;
            }
            if (!(weight.m_weight > 0.0f))
            {
                continue;
            }
            const auto* mapped = FindPaletteEntry(result, weight.m_exportedId);
            if (!mapped)
            {
                result.m_error = AZStd::string::format(
                    "Base surface ID %u is missing from the surface palette.", weight.m_exportedId);
                return result;
            }
            AddWeight(base, mapped->m_surfaceTag, weight.m_weight);
            total += weight.m_weight;
        }
        if (total > 0.0)
        {
            AZStd::sort(base.begin(), base.end(), [](const auto& left, const auto& right)
            {
                return static_cast<AZ::u32>(left.m_tag) > static_cast<AZ::u32>(right.m_tag);
            });
            result.m_baseWeights.reserve(base.size());
            for (const auto& entry : base)
            {
                result.m_baseWeights.emplace_back(entry.m_tag, static_cast<float>(entry.m_weight / total));
            }
        }
        return result;
    }

    bool SurfacePalettesEqual(const PreparedSurfacePalette& left, const PreparedSurfacePalette& right)
    {
        if (left.m_error != right.m_error || left.m_entries.size() != right.m_entries.size() ||
            left.m_baseWeights.size() != right.m_baseWeights.size())
        {
            return false;
        }
        for (size_t index = 0; index < left.m_entries.size(); ++index)
        {
            if (left.m_entries[index].m_exportedId != right.m_entries[index].m_exportedId ||
                left.m_entries[index].m_surfaceTag != right.m_entries[index].m_surfaceTag)
            {
                return false;
            }
        }
        for (size_t index = 0; index < left.m_baseWeights.size(); ++index)
        {
            if (left.m_baseWeights[index] != right.m_baseWeights[index])
            {
                return false;
            }
        }
        return true;
    }

    SurfaceStampValidation PrepareSurfaceStamp(const HeightmapStampRegistrationData& registration,
        bool hasNonUniformScale, const PreparedSurfacePalette& palette, PreparedSurfaceStamp& result,
        HeightmapStampValidation* placementValidation)
    {
        result = {};
        const auto& maps = registration.m_configuration.m_surfaceMaps;
        const bool hasA = maps.m_surfaceIdAAsset.GetId().IsValid();
        const bool hasB = maps.m_surfaceIdBAsset.GetId().IsValid();
        const bool hasBlend = maps.m_blendMaskAsset.GetId().IsValid();
        if (!hasA && !hasB && !hasBlend)
        {
            return SurfaceStampValidation::Absent;
        }
        if (!hasA || hasB != hasBlend)
        {
            return SurfaceStampValidation::IncompletePair;
        }

        PreparedStampPlacement placement;
        auto placementResult = PrepareStampPlacement(registration, hasNonUniformScale, placement);
        const auto& config = registration.m_configuration;
        if (placementResult == HeightmapStampValidation::Valid &&
            (!std::isfinite(config.m_strength) || config.m_strength < 0.0f || config.m_strength > 1.0f))
        {
            placementResult = HeightmapStampValidation::Strength;
        }
        if (placementResult == HeightmapStampValidation::Valid &&
            (!std::isfinite(config.m_featherExponent) || config.m_featherExponent < 1.0f))
        {
            placementResult = HeightmapStampValidation::FeatherExponent;
        }
        if (placementResult == HeightmapStampValidation::Valid &&
            (!std::isfinite(config.m_featherWidth) || config.m_featherWidth < 0.0f ||
                double(config.m_featherWidth) >
                    std::min(placement.m_halfWidth, placement.m_halfDepth) - placement.m_edgeInset))
        {
            placementResult = HeightmapStampValidation::Feather;
        }
        if (placementValidation)
        {
            *placementValidation = placementResult;
        }
        if (placementResult != HeightmapStampValidation::Valid)
        {
            return SurfaceStampValidation::Placement;
        }
        if (!palette.IsValid())
        {
            return SurfaceStampValidation::Palette;
        }

        if (registration.m_surfaceIdA.m_status != HeightmapDataStatus::Ready || !registration.m_surfaceIdA.m_data ||
            (hasB && (registration.m_surfaceIdB.m_status != HeightmapDataStatus::Ready || !registration.m_surfaceIdB.m_data ||
                registration.m_surfaceBlend.m_status != HeightmapDataStatus::Ready || !registration.m_surfaceBlend.m_data)))
        {
            return SurfaceStampValidation::DataUnavailable;
        }
        if (!HasValidRawImage(registration.m_surfaceIdA.m_data) ||
            (hasB && (!HasValidRawImage(registration.m_surfaceIdB.m_data) || !HasValidRawImage(registration.m_surfaceBlend.m_data))))
        {
            return SurfaceStampValidation::ImageData;
        }
        const auto& imageA = *registration.m_surfaceIdA.m_data;
        if (hasB && (registration.m_surfaceIdB.m_data->m_width != imageA.m_width ||
            registration.m_surfaceIdB.m_data->m_height != imageA.m_height ||
            registration.m_surfaceBlend.m_data->m_width != imageA.m_width ||
            registration.m_surfaceBlend.m_data->m_height != imageA.m_height))
        {
            return SurfaceStampValidation::DimensionMismatch;
        }
        if (!AllIdsResolve(imageA, palette) ||
            (hasB && !AllIdsResolve(*registration.m_surfaceIdB.m_data, palette)))
        {
            return SurfaceStampValidation::UnknownId;
        }

        result.m_placement = AZStd::move(placement);
        result.m_surfaceIdA = registration.m_surfaceIdA.m_data;
        result.m_surfaceIdB = hasB ? registration.m_surfaceIdB.m_data : HeightmapDataPtr{};
        result.m_surfaceBlend = hasB ? registration.m_surfaceBlend.m_data : HeightmapDataPtr{};
        result.m_strength = config.m_strength;
        result.m_feather = config.m_featherWidth;
        result.m_featherExponent = config.m_featherExponent;
        return SurfaceStampValidation::Valid;
    }

    const char* GetSurfaceStampValidationMessage(SurfaceStampValidation validation)
    {
        switch (validation)
        {
        case SurfaceStampValidation::Valid: return "Valid surface map set.";
        case SurfaceStampValidation::Absent: return "No surface maps assigned; height contribution is unchanged.";
        case SurfaceStampValidation::Placement: return "Surface placement is invalid; see the stamp placement diagnostic.";
        case SurfaceStampValidation::Palette: return "The composition surface palette is invalid.";
        case SurfaceStampValidation::IncompletePair: return "Assign Surface ID A, and assign Surface ID B together with Blend Mask.";
        case SurfaceStampValidation::DataUnavailable: return "One or more selected surface images are loading, missing, failed, or unsupported.";
        case SurfaceStampValidation::ImageData: return "Surface image mip-zero dimensions or exact R16 samples are malformed.";
        case SurfaceStampValidation::DimensionMismatch: return "Surface ID A, Surface ID B, and Blend Mask must have identical dimensions.";
        case SurfaceStampValidation::UnknownId: return "A surface ID map contains a nonzero ID missing from the composition palette.";
        }
        return "Unsupported surface map configuration.";
    }

    AZStd::string PrepareMeshSurfaceStamp(const TerrainMeshHeightStampRegistrationData& registration,
        const PreparedTerrainMeshHeightStamp& mesh, const PreparedSurfacePalette& palette, PreparedSurfaceStamp& result)
    {
        result = {};
        if (registration.m_configuration.m_surfaceMappings.empty()) return {};
        if (!palette.IsValid()) return palette.m_error;
        result.m_mesh = mesh;
        result.m_placement.m_stampEntityId = mesh.m_stampEntityId;
        result.m_placement.m_worldBounds = mesh.m_worldBounds;
        result.m_placement.m_priority = mesh.m_priority;
        result.m_placement.m_stableOrderKey = mesh.m_stableOrderKey;
        result.m_meshSlotTags.resize(mesh.m_data->m_materialSlots.size());
        AZStd::string diagnostic;
        for (const auto& mapping : registration.m_configuration.m_surfaceMappings)
        {
            if (mapping.m_surfaceId == 0) continue;
            size_t matches = 0, slot = 0, mappings = 0;
            for (size_t i = 0; i < mesh.m_data->m_materialSlots.size(); ++i)
                if (mesh.m_data->m_materialSlots[i] == mapping.m_slotName) { ++matches; slot = i; }
            for (const auto& other : registration.m_configuration.m_surfaceMappings)
                if (other.m_slotName == mapping.m_slotName) ++mappings;
            const auto* entry = FindPaletteEntry(palette, mapping.m_surfaceId);
            if (matches != 1 || mappings != 1 || !entry)
                diagnostic += AZStd::string::format("Slot '%s' -> ID %u unresolved: check exact imported name, duplicate mappings and surface palette. ",
                    mapping.m_slotName.c_str(), mapping.m_surfaceId);
            else result.m_meshSlotTags[slot] = entry->m_surfaceTag;
        }
        for (size_t i = 0; i < result.m_meshSlotTags.size(); ++i)
            if (result.m_meshSlotTags[i] == AZ::Crc32{})
                diagnostic += AZStd::string::format("Slot '%s' leaves lower surfaces unchanged. ", mesh.m_data->m_materialSlots[i].c_str());
        return diagnostic;
    }

    void ComposeSurfaceStamps(const AZ::Vector3& position, const PreparedSurfacePalette& palette,
        AZStd::span<const PreparedSurfaceStamp> stamps,
        AzFramework::SurfaceData::SurfaceTagWeightList& outSurfaceWeights)
    {
        outSurfaceWeights.clear();
        if (!palette.IsValid() || !std::isfinite(position.GetX()) || !std::isfinite(position.GetY()))
        {
            return;
        }

        AZStd::vector<AccumulatedWeight> accumulated;
        accumulated.reserve(palette.m_baseWeights.size() + stamps.size() * 2);
        for (const auto& base : palette.m_baseWeights)
        {
            AddWeight(accumulated, base.m_surfaceType, base.m_weight);
        }

        for (const auto& stamp : stamps)
        {
            if (stamp.m_mesh.m_data)
            {
                TerrainMeshHeightContribution contribution;
                if (!SampleTerrainMeshHeightStamp(position, stamp.m_mesh, contribution)) continue;
                float x, y;
                const auto& mesh = stamp.m_mesh;
                MapTerrainMeshHeightXY(position.GetX(), position.GetY(), float(mesh.m_originX), float(mesh.m_originY),
                    float(mesh.m_cosYaw), float(mesh.m_sinYaw), float(mesh.m_inverseScale), x, y);
                TerrainMeshHeightCellAddress address;
                if (!ResolveTerrainMeshHeightCell(*mesh.m_data, x, y, address)) continue;
                const size_t triangle = ResolveTerrainMeshHeightTriangle(*mesh.m_data, address);
                if (triangle >= mesh.m_data->m_cellMaterialSlots.size()) continue;
                const AZ::u32 slot = mesh.m_data->m_cellMaterialSlots[triangle];
                if (slot >= stamp.m_meshSlotTags.size() || stamp.m_meshSlotTags[slot] == AZ::Crc32{}) continue;
                for (auto& entry : accumulated) entry.m_weight *= 1.0 - contribution.m_weight;
                AddWeight(accumulated, stamp.m_meshSlotTags[slot], contribution.m_weight);
                continue;
            }
            if (!stamp.m_surfaceIdA || stamp.m_strength == 0.0)
            {
                continue;
            }
            MappedStampPosition mapped;
            if (!TryMapPositionToStamp(position, stamp.m_placement, mapped))
            {
                continue;
            }
            const double dx = mapped.m_edgeDistanceX;
            const double dy = mapped.m_edgeDistanceY;
            const double edgeInset = stamp.m_placement.m_edgeInset;
            double coverage = stamp.m_strength;
            if (stamp.m_feather > 0.0)
            {
                const double mask = std::clamp(
                    Internal::SmoothStep01((dx - edgeInset) / stamp.m_feather) *
                    Internal::SmoothStep01((dy - edgeInset) / stamp.m_feather), 0.0, 1.0);
                coverage *= stamp.m_featherExponent == 1.0 || mask == 0.0 || mask == 1.0
                    ? mask : std::pow(mask, stamp.m_featherExponent);
            }
            if (!(coverage > 0.0))
            {
                continue;
            }

            AZStd::vector<AccumulatedWeight> sampled;
            sampled.reserve(8);
            double sampleMass = 0.0;
            SampleSurface(stamp, palette, mapped.m_u, mapped.m_v, sampled, sampleMass);
            sampleMass = std::clamp(sampleMass, 0.0, 1.0);
            if (!(sampleMass > 0.0))
            {
                continue;
            }

            const double preserve = 1.0 - coverage * sampleMass;
            for (auto& entry : accumulated)
            {
                entry.m_weight *= preserve;
            }
            for (const auto& entry : sampled)
            {
                AddWeight(accumulated, entry.m_tag, coverage * entry.m_weight);
            }
        }

        AZStd::erase_if(accumulated, [](const auto& entry)
        {
            return !(entry.m_weight > 0.0) || !std::isfinite(entry.m_weight);
        });
        AZStd::sort(accumulated.begin(), accumulated.end(), [](const auto& left, const auto& right)
        {
            return left.m_weight != right.m_weight ? left.m_weight > right.m_weight
                : static_cast<AZ::u32>(left.m_tag) > static_cast<AZ::u32>(right.m_tag);
        });
        if (accumulated.size() > AzFramework::SurfaceData::Constants::MaxSurfaceWeights)
        {
            accumulated.resize(AzFramework::SurfaceData::Constants::MaxSurfaceWeights);
        }
        double total = 0.0;
        for (const auto& entry : accumulated)
        {
            total += entry.m_weight;
        }
        if (!(total > 0.0) || !std::isfinite(total))
        {
            return;
        }
        for (const auto& entry : accumulated)
        {
            outSurfaceWeights.emplace_back(entry.m_tag, static_cast<float>(entry.m_weight / total));
        }
        AZStd::sort(outSurfaceWeights.begin(), outSurfaceWeights.end(),
            AzFramework::SurfaceData::SurfaceTagWeightComparator());
    }
} // namespace TerrainCompositor
