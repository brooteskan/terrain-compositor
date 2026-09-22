#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/parallel/shared_mutex.h>
#include <AzFramework/Components/EditorEntityEvents.h>
#include <AzFramework/Entity/EntityContextBus.h>
#include <Atom/RPI.Public/Material/Material.h>
#include <GradientSignal/Ebuses/GradientRequestBus.h>
#include <GradientSignal/GradientSampler.h>
#include <LmbrCentral/Dependency/DependencyMonitor.h>
#include <TerrainCompositor/TerrainExistenceBus.h>
#include <TerrainCompositor/TerrainProceduralSnapshot.h>
#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>
#include <atomic>

namespace TerrainCompositor
{
    //! Editable parameters for the procedural ground height field and shared terrain tint.
    class ProceduralGroundGradientConfig final
        : public AZ::ComponentConfig
    {
    public:
        AZ_CLASS_ALLOCATOR(ProceduralGroundGradientConfig, AZ::SystemAllocator);
        AZ_RTTI(ProceduralGroundGradientConfig, "{A3E66702-7C4B-4FD1-B56A-59489A2E479D}", AZ::ComponentConfig);

        static void Reflect(AZ::ReflectContext* context);

        //! Number of hill centers per meter along the terrain. Zero produces flat ground.
        float m_hillDensity = 0.005f;

        //! Maximum height or depth from the base ground plane. DefaultLevel's 2048 m height range maps this value directly to meters.
        float m_amplitudeMeters = 180.0f;

        //! Controls how quickly each feature rises or falls. Higher values create tighter bumps and depressions without changing their count.
        float m_frequency = 1.5f;

        //! Integer powers are experimental; captured for every height consumer.
        ProceduralHillPolicy m_kernelPolicy = ProceduralHillPolicy::CachedExact;

        //! Terrain-wide shader tint strength. Zero restores the untinted base color.
        float m_noiseTintStrength = 0.75f;

        //! Optional normalized mask. Values at or above Hole Threshold remove procedural terrain.
        GradientSignal::GradientSampler m_holeMask;
        float m_holeThreshold = 0.5f;
    };

    //! Supplies a deterministic, mathematically unbounded procedural bump and depression field for terrain height composition.
    class ProceduralGroundGradientComponent final
        : public AZ::Component
        , public AzFramework::EditorEntityEvents
        , private GradientSignal::GradientRequestBus::Handler
        , private TerrainExistenceSourceRequestBus::Handler
        , private TerrainProceduralSnapshotRequestBus::Handler
        , private AzFramework::EntityContextEventBus::Handler
        , private AZ::SystemTickBus::Handler
    {
    public:
        AZ_COMPONENT_DECL(ProceduralGroundGradientComponent);

        static void Reflect(AZ::ReflectContext* context);

        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent);

        explicit ProceduralGroundGradientComponent(const ProceduralGroundGradientConfig& configuration);
        ProceduralGroundGradientComponent() = default;
        ~ProceduralGroundGradientComponent() override;

        void Activate() override;
        void Deactivate() override;
        bool ReadInConfig(const AZ::ComponentConfig* baseConfig) override;
        bool WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const override;

        void EditorActivate(AZ::EntityId entityId) override;
        void EditorDeactivate(AZ::EntityId entityId) override;

        //! Preserved scalar oracle for differential measurements; never selected
        //! implicitly by optimized retained batches.
        static float EvaluateReference(const AZ::Vector3& position, const ProceduralGroundGradientConfig& configuration)
        {
            return EvaluatePosition(position, configuration);
        }

    private:
        TerrainProceduralSnapshotPtr AcquireTerrainSnapshot() const override;
        float GetValue(const GradientSignal::GradientSampleParams& sampleParams) const override;
        void GetValues(AZStd::span<const AZ::Vector3> positions, AZStd::span<float> outValues) const override;
        bool GetTerrainExists(const AZ::Vector3& position) const override;
        void GetTerrainExistsFromList(
            AZStd::span<const AZ::Vector3> positions, AZStd::span<bool> terrainExists) const override;

        static float EvaluatePosition(const AZ::Vector3& position, const ProceduralGroundGradientConfig& configuration,
            bool pruneZeroProfiles = false);

        void StartGradient(AZ::EntityId entityId);
        void StopGradient();
        AZ::u32 OnConfigurationChanged();
        ProceduralGroundGradientConfig GetQueryConfiguration() const;
        std::shared_ptr<const ProceduralHillKernel> GetQueryKernel() const;

        void StopNoiseTintUpdates();
        void BindTerrainMaterial();
        void BindTerrainMaterialForContext(const AzFramework::EntityContextId& context);
        void ApplyNoiseTint(const AZ::Data::Instance<AZ::RPI::Material>& material);
        void OnSystemTick() override;
        void OnEntityContextDestroyEntity(const AZ::EntityId& entityId) override;
        void OnEntityContextReset() override;

        static constexpr float NormalizedGroundHeight = 0.5f;
        static constexpr float TerrainHeightRangeMeters = 2048.0f;

        // Serialization/inspector state is only accessed on the main thread. Queries never read it directly.
        ProceduralGroundGradientConfig m_configuration;
        mutable AZStd::shared_mutex m_queryMutex;
        ProceduralGroundGradientConfig m_queryConfiguration;
        std::shared_ptr<const ProceduralHillKernel> m_queryKernel;
        std::shared_ptr<TerrainPreparationDependency> m_snapshotDependency;
        AZ::Uuid m_snapshotSession;
        AZ::EntityId m_snapshotEntityId;
        AZ::EntityId m_activeEntityId;
        AZ::Data::Instance<AZ::RPI::Material> m_terrainMaterial;
        struct MaterialUpdateState
        {
            std::atomic<ProceduralGroundGradientComponent*> m_owner = nullptr;
            std::atomic<AZ::u64> m_generation = 0;
        };
        std::shared_ptr<MaterialUpdateState> m_materialUpdateState;
        std::weak_ptr<TerrainMeshCutoutRenderChannel> m_materialChannel;
        TerrainMeshCutoutRenderChannel::MaterialChangedEvent::Handler m_materialChangedHandler;
        LmbrCentral::DependencyMonitor m_holeDependencyMonitor;
        bool m_reportedMissingTintProperty = false;
        static constexpr unsigned MaterialResolutionAttempts = 8;
        unsigned m_materialRetriesRemaining = 0;
    };
} // namespace TerrainCompositor
