#include <TerrainCompositor/SurfaceCompositionConfig.h>

#include <AzCore/Asset/AssetSerializer.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace TerrainCompositor
{
    void SurfaceMapSetConfig::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection() && serialize->FindClassData(azrtti_typeid<SurfaceMapSetConfig>()))
            {
                return;
            }
            serialize->Class<SurfaceMapSetConfig>()
                ->Version(1)
                ->Field("SurfaceIdAAsset", &SurfaceMapSetConfig::m_surfaceIdAAsset)
                ->Field("SurfaceIdBAsset", &SurfaceMapSetConfig::m_surfaceIdBAsset)
                ->Field("BlendMaskAsset", &SurfaceMapSetConfig::m_blendMaskAsset);

            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<SurfaceMapSetConfig>("Surface Map Set",
                    "Unsigned 16-bit categorical ID A, with optional ID B and blend mask. ID B and blend must be assigned together.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SurfaceMapSetConfig::m_surfaceIdAAsset,
                        "Surface ID A", "Optional unsigned 16-bit GSI16 map. ID zero is transparent; other values resolve through the composition palette.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SurfaceMapSetConfig::m_surfaceIdBAsset,
                        "Surface ID B", "Optional second unsigned 16-bit GSI16 map. Assign together with Blend Mask.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SurfaceMapSetConfig::m_blendMaskAsset,
                        "Blend Mask", "Optional unsigned 16-bit GSI16 blend: A has weight 1-b and B has weight b. Assign together with Surface ID B.");
            }
        }
    }

    void SurfacePaletteEntry::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection() && serialize->FindClassData(azrtti_typeid<SurfacePaletteEntry>()))
            {
                return;
            }
            serialize->Class<SurfacePaletteEntry>()
                ->Version(1)
                ->Field("ExportedId", &SurfacePaletteEntry::m_exportedId)
                ->Field("SurfaceTag", &SurfacePaletteEntry::m_surfaceTag);

            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<SurfacePaletteEntry>("Surface Palette Entry", "Stable exported surface ID to O3DE surface tag.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SurfacePaletteEntry::m_exportedId,
                        "Exported ID", "Unsigned authoring label in [1, 65535]. Zero is reserved for transparency.")
                    ->Attribute(AZ::Edit::Attributes::Min, AZ::u16{ 1 })
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &SurfacePaletteEntry::m_surfaceTag,
                        "Surface Tag", "Tag consumed by O3DE Terrain Surface Materials List.");
            }
        }
    }

    void SurfaceBaseWeight::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection() && serialize->FindClassData(azrtti_typeid<SurfaceBaseWeight>()))
            {
                return;
            }
            serialize->Class<SurfaceBaseWeight>()
                ->Version(1)
                ->Field("ExportedId", &SurfaceBaseWeight::m_exportedId)
                ->Field("Weight", &SurfaceBaseWeight::m_weight);

            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<SurfaceBaseWeight>("Base Surface Weight", "Explicit composition base resolved through the shared palette.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SurfaceBaseWeight::m_exportedId,
                        "Exported ID", "Palette ID to use in the base surface distribution.")
                    ->Attribute(AZ::Edit::Attributes::Min, AZ::u16{ 1 })
                    ->DataElement(AZ::Edit::UIHandlers::Default, &SurfaceBaseWeight::m_weight,
                        "Weight", "Finite positive weight. Base entries are merged by tag and normalized.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f);
            }
        }
    }
} // namespace TerrainCompositor
