#include <TerrainCompositor/SurfaceStampSampling.h>

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

        double Smooth(double value)
        {
            value = std::clamp(value, 0.0, 1.0);
            return value * value * (3.0 - 2.0 * value);
        }

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
            return image && image->m_width > 0 && image->m_height > 0 &&
                size_t(image->m_height) <= AZStd::numeric_limits<size_t>::max() / image->m_width &&
                image->m_rawSamples.size() == size_t(image->m_width) * image->m_height;
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

        void AddDecodedPixel(const PreparedSurfaceStamp& stamp, const PreparedSurfacePalette& palette,
            size_t index, double spatialWeight, AZStd::vector<AccumulatedWeight>& sampled, double& sampleMass)
        {
            const AZ::u16 idA = stamp.m_surfaceIdA->m_rawSamples[index];
            if (!stamp.m_surfaceIdB)
            {
                if (idA != 0)
                {
                    const auto* mapped = FindPaletteEntry(palette, idA);
                    AZ_Assert(mapped, "Prepared surface stamp contains an unresolved ID A value.");
                    if (mapped)
                    {
                        AddWeight(sampled, mapped->m_surfaceTag, spatialWeight);
                        sampleMass += spatialWeight;
                    }
                }
                return;
            }

            const AZ::u16 idB = stamp.m_surfaceIdB->m_rawSamples[index];
            const double blend = double(stamp.m_surfaceBlend->m_rawSamples[index]) / 65535.0;
            const double weightA = spatialWeight * (1.0 - blend);
            const double weightB = spatialWeight * blend;
            if (idA != 0 && weightA > 0.0)
            {
                const auto* mapped = FindPaletteEntry(palette, idA);
                AZ_Assert(mapped, "Prepared surface stamp contains an unresolved ID A value.");
                if (mapped)
                {
                    AddWeight(sampled, mapped->m_surfaceTag, weightA);
                    sampleMass += weightA;
                }
            }
            if (idB != 0 && weightB > 0.0)
            {
                const auto* mapped = FindPaletteEntry(palette, idB);
                AZ_Assert(mapped, "Prepared surface stamp contains an unresolved ID B value.");
                if (mapped)
                {
                    AddWeight(sampled, mapped->m_surfaceTag, weightB);
                    sampleMass += weightB;
                }
            }
        }

        void SampleSurface(const PreparedSurfaceStamp& stamp, const PreparedSurfacePalette& palette,
            double u, double v, AZStd::vector<AccumulatedWeight>& sampled, double& sampleMass)
        {
            const auto& image = *stamp.m_surfaceIdA;
            const double pixelX = std::clamp(u, 0.0, 1.0) * (image.m_width - 1);
            const double pixelY = std::clamp(1.0 - v, 0.0, 1.0) * (image.m_height - 1);
            const size_t x0 = static_cast<size_t>(pixelX);
            const size_t y0 = static_cast<size_t>(pixelY);
            const size_t x1 = AZStd::min(x0 + 1, size_t(image.m_width - 1));
            const size_t y1 = AZStd::min(y0 + 1, size_t(image.m_height - 1));
            const double tx = pixelX - double(x0);
            const double ty = pixelY - double(y0);
            const size_t row0 = y0 * size_t(image.m_width);
            const size_t row1 = y1 * size_t(image.m_width);

            // Fixed neighbor order keeps scalar and batch accumulation bit-identical.
            AddDecodedPixel(stamp, palette, row0 + x0, (1.0 - tx) * (1.0 - ty), sampled, sampleMass);
            AddDecodedPixel(stamp, palette, row0 + x1, tx * (1.0 - ty), sampled, sampleMass);
            AddDecodedPixel(stamp, palette, row1 + x0, (1.0 - tx) * ty, sampled, sampleMass);
            AddDecodedPixel(stamp, palette, row1 + x1, tx * ty, sampled, sampleMass);
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
                    Smooth((dx - edgeInset) / stamp.m_feather) *
                    Smooth((dy - edgeInset) / stamp.m_feather), 0.0, 1.0);
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
