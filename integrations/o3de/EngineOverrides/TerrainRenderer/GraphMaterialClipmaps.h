#pragma once
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <Atom/RPI.Public/Material/Material.h>
#include <Atom/RPI.Public/Scene.h>
#include <Atom/RPI.Public/View.h>
namespace TerrainCompositor
{
    class GraphMaterialClipmaps
    {
    public:
        GraphMaterialClipmaps();
        ~GraphMaterialClipmaps();
        void Update(AZ::RPI::Scene* scene, const AZ::Data::Instance<AZ::RPI::Material>& material,
            const AZ::Data::Instance<AZ::RPI::ShaderResourceGroup>& terrainSrg,
            const AZ::RPI::ViewPtr& view, AZ::u64 geometryRevision);
        void Invalidate();
        void Shutdown();
    private:
        struct State;
        AZStd::unique_ptr<State> m_state;
    };
}
