#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/ComponentBus.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Asset/AssetCommon.h>
#include <AzFramework/Components/EditorEntityEvents.h>
#include <AzFramework/Entity/EntityContextBus.h>
#include <Atom/RPI.Reflect/Material/MaterialAsset.h>
#include <Atom/RPI.Public/Scene.h>

namespace TerrainCompositorCanvas
{
    class TerrainTintMaterialRequests : public AZ::ComponentBus
    {
    public:
        virtual AZStd::string GetStatus() const = 0;
    };
    using TerrainTintMaterialRequestBus = AZ::EBus<TerrainTintMaterialRequests>;

    //! The generic editor wrapper forwards EditorEntityEvents to this component.
    //! Asset lifetime and scene-wide ownership are handled by the terrain renderer.
    class TerrainTintMaterialComponent final
        : public AZ::Component
        , public AzFramework::EditorEntityEvents
        , private AZ::SystemTickBus::Handler
        , private TerrainTintMaterialRequestBus::Handler
        , private AzFramework::EntityContextEventBus::Handler
    {
    public:
        AZ_COMPONENT(TerrainTintMaterialComponent, "{CDF8AAED-7B39-4E4F-AF96-0C0E11FEE3D6}", AzFramework::EditorEntityEvents);
        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        void Activate() override;
        void Deactivate() override;
        void EditorActivate(AZ::EntityId entityId) override;
        void EditorDeactivate(AZ::EntityId entityId) override;

    private:
        void Start(AZ::EntityId entityId);
        void Stop();
        bool Bind();
        void OnSystemTick() override;
        void OnEntityContextReset() override;
        void OnEntityContextDestroyEntity(const AZ::EntityId& entityId) override;
        AZ::u32 OnConfigurationChanged();
        AZStd::string GetStatus() const override;
        AZ::Data::Asset<AZ::RPI::MaterialAsset> m_material;
        AZ::EntityId m_owner;
        AZ::RPI::SceneId m_sceneId;
        AzFramework::EntityContextId m_context;
        unsigned m_attemptsRemaining = 0;
        bool m_conflict = false;
    };
}
