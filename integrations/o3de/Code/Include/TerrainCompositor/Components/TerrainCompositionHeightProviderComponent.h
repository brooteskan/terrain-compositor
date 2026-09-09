#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/parallel/shared_mutex.h>
#include <AzFramework/Components/EditorEntityEvents.h>
#include <AzFramework/Terrain/TerrainDataRequestBus.h>
#include <TerrainSystem/TerrainSystemBus.h>
#include <LmbrCentral/Dependency/DependencyNotificationBus.h>
#include <TerrainCompositor/TerrainCompositionProviderBinding.h>
#include <TerrainCompositor/TerrainCompositionBus.h>
#include <TerrainCompositor/TerrainCompositorTypeIds.h>

namespace TerrainCompositor
{
    class TerrainCompositionHeightProviderConfig final : public AZ::ComponentConfig
    {
    public:
        AZ_CLASS_ALLOCATOR(TerrainCompositionHeightProviderConfig, AZ::SystemAllocator);
        AZ_RTTI(TerrainCompositionHeightProviderConfig, TerrainCompositionHeightProviderConfigTypeId, AZ::ComponentConfig);

        static void Reflect(AZ::ReflectContext* context);
        AZ::EntityId m_compositionEntityId;
    };

    //! Terrain-region adapter that returns the compositor's atomic world-height/existence snapshot.
    class TerrainCompositionHeightProviderComponent final
        : public AZ::Component
        , public AzFramework::EditorEntityEvents
        , private Terrain::TerrainAreaHeightRequestBus::Handler
        , private LmbrCentral::DependencyNotificationBus::Handler
        , private AzFramework::Terrain::TerrainDataNotificationBus::Handler
        , private AZ::SystemTickBus::Handler
    {
    public:
        AZ_COMPONENT_DECL(TerrainCompositionHeightProviderComponent);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& services);

        // Intentional O3DE terrain-provider adapter interface; the height and surface buses require distinct components.
        /* jscpd:ignore-start */
        TerrainCompositionHeightProviderComponent() = default;
        explicit TerrainCompositionHeightProviderComponent(const TerrainCompositionHeightProviderConfig& configuration);

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
        void RefreshHeightBounds();
        void OnSystemTick() override;
        void OnCompositionChanged() override;
        void OnCompositionRegionChanged(const AZ::Aabb& dirtyRegion) override;
        void OnTerrainDataChanged(const AZ::Aabb& dirtyRegion, TerrainDataChangedMask dataChangedMask) override;

        void GetHeight(const AZ::Vector3& inPosition, AZ::Vector3& outPosition, bool& terrainExists) override;
        void GetHeights(AZStd::span<AZ::Vector3> inOutPositionList, AZStd::span<bool> terrainExistsList) override;

        TerrainCompositionProviderBinding m_binding;
        TerrainCompositionHeightProviderConfig m_configuration;
        mutable AZStd::shared_mutex m_heightBoundsMutex;
        AzFramework::Terrain::FloatRange m_heightBounds = AzFramework::Terrain::FloatRange::CreateNull();
    };
} // namespace TerrainCompositor
