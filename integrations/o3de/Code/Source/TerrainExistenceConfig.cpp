#include <TerrainCompositor/TerrainExistenceConfig.h>

#include <AzCore/Asset/AssetSerializer.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace TerrainCompositor
{
    void TerrainHoleMaskConfig::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection() && serialize->FindClassData(azrtti_typeid<TerrainHoleMaskConfig>()))
            {
                return;
            }
            serialize->Enum<TerrainExistenceOperation>()
                ->Value("RemoveTerrain", TerrainExistenceOperation::RemoveTerrain)
                ->Value("RestoreTerrain", TerrainExistenceOperation::RestoreTerrain);
            serialize->Class<TerrainHoleMaskConfig>()
                ->Version(1)
                ->Field("MaskAsset", &TerrainHoleMaskConfig::m_maskAsset)
                ->Field("Threshold", &TerrainHoleMaskConfig::m_threshold)
                ->Field("Operation", &TerrainHoleMaskConfig::m_operation);

            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<TerrainHoleMaskConfig>("Terrain Hole Mask",
                    "Explicit transformed terrain-existence input. Below-threshold samples leave lower terrain unchanged.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainHoleMaskConfig::m_maskAsset,
                        "Mask Asset", "Optional unsigned 16-bit GSI16 mask. Missing or unavailable data contributes nothing.")
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &TerrainHoleMaskConfig::m_threshold,
                        "Threshold", "Bilinearly sampled mask values at or above this value apply the selected operation.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->Attribute(AZ::Edit::Attributes::Step, 0.01f)
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &TerrainHoleMaskConfig::m_operation,
                        "Operation", "Remove terrain, or restore terrain removed by an earlier/lower-priority layer.");
            }
        }
    }
} // namespace TerrainCompositor
