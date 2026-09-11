#include "TerrainCompositorSystemComponent.h"

#include <Atom/RPI.Public/FeatureProcessorFactory.h>
#include <AzCore/Serialization/SerializeContext.h>

#include <TerrainCompositor/TerrainCompositorTypeIds.h>
#include <TerrainCompositor/TerrainMeshCutoutFeatureProcessor.h>

namespace TerrainCompositor
{
    AZ_COMPONENT_IMPL(
        TerrainCompositorSystemComponent,
        "TerrainCompositorSystemComponent",
        TerrainCompositorSystemComponentTypeId);

    void TerrainCompositorSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        TerrainMeshCutoutFeatureProcessor::Reflect(context);
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<TerrainCompositorSystemComponent, AZ::Component>()->Version(0);
        }
    }

    void TerrainCompositorSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("TerrainCompositorService"));
    }

    void TerrainCompositorSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("TerrainCompositorService"));
    }

    void TerrainCompositorSystemComponent::GetRequiredServices(
        AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        // The feature processor factory must outlive our registration.
        required.push_back(AZ_CRC_CE("RPISystem"));
    }

    void TerrainCompositorSystemComponent::GetDependentServices(
        [[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
    }

    void TerrainCompositorSystemComponent::Init()
    {
    }

    void TerrainCompositorSystemComponent::Activate()
    {
        m_heightmapDataCache = AZStd::make_unique<HeightmapDataCache>();
        m_terrainMeshCutoutDataCache = AZStd::make_unique<TerrainMeshCutoutDataCache>();
        m_terrainMeshHeightDataCache = AZStd::make_unique<TerrainMeshHeightDataCache>();
        m_terrainMeshCutoutRenderRegistry = AZStd::make_unique<TerrainMeshCutoutRenderRegistry>();
        AZ::RPI::FeatureProcessorFactory::Get()->RegisterFeatureProcessor<TerrainMeshCutoutFeatureProcessor>();
    }

    void TerrainCompositorSystemComponent::Deactivate()
    {
        AZ::RPI::FeatureProcessorFactory::Get()->UnregisterFeatureProcessor<TerrainMeshCutoutFeatureProcessor>();
        m_terrainMeshCutoutRenderRegistry.reset();
        m_terrainMeshHeightDataCache.reset();
        m_terrainMeshCutoutDataCache.reset();
        m_heightmapDataCache.reset();
    }
} // namespace TerrainCompositor

