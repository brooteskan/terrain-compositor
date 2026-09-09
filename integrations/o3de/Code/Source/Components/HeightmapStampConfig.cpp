#include <TerrainCompositor/Components/HeightmapStampConfig.h>
#include <TerrainCompositor/HeightmapStampIdentity.h>

#include <AzCore/Asset/AssetSerializer.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <algorithm>
#include <cmath>

namespace TerrainCompositor
{
    namespace
    {
        float GetRemainingEdgeWidth(float width, float depth, float usedWidth)
        {
            if (!std::isfinite(width) || !std::isfinite(depth) || !std::isfinite(usedWidth))
            {
                return 0.0f;
            }
            const double remaining = std::max(0.0, 0.5 * std::min(width, depth) - std::max(0.0f, usedWidth));
            float maximum = static_cast<float>(remaining);
            // Do not let a rounded-up inspector maximum fail the sampler's double-precision validation.
            if (maximum > remaining) { maximum = std::nextafter(maximum, 0.0f); }
            return maximum;
        }
    }

    void HeightmapStampConfig::AssignNewPersistentOrderingIdentity()
    {
        m_orderingId = AZ::Uuid::CreateRandom();
        m_stableOrderKey = MakeUuidStampOrderKey(m_orderingId);
    }

    AZStd::string HeightmapStampConfig::GetRuntimeOrderKey() const
    {
        if (!m_stableOrderKey.empty())
        {
            return IsValidStampOrderKey(m_stableOrderKey) ? m_stableOrderKey : AZStd::string{};
        }
        return MakeUuidStampOrderKey(m_orderingId);
    }

    float HeightmapStampConfig::GetMaximumFeatherWidth() const
    {
        return GetRemainingEdgeWidth(m_footprintWidth, m_footprintDepth, m_edgeInset);
    }

    float HeightmapStampConfig::GetMaximumEdgeInset() const
    {
        return GetRemainingEdgeWidth(m_footprintWidth, m_footprintDepth, m_featherWidth);
    }

