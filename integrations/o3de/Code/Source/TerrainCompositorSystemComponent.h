#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>

#include <TerrainCompositor/HeightmapDataCache.h>
#include <TerrainCompositor/TerrainMeshCutoutDataCache.h>
#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>
#include <TerrainCompositor/TerrainMeshHeightDataCache.h>

namespace TerrainCompositor
{
    class TerrainCompositorSystemComponent final : public AZ::Component
    {
    public:
        AZ_COMPONENT_DECL(TerrainCompositorSystemComponent);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent);

        void Init() override;
        void Activate() override;
        void Deactivate() override;

    private:
        AZStd::unique_ptr<HeightmapDataCache> m_heightmapDataCache;
        AZStd::unique_ptr<TerrainMeshCutoutDataCache> m_terrainMeshCutoutDataCache;
        AZStd::unique_ptr<TerrainMeshHeightDataCache> m_terrainMeshHeightDataCache;
        AZStd::unique_ptr<TerrainMeshCutoutRenderRegistry> m_terrainMeshCutoutRenderRegistry;
    };
} // namespace TerrainCompositor

