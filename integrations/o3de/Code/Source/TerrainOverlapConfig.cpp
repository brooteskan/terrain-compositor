#include <TerrainCompositor/TerrainOverlapConfig.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace TerrainCompositor
{
    void TerrainOverlapSourceConfig::Reflect(AZ::ReflectContext* context)
    {
        auto* s = azrtti_cast<AZ::SerializeContext*>(context);
        if (!s || (!s->IsRemovingReflection() && s->FindClassData(azrtti_typeid<TerrainOverlapSourceConfig>()))) return;
        s->Class<TerrainOverlapSourceConfig>()->Version(1)
            ->Field("StampEntityId", &TerrainOverlapSourceConfig::m_stampEntityId)
            ->Field("MaterialSlot", &TerrainOverlapSourceConfig::m_materialSlot);
        if (auto* e = s->GetEditContext())
            e->Class<TerrainOverlapSourceConfig>("Overlap Source", "Bind one stamp instance to a graph material slot; source coordinates remain independent.")
                ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainOverlapSourceConfig::m_stampEntityId,
                    "Stamp", "Image or regular-grid mesh height stamp belonging to this composition.")
                ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainOverlapSourceConfig::m_materialSlot,
                    "Material Slot", "Slot 1 through 4 in the Terrain Overlap material graph.")
                ->Attribute(AZ::Edit::Attributes::Min, 1u)->Attribute(AZ::Edit::Attributes::Max, 4u);
    }

    void TerrainOverlapConfig::Reflect(AZ::ReflectContext* context)
    {
        TerrainOverlapSourceConfig::Reflect(context);
        auto* s = azrtti_cast<AZ::SerializeContext*>(context);
        if (!s || (!s->IsRemovingReflection() && s->FindClassData(azrtti_typeid<TerrainOverlapConfig>()))) return;
        s->Class<TerrainOverlapConfig>()->Version(1)
            ->Field("Enabled", &TerrainOverlapConfig::m_enabled)->Field("Width", &TerrainOverlapConfig::m_width)
            ->Field("RetainedSurfaces", &TerrainOverlapConfig::m_retainedSurfaces)
            ->Field("FlatGround", &TerrainOverlapConfig::m_flatGround)->Field("GroundHeight", &TerrainOverlapConfig::m_groundHeight)
            ->Field("GroundMaterialSlot", &TerrainOverlapConfig::m_groundMaterialSlot)->Field("Sources", &TerrainOverlapConfig::m_sources);
        if (auto* e = s->GetEditContext())
            e->Class<TerrainOverlapConfig>("Terrain Material Overlap", "Optional source-aware material blending. Geometry and existence are unchanged.")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainOverlapConfig::m_enabled, "Enabled", "Requires an overlap-capable graph; disabled preserves previous shading.")
                ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainOverlapConfig::m_width, "Transition Width (m)", "World metres of vertical separation and inward footprint distance. Zero disables blending.")
                ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainOverlapConfig::m_retainedSurfaces, "Retained Surface Envelope", "Use only when participating retained mesh shaders share this resolver. Otherwise retain ordered stamp precedence.")
                ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainOverlapConfig::m_flatGround, "Flat Ground Contributor", "Explicit constant-height contributor. Does not create ground geometry or replace the procedural height source.")
                ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainOverlapConfig::m_groundHeight, "Ground Height (m)", "Actual world elevation of the flat ground surface.")
                ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainOverlapConfig::m_groundMaterialSlot, "Ground Material Slot", "Slot 1 through 4; counts against the four-source limit.")
                ->Attribute(AZ::Edit::Attributes::Min, 1u)->Attribute(AZ::Edit::Attributes::Max, 4u)
                ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainOverlapConfig::m_sources, "Sources", "At most four contributors including ground. Slots must be unique; priority comes from stamps, never list order.");
    }
}
