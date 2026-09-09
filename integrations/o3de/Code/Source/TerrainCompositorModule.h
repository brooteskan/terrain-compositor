#pragma once

#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/Module/Module.h>

#include "TerrainCompositorSystemComponent.h"

#include <TerrainCompositor/Components/HeightmapStampComponent.h>
#include <TerrainCompositor/Components/ProceduralGroundGradientComponent.h>
#include <TerrainCompositor/Components/TerrainCompositionGradientComponent.h>
#include <TerrainCompositor/Components/TerrainCompositionHeightProviderComponent.h>
#include <TerrainCompositor/Components/TerrainCompositionSurfaceProviderComponent.h>
#include <TerrainCompositor/Components/TerrainMeshCutoutComponent.h>
#include <TerrainCompositor/Components/TerrainMeshHeightStampComponent.h>
#include <TerrainCompositor/TerrainCompositorTypeIds.h>

namespace TerrainCompositor
{
    class TerrainCompositorModule : public AZ::Module
    {
    public:
        AZ_RTTI(TerrainCompositorModule, TerrainCompositorModuleTypeId, AZ::Module);
        AZ_CLASS_ALLOCATOR(TerrainCompositorModule, AZ::SystemAllocator);

        TerrainCompositorModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                {
                    HeightmapStampComponent::CreateDescriptor(),
                    ProceduralGroundGradientComponent::CreateDescriptor(),
                    TerrainCompositionGradientComponent::CreateDescriptor(),
                    TerrainCompositionHeightProviderComponent::CreateDescriptor(),
                    TerrainCompositionSurfaceProviderComponent::CreateDescriptor(),
                    TerrainMeshCutoutComponent::CreateDescriptor(),
                    TerrainMeshHeightStampComponent::CreateDescriptor(),
                    TerrainCompositorSystemComponent::CreateDescriptor(),
                });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return { azrtti_typeid<TerrainCompositorSystemComponent>() };
        }
    };
} // namespace TerrainCompositor

