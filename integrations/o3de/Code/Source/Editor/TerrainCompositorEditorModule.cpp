#include "../TerrainCompositorModule.h"
#include "EditorHeightmapStampComponent.h"
#include "EditorTerrainCompositionGradientComponent.h"
#include "EditorTerrainCompositionHeightProviderComponent.h"
#include "EditorTerrainCompositionSurfaceProviderComponent.h"
#include "EditorTerrainMeshCutoutComponent.h"
#include "EditorTerrainMeshHeightStampComponent.h"
#include "HeightmapStampEditorSystemComponent.h"

namespace TerrainCompositor
{
    class TerrainCompositorEditorModule final : public TerrainCompositorModule
    {
    public:
        AZ_RTTI(
            TerrainCompositorEditorModule,
            TerrainCompositorEditorModuleTypeId,
            TerrainCompositorModule);
        AZ_CLASS_ALLOCATOR(TerrainCompositorEditorModule, AZ::SystemAllocator);

        TerrainCompositorEditorModule()
        {
            m_descriptors.push_back(HeightmapStampEditorSystemComponent::CreateDescriptor());
            m_descriptors.push_back(EditorHeightmapStampComponent::CreateDescriptor());
            m_descriptors.push_back(EditorTerrainMeshCutoutComponent::CreateDescriptor());
            m_descriptors.push_back(EditorTerrainMeshHeightStampComponent::CreateDescriptor());
            m_descriptors.push_back(EditorTerrainCompositionGradientComponent::CreateDescriptor());
            m_descriptors.push_back(EditorTerrainCompositionHeightProviderComponent::CreateDescriptor());
            m_descriptors.push_back(EditorTerrainCompositionSurfaceProviderComponent::CreateDescriptor());
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            auto components = TerrainCompositorModule::GetRequiredSystemComponents();
            components.push_back(azrtti_typeid<HeightmapStampEditorSystemComponent>());
            return components;
        }
    };
} // namespace TerrainCompositor

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(
    AZ_JOIN(Gem_, O3DE_GEM_NAME, _Editor),
    TerrainCompositor::TerrainCompositorEditorModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_TerrainCompositor_Editor, TerrainCompositor::TerrainCompositorEditorModule)
#endif

