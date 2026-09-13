#include <TerrainCompositor/HeightmapStampSampling.h>
#include "StampMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace TerrainCompositor
{
    namespace
    {
        constexpr float MaximumReconstructionRadius = 8.0f;

        double CubicBSpline(double value)
        {
            value = std::abs(value);
            if (value < 1.0)
            {
                return (4.0 + value * value * (3.0 * value - 6.0)) / 6.0;
            }
            if (value < 2.0)
            {
                const double remainder = 2.0 - value;
                return remainder * remainder * remainder / 6.0;
            }
            return 0.0;
        }

        double SampleBilinear(const float* samples, AZ::u32 width, AZ::u32 height, double u, double v)
        {
            // Preparation checked nonzero dimensions and the complete row-major buffer size.
            // A one-pixel dimension gives coordinate 0 and identical neighbors, without division.
            const double pixelX = std::clamp(u, 0.0, 1.0) * (width - 1);
            const double pixelY = std::clamp(1.0 - v, 0.0, 1.0) * (height - 1);
            const size_t x0 = static_cast<size_t>(pixelX);
            const size_t y0 = static_cast<size_t>(pixelY);
            const size_t x1 = std::min(x0 + 1, size_t(width - 1));
            const size_t y1 = std::min(y0 + 1, size_t(height - 1));
            const double tx = pixelX - static_cast<double>(x0);
            const double ty = pixelY - static_cast<double>(y0);
            const size_t row0 = y0 * size_t(width);
            const size_t row1 = y1 * size_t(width);
            const double top = samples[row0 + x0] * (1.0 - tx) + samples[row0 + x1] * tx;
            const double bottom = samples[row1 + x0] * (1.0 - tx) + samples[row1 + x1] * tx;
            return top * (1.0 - ty) + bottom * ty;
        }

        double SampleHeightmap(const PreparedHeightmapStamp& stamp, double u, double v)
        {
            if (stamp.m_reconstruction)
            {
                return SampleBilinear(
                    stamp.m_reconstruction->m_samples.data(), stamp.m_reconstruction->m_width, stamp.m_reconstruction->m_height, u, v);
            }
            return SampleBilinear(stamp.m_image->m_samples.data(), stamp.m_image->m_width, stamp.m_image->m_height, u, v);
        }

        struct ContributionSample
        {
            double m_displacement = 0.0;
            double m_target = 0.0;
            double m_heightOrigin = 0.0;
            double m_weight = 0.0;
            double m_replaceBlend = 1.0;
            bool m_relativeEdgeBlend = true;
        };

        bool TrySampleImageContribution(const AZ::Vector3& position, const PreparedHeightmapStamp& stamp, ContributionSample& result)
        {
            if (!stamp.m_image || stamp.m_strength == 0.0)
            {
                return false;
            }
            MappedStampPosition mapped;
            if (!TryMapPositionToStamp(position, stamp.m_placement, mapped))
            {
                return false;
            }
            const double dx = mapped.m_edgeDistanceX;
            const double dy = mapped.m_edgeDistanceY;
            const double edgeInset = stamp.m_placement.m_edgeInset;
            ContributionSample sample;
            sample.m_weight = stamp.m_strength;
            if (stamp.m_feather > 0.0)
            {
                const double featherX = Internal::SmoothStep01((dx - edgeInset) / stamp.m_feather);
                const double featherY = Internal::SmoothStep01((dy - edgeInset) / stamp.m_feather);
                sample.m_replaceBlend = std::clamp(featherX * featherY, 0.0, 1.0);
                sample.m_weight = stamp.m_featherExponent == 1.0
                    ? stamp.m_strength * featherX * featherY
                    : ((sample.m_replaceBlend == 0.0 || sample.m_replaceBlend == 1.0)
                           ? stamp.m_strength * sample.m_replaceBlend
                           : stamp.m_strength * std::pow(sample.m_replaceBlend, stamp.m_featherExponent));
            }
            if (sample.m_weight == 0.0)
            {
                return false;
            }
            sample.m_displacement = stamp.m_heightRange * SampleHeightmap(stamp, mapped.m_u, mapped.m_v);
            sample.m_target = stamp.m_heightOrigin + sample.m_displacement;
            sample.m_heightOrigin = stamp.m_heightOrigin;
            sample.m_relativeEdgeBlend = stamp.m_relativeEdgeBlend;
            result = sample;
            return true;
        }

        template<class Record, class SampleFunction>
        float ComposeContributions(
            const AZ::Vector3& position,
            float baseValue,
            const HeightmapRegionMapping& mapping,
            AZStd::span<const Record> records,
            SampleFunction&& sampleFunction)
        {
            const double minZ = mapping.m_minZ;
            const double range = mapping.m_range;
            if (records.empty() || !std::isfinite(minZ) || !std::isfinite(range) || range <= 0.0 || !std::isfinite(position.GetX()) ||
                !std::isfinite(position.GetY()))
            {
                return baseValue;
            }
            bool contributed = false;
            double height = 0.0;
            for (const auto& record : records)
            {
                ContributionSample sample;
                if (!sampleFunction(record, sample))
                {
                    continue;
                }
                if (!contributed)
                {
                    height = minZ + double(baseValue) * range;
                    contributed = true;
                }
                if (sample.m_relativeEdgeBlend && sample.m_replaceBlend < 1.0)
                {
                    height += sample.m_weight * (sample.m_displacement + sample.m_replaceBlend * (sample.m_heightOrigin - height));
                }
                else
                {
                    height = height * (1.0 - sample.m_weight) + sample.m_target * sample.m_weight;
                }
            }
            if (!contributed)
            {
                return baseValue;
            }
            const double gradient = (height - minZ) / range;
            if (!std::isfinite(gradient) || std::abs(gradient) > std::numeric_limits<float>::max())
            {
                return baseValue;
            }
            return static_cast<float>(gradient);
        }
    } // namespace

    HeightmapReconstructionDataPtr CreateHeightmapReconstruction(const HeightmapData& image, float radius)
    {
        if (!std::isfinite(radius) || radius <= 0.0f || radius > MaximumReconstructionRadius || image.m_width == 0 || image.m_height == 0 ||
            image.m_samples.size() != size_t(image.m_width) * image.m_height)
        {
            return {};
        }

        const int support = std::max(1, static_cast<int>(std::ceil(2.0 * double(radius))));
        AZStd::vector<float> intermediate(image.m_samples.size());
        auto filtered = AZStd::make_shared<HeightmapReconstructionData>();
        filtered->m_width = image.m_width;
        filtered->m_height = image.m_height;
        filtered->m_samples.resize(image.m_samples.size());

        const auto filterLine = [radius, support](const auto& read, const auto& write, size_t count)
        {
            for (size_t destination = 0; destination < count; ++destination)
            {
                double weighted = 0.0;
                double totalWeight = 0.0;
                float minimum = std::numeric_limits<float>::max();
                float maximum = -std::numeric_limits<float>::max();
                for (int offset = -support; offset <= support; ++offset)
                {
                    const double weight = CubicBSpline(double(offset) / double(radius));
                    if (weight == 0.0)
                    {
                        continue;
                    }
                    const size_t source = static_cast<size_t>(
                        std::clamp(static_cast<long long>(destination) + offset, 0LL, static_cast<long long>(count - 1)));
                    const float sample = read(source);
                    weighted += double(sample) * weight;
                    totalWeight += weight;
                    minimum = std::min(minimum, sample);
                    maximum = std::max(maximum, sample);
                }
                write(
                    destination,
                    minimum == maximum ? minimum
                                       : static_cast<float>(std::clamp(weighted / totalWeight, double(minimum), double(maximum))));
            }
        };

        for (size_t y = 0; y < image.m_height; ++y)
        {
            filterLine(
                [&](size_t x)
                {
                    return image.m_samples[y * size_t(image.m_width) + x];
                },
                [&](size_t x, float value)
                {
                    intermediate[y * size_t(image.m_width) + x] = value;
                },
                image.m_width);
        }
        for (size_t x = 0; x < image.m_width; ++x)
        {
            filterLine(
                [&](size_t y)
                {
                    return intermediate[y * size_t(image.m_width) + x];
                },
                [&](size_t y, float value)
                {
                    filtered->m_samples[y * size_t(image.m_width) + x] = value;
                },
                image.m_height);
        }
        return filtered;
    }

    HeightmapStampValidation PrepareHeightmapStamp(
        const HeightmapStampRegistrationData& registration, bool hasNonUniformScale, PreparedHeightmapStamp& result)
    {
        result = {};
        const auto& config = registration.m_configuration;
        PreparedStampPlacement placement;
        const auto placementValidation = PrepareStampPlacement(registration, hasNonUniformScale, placement);
        if (placementValidation != HeightmapStampValidation::Valid)
        {
            return placementValidation;
        }
        if (!std::isfinite(config.m_heightScale) || config.m_heightScale < 0.0f)
        {
            return HeightmapStampValidation::HeightScale;
        }
        if (!std::isfinite(config.m_verticalOffset))
        {
            return HeightmapStampValidation::Offset;
        }
        if (!std::isfinite(config.m_strength) || config.m_strength < 0.0f || config.m_strength > 1.0f)
        {
            return HeightmapStampValidation::Strength;
        }
        if (!std::isfinite(config.m_featherExponent) || config.m_featherExponent < 1.0f)
        {
            return HeightmapStampValidation::FeatherExponent;
        }
        if (config.m_samplingMode != HeightmapSamplingMode::Bilinear && config.m_samplingMode != HeightmapSamplingMode::SmoothCubic)
        {
            return HeightmapStampValidation::SamplingMode;
        }
        if (!std::isfinite(config.m_reconstructionRadius) || config.m_reconstructionRadius < 0.0f ||
            config.m_reconstructionRadius > MaximumReconstructionRadius)
        {
            return HeightmapStampValidation::ReconstructionRadius;
        }
        const double halfSmallerDimension = std::min(placement.m_halfWidth, placement.m_halfDepth);
        if (!std::isfinite(config.m_featherWidth) || config.m_featherWidth < 0.0f ||
            double(config.m_featherWidth) > halfSmallerDimension - placement.m_edgeInset)
        {
            return HeightmapStampValidation::Feather;
        }
        const auto& transform = registration.m_worldTransform;
        const double scale = transform.GetUniformScale();
        PreparedHeightmapStamp prepared;
        prepared.m_placement = AZStd::move(placement);
        prepared.m_heightOrigin = double(transform.GetTranslation().GetZ()) + scale * double(config.m_verticalOffset);
        prepared.m_heightRange = scale * double(config.m_heightScale);
        prepared.m_strength = config.m_strength;
        prepared.m_feather = config.m_featherWidth;
        prepared.m_featherExponent = config.m_featherExponent;
        prepared.m_relativeEdgeBlend = config.m_relativeEdgeBlend;
        prepared.m_imageRevision = registration.m_heightmap.m_revision;

        const double floatMax = std::numeric_limits<float>::max();
        if (std::abs(prepared.m_heightOrigin) > floatMax || std::abs(prepared.m_heightOrigin + prepared.m_heightRange) > floatMax)
        {
            return HeightmapStampValidation::Bounds;
        }

        if (registration.m_heightmap.m_status == HeightmapDataStatus::Ready && registration.m_heightmap.m_data)
        {
            const auto& image = *registration.m_heightmap.m_data;
            if (image.m_width == 0 || image.m_height == 0 || size_t(image.m_height) > std::numeric_limits<size_t>::max() / image.m_width ||
                image.m_samples.size() != size_t(image.m_width) * image.m_height)
            {
                return HeightmapStampValidation::ImageData;
            }
            prepared.m_image = registration.m_heightmap.m_data;
        }
        result = AZStd::move(prepared);
        return HeightmapStampValidation::Valid;
    }

    const char* GetHeightmapStampValidationMessage(HeightmapStampValidation validation)
    {
        switch (validation)
        {
        case HeightmapStampValidation::Valid:
            return "Valid";
        case HeightmapStampValidation::Width:
            return "Footprint Width must be finite and positive (local meters).";
        case HeightmapStampValidation::Depth:
            return "Footprint Depth must be finite and positive (local meters).";
        case HeightmapStampValidation::HeightScale:
            return "Height Scale must be finite and nonnegative (local meters).";
        case HeightmapStampValidation::Offset:
            return "Vertical Offset must be finite (signed local meters).";
        case HeightmapStampValidation::Strength:
            return "Strength must be finite and between 0 and 1.";
        case HeightmapStampValidation::Feather:
            return "Edge Feather Width must be finite and nonnegative; width plus Edge Inset cannot exceed half the smaller footprint "
                   "dimension.";
        case HeightmapStampValidation::FeatherExponent:
            return "Edge Feather Exponent must be finite and at least 1.";
        case HeightmapStampValidation::EdgeInset:
            return "Edge Inset must be finite and between 0 and half the smaller footprint dimension; inset plus feather width must fit "
                   "inside that half dimension.";
        case HeightmapStampValidation::MissingTransform:
            return "An active Transform component is required.";
        case HeightmapStampValidation::NonUniformScale:
            return "Remove Non-uniform Scale; stamps support only Transform's positive uniform scale.";
        case HeightmapStampValidation::NonFiniteTransform:
            return "World translation, rotation, and scale must be finite.";
        case HeightmapStampValidation::UniformScale:
            return "Effective world uniform scale must be positive.";
        case HeightmapStampValidation::Rotation:
            return "Effective world rotation must be yaw-only; remove pitch/roll from the stamp or its parents.";
        case HeightmapStampValidation::Bounds:
            return "World footprint or target heights exceed the representable range; reduce placement, dimensions, or scale.";
        case HeightmapStampValidation::ImageData:
            return "Ready height data has invalid dimensions or sample count; reprocess the height image.";
        case HeightmapStampValidation::SamplingMode:
            return "Sampling Mode is unsupported; select Bilinear or Smooth Cubic.";
        case HeightmapStampValidation::ReconstructionRadius:
            return "Reconstruction Radius must be finite and between 0 and 8 source texels.";
        }
        return "Unsupported stamp configuration.";
    }

    HeightmapRegionMapping PrepareHeightmapRegionMapping(const AZ::Aabb& bounds)
    {
        if (!bounds.IsValid() || !bounds.GetMin().IsFinite() || !bounds.GetMax().IsFinite())
        {
            return {};
        }
        return { bounds.GetMin().GetZ(), double(bounds.GetMax().GetZ()) - bounds.GetMin().GetZ() };
    }

    float ComposeHeightmapStamps(
        const AZ::Vector3& position, float baseValue, const AZ::Aabb& regionBounds, AZStd::span<const PreparedHeightmapStamp> stamps)
    {
        return ComposeHeightmapStamps(position, baseValue, PrepareHeightmapRegionMapping(regionBounds), stamps);
    }

    float ComposeHeightmapStamps(
        const AZ::Vector3& position,
        float baseValue,
        const HeightmapRegionMapping& mapping,
        AZStd::span<const PreparedHeightmapStamp> stamps)
    {
        return ComposeContributions(
            position,
            baseValue,
            mapping,
            stamps,
            [&position](const PreparedHeightmapStamp& stamp, ContributionSample& sample)
            {
                return TrySampleImageContribution(position, stamp, sample);
            });
    }

    float ComposeHeightContributors(
        const AZ::Vector3& position,
        float baseValue,
        const HeightmapRegionMapping& mapping,
        AZStd::span<const PreparedHeightContributor> contributors)
    {
        return ComposeContributions(
            position,
            baseValue,
            mapping,
            contributors,
            [&position](const PreparedHeightContributor& contributor, ContributionSample& sample)
            {
                if (contributor.m_type == PreparedHeightContributor::Type::Image)
                {
                    return TrySampleImageContribution(position, contributor.m_image, sample);
                }
                TerrainMeshHeightContribution meshSample;
                if (!SampleTerrainMeshHeightStamp(position, contributor.m_mesh, meshSample))
                {
                    return false;
                }
                sample.m_displacement = meshSample.m_displacement;
                sample.m_target = meshSample.m_targetHeight;
                sample.m_heightOrigin = contributor.m_mesh.m_heightOrigin;
                sample.m_weight = meshSample.m_weight;
                sample.m_replaceBlend = meshSample.m_replaceBlend;
                sample.m_relativeEdgeBlend = contributor.m_mesh.m_relativeEdgeBlend;
                return true;
            });
    }

    float ComposeHeightContributors(const AZ::Vector3& position, float baseValue,
        const HeightmapRegionMapping& mapping, AZStd::span<const PreparedHeightContributor* const> contributors)
    {
        return ComposeContributions(position, baseValue, mapping, contributors,
            [&position](const PreparedHeightContributor* selected, ContributionSample& sample)
            {
                const auto& contributor = *selected;
                if (contributor.m_type == PreparedHeightContributor::Type::Image)
                {
                    return TrySampleImageContribution(position, contributor.m_image, sample);
                }
                TerrainMeshHeightContribution meshSample;
                if (!SampleTerrainMeshHeightStamp(position, contributor.m_mesh, meshSample))
                {
                    return false;
                }
                sample.m_displacement = meshSample.m_displacement;
                sample.m_target = meshSample.m_targetHeight;
                sample.m_heightOrigin = contributor.m_mesh.m_heightOrigin;
                sample.m_weight = meshSample.m_weight;
                sample.m_replaceBlend = meshSample.m_replaceBlend;
                sample.m_relativeEdgeBlend = contributor.m_mesh.m_relativeEdgeBlend;
                return true;
            });
    }
} // namespace TerrainCompositor