    void HeightmapStampConfig::Reflect(AZ::ReflectContext* context)
    {
        SurfaceMapSetConfig::Reflect(context);
        TerrainHoleMaskConfig::Reflect(context);
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            // Shared by runtime and editor descriptors; reflection-context creation need not visit them in module order.
            if (!serializeContext->IsRemovingReflection() && serializeContext->FindClassData(azrtti_typeid<HeightmapStampConfig>()))
            {
                return;
            }
            serializeContext->Enum<HeightmapSamplingMode>()
                ->Value("Bilinear", HeightmapSamplingMode::Bilinear)
                ->Value("SmoothCubic", HeightmapSamplingMode::SmoothCubic);

            serializeContext->Class<HeightmapStampConfig, AZ::ComponentConfig>()
                ->Version(7)
                ->Field("TargetCompositionEntityId", &HeightmapStampConfig::m_targetCompositionEntityId)
                ->Field("HeightmapAsset", &HeightmapStampConfig::m_heightmapAsset)
                ->Field("SurfaceMaps", &HeightmapStampConfig::m_surfaceMaps)
                ->Field("HoleMask", &HeightmapStampConfig::m_holeMask)
                ->Field("FootprintWidth", &HeightmapStampConfig::m_footprintWidth)
                ->Field("FootprintDepth", &HeightmapStampConfig::m_footprintDepth)
                ->Field("HeightScale", &HeightmapStampConfig::m_heightScale)
                ->Field("VerticalOffset", &HeightmapStampConfig::m_verticalOffset)
                ->Field("SamplingMode", &HeightmapStampConfig::m_samplingMode)
                ->Field("ReconstructionRadius", &HeightmapStampConfig::m_reconstructionRadius)
                ->Field("Strength", &HeightmapStampConfig::m_strength)
                ->Field("FeatherWidth", &HeightmapStampConfig::m_featherWidth)
                ->Field("FeatherExponent", &HeightmapStampConfig::m_featherExponent)
                ->Field("EdgeInset", &HeightmapStampConfig::m_edgeInset)
                ->Field("RelativeEdgeBlend", &HeightmapStampConfig::m_relativeEdgeBlend)
                ->Field("Priority", &HeightmapStampConfig::m_priority)
                ->Field("OrderingId", &HeightmapStampConfig::m_orderingId)
                ->Field("StableOrderKey", &HeightmapStampConfig::m_stableOrderKey);

            if (AZ::EditContext* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<HeightmapStampConfig>("Heightmap Stamp Configuration", "A centered, yaw-only heightmap stamp.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampConfig::m_targetCompositionEntityId,
                        "Target Composition", "Composition entity in this stamp's entity context.")
                    ->Attribute(AZ::Edit::Attributes::RequiredService, AZ_CRC_CE("TerrainCompositionService"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampConfig::m_heightmapAsset,
                        "Heightmap Asset", "16-bit grayscale TIFF with GSI16, Linear source/destination, R16, and resolution reduction 0. Missing or loading assets contribute nothing.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampConfig::m_surfaceMaps,
                        "Surface Maps", "Optional categorical surface data. Invalid or unavailable surface inputs never disable height contribution.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampConfig::m_holeMask,
                        "Terrain Hole Mask", "Optional explicit terrain-existence data, transformed with this stamp and independent of height strength.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampConfig::m_footprintWidth,
                        "Footprint Width", "Positive centered local-meter width. Image left/right maps to local -X/+X; no tiling.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.001f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampConfig::m_footprintDepth,
                        "Footprint Depth", "Positive centered local-meter depth. Image top/bottom maps to local +Y/-Y; no tiling.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.001f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampConfig::m_heightScale,
                        "Height Scale", "Nonnegative local elevation range represented by normalized image samples.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampConfig::m_verticalOffset,
                        "Vertical Offset", "Signed local elevation offset relative to the entity origin. With Relative Edge Blend, the absolute baseline fades in across the feather band.")
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &HeightmapStampConfig::m_samplingMode,
                        "Sampling Mode", "Bilinear preserves existing output. Smooth Cubic prefilters the source with a bounded cubic B-spline before sampling.")
                    ->EnumAttribute(HeightmapSamplingMode::Bilinear, "Bilinear")
                    ->EnumAttribute(HeightmapSamplingMode::SmoothCubic, "Smooth Cubic")
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &HeightmapStampConfig::m_reconstructionRadius,
                        "Reconstruction Radius", "Cubic B-spline scale in source texels. Zero leaves the source unchanged; 1 to 2 texels is a useful terrace-smoothing range.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, 8.0f)
                    ->Attribute(AZ::Edit::Attributes::SoftMax, 2.0f)
                    ->Attribute(AZ::Edit::Attributes::Step, 0.25f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " texels")
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &HeightmapStampConfig::m_strength,
                        "Strength", "Stamp blend weight. Zero leaves the accumulated lower layers unchanged; the interior uses Replace.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampConfig::m_featherWidth,
                        "Edge Feather Width", "Local transition width after Edge Inset. Width plus inset cannot exceed half the smaller footprint dimension. Zero is a hard edge.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, &HeightmapStampConfig::GetMaximumFeatherWidth)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampConfig::m_featherExponent,
                        "Edge Feather Exponent", "Finite exponent >= 1 applied to the feather mask. One preserves the original curve; larger values suppress the outer band and move the transition inward. Interior Strength is unchanged.")
                    ->Attribute(AZ::Edit::Attributes::Min, 1.0f)
                    ->Attribute(AZ::Edit::Attributes::Step, 0.25f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampConfig::m_edgeInset,
                        "Edge Inset", "Local strip with exactly zero stamp contribution, including its inner boundary. Feathering starts after this strip. Zero disables the inset; the heightmap mapping is unchanged.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, &HeightmapStampConfig::GetMaximumEdgeInset)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampConfig::m_relativeEdgeBlend,
                        "Relative Edge Blend", "Blend relief above the image's zero-sample elevation onto the accumulated ground near the edge, tapering to absolute Replace across the feather band. Independent of Strength/exponent; no effect with zero feather. Disable for the previous pure Replace behavior.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HeightmapStampConfig::m_priority,
                        "Priority", "Lower priorities are applied first; higher priorities replace accumulated lower layers.");
                // OrderingId is serialized, but not an editable height control.
            }
        }
    }
} // namespace TerrainCompositor
