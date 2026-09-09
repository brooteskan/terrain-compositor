#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzFramework/Components/EditorEntityEvents.h>
#include <Terrain/Ebuses/TerrainAreaSurfaceRequestBus.h>
#include <TerrainCompositor/TerrainCompositionProviderBinding.h>
#include <TerrainCompositor/TerrainCompositionBus.h>
#include <TerrainCompositor/TerrainCompositorTypeIds.h>

namespace TerrainCompositor
{
    class TerrainCompositionSurfaceProviderConfig final : public AZ::ComponentConfig
    {
    public:
        AZ_CLASS_ALLOCATOR(TerrainCompositionSurfaceProviderConfig, AZ::SystemAllocator);
        AZ_RTTI(TerrainCompositionSurfaceProviderConfig, TerrainCompositionSurfaceProviderConfigTypeId, AZ::ComponentConfig);

        static void Reflect(AZ::ReflectContext* context);

        AZ::EntityId m_compositionEntityId;
    };

    //! Terrain-region adapter for the composition's immutable surface query snapshot.
    //! Add this to the Terrain Layer Spawner entity in place of Terrain Surface Gradient List.
    class TerrainCompositionSurfaceProviderComponent final
        : public AZ::Component
        , public AzFramework::EditorEntityEvents
        , private Terrain::TerrainAreaSurfaceRequestBus::Handler
        , private AZ::SystemTickBus::Handler
    {
    public:
        AZ_COMPONENT_DECL(TerrainCompositionSurfaceProviderComponent);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& services);

        // Intentional O3DE terrain-provider adapter interface; the height and surface buses require distinct components.
        /* jscpd:ignore-start */
        TerrainCompositionSurfaceProviderComponent() = default;
        explicit TerrainCompositionSurfaceProviderComponent(const TerrainCompositionSurfaceProviderConfig& configuration);

        void Activate() override;
        void Deactivate() override;
        void EditorActivate(AZ::EntityId entityId) override;
        void EditorDeactivate(AZ::EntityId entityId) override;
        bool ReadInConfig(const AZ::ComponentConfig* baseConfig) override;
        bool WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const override;
        AZStd::string GetStatusMessage() const;

    private:
        void StartProvider(AZ::EntityId terrainRegionEntityId);
        void StopProvider();
        void RestartProvider();
        void RefreshArea() const;
        /* jscpd:ignore-end */
        void OnSystemTick() override;

        void GetSurfaceWeights(const AZ::Vector3& position,
            AzFramework::SurfaceData::SurfaceTagWeightList& outSurfaceWeights) const override;
        void GetSurfaceWeightsFromList(AZStd::span<const AZ::Vector3> positions,
            AZStd::span<AzFramework::SurfaceData::SurfaceTagWeightList> outSurfaceWeights) const override;

        TerrainCompositionProviderBinding m_binding;
        TerrainCompositionSurfaceProviderConfig m_configuration;
    };
} // namespace TerrainCompositor
