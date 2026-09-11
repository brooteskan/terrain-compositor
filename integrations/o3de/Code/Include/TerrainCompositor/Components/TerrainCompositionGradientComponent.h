#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityBus.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/smart_ptr/weak_ptr.h>
#include <AzFramework/Components/EditorEntityEvents.h>
#include <GradientSignal/Ebuses/GradientRequestBus.h>
#include <LmbrCentral/Dependency/DependencyMonitor.h>
#include <LmbrCentral/Shape/ShapeComponentBus.h>
#include <TerrainCompositor/HeightmapControlThread.h>
#include <TerrainCompositor/Internal/CompositionRegistrations.h>
#include <TerrainCompositor/Internal/PreparedComposition.h>
#include <TerrainCompositor/HeightmapStampSampling.h>
#include <TerrainCompositor/SurfaceCompositionConfig.h>
#include <TerrainCompositor/SurfaceStampSampling.h>
#include <TerrainCompositor/TerrainCompositionBus.h>
#include <TerrainCompositor/TerrainExistenceBus.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>
#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>
#include <TerrainCompositor/TerrainInvalidation.h>
#include <TerrainCompositor/TerrainQuality.h>
#include <atomic>
#include <memory>
#include <mutex>

namespace TerrainCompositor
{
    class TerrainCompositionConfig final : public AZ::ComponentConfig
    {
    public:
        AZ_CLASS_ALLOCATOR(TerrainCompositionConfig, AZ::SystemAllocator);
        AZ_RTTI(TerrainCompositionConfig, TerrainCompositionConfigTypeId, AZ::ComponentConfig);

        static void Reflect(AZ::ReflectContext* context);

        AZ::EntityId m_proceduralSourceEntityId;
        AZ::EntityId m_targetTerrainRegionEntityId;
        TerrainQualityConfig m_terrainQuality;
        AZStd::vector<SurfacePaletteEntry> m_surfacePalette;
        AZStd::vector<SurfaceBaseWeight> m_baseSurfaceWeights;
    };

    //! Lives on its own entity, not on the Terrain Height Gradient List entity.
    //! Composes the procedural base with scoped, validated stamps in world-height
    //! meters.
    class TerrainCompositionGradientComponent final
        : public AZ::Component
        , public AzFramework::EditorEntityEvents
        , private GradientSignal::GradientRequestBus::Handler
        , private TerrainCompositionRequestBus::Handler
        , private TerrainCompositionHeightRequestBus::Handler
        , private TerrainCompositionSurfaceRequestBus::Handler
        , private LmbrCentral::ShapeComponentNotificationsBus::Handler
        , private AZ::EntityBus::Handler
        , private AZ::SystemTickBus::Handler
        , private AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT_DECL(TerrainCompositionGradientComponent);

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent);

        TerrainCompositionGradientComponent() = default;
        explicit TerrainCompositionGradientComponent(const TerrainCompositionConfig& configuration);

        void Activate() override;
        void Deactivate() override;
        void EditorActivate(AZ::EntityId entityId) override;
        void EditorDeactivate(AZ::EntityId entityId) override;
        bool ReadInConfig(const AZ::ComponentConfig* baseConfig) override;
        bool WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const override;
        //! Editor previews inject the stock wrapped renderer profile before
        //! activation.
        void SetTerrainQualityBaseline(const TerrainQualityBaseline& baseline);
        //! Read-only, control-thread authoring diagnostic; does not sample terrain.
        AZStd::string GetStatusMessage() const;

    private:
        friend class TerrainRenderGeometryBatchTests;
        template<class> friend class TerrainRegistrationLifecycleTests;
        struct QueryState : Internal::PreparedComposition
        {
            TerrainCompositionAddress m_address;
            AZ::Uuid m_session{};
            AZ::u64 m_revision = 0;
            AZ::EntityId m_ownerEntityId{};
            AZ::EntityId m_sourceEntityId{};
            AZ::EntityId m_regionEntityId{};
            std::weak_ptr<TerrainMeshCutoutRenderChannel> m_renderChannel;
        };
        using QueryStatePtr = std::shared_ptr<const QueryState>;

        struct ReconstructionCacheEntry
        {
            AZStd::weak_ptr<const HeightmapData> m_source;
            AZStd::weak_ptr<const HeightmapReconstructionData> m_reconstruction;
            AZ::u64 m_sourceRevision = 0;
            HeightmapSamplingMode m_mode = HeightmapSamplingMode::Bilinear;
            float m_radius = 0.0f;
        };

        // Dependency callbacks can originate on other threads. They own only a weak
        // subscription mailbox, never the component; replacing the mailbox retires
        // all callbacks from the old source/session.
        struct SourceChanges
        {
            std::mutex m_mutex;
            AZStd::vector<AZ::Aabb> m_regions;
            bool m_wholeRegion = false;
        };

        void StartComposition(AZ::EntityId entityId);
        AZ::u32 OnConfigurationChanged();
        void ConnectDependencies();
        void RefreshRegionBounds();
        void QueueHeightRegionChange(const QueryState& state);
        void QueueSurfaceRegionChange(const QueryState& state);
        void QueueHeightFootprintChange(const HeightmapStampFootprintChange& change);
        void QueueSurfaceFootprintChange(const QueryState& state, const AZ::Aabb& footprint);
        void CollectSourceChanges();
        void PublishStamps();
        HeightmapReconstructionDataPtr AcquireHeightmapReconstruction(
            const HeightmapDataPtr& source, HeightmapSamplingMode mode, float radius);
        void OnSystemTick() override;
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
        int GetTickOrder() override
        {
            return AZ::TICK_DEFAULT - 1;
        }
        static void DispatchChanges(
            TerrainCompositionAddress address,
            const AZStd::vector<HeightmapStampFootprintChange>& changes,
            const AZStd::vector<AZ::Aabb>& heightRegions,
            const AZStd::vector<AZ::Aabb>& surfaceRegions);
        QueryStatePtr GetQueryState() const;
        static bool CanSampleSource(const QueryState& state);
        static float GetNormalizedHeight(const QueryState& state, const AZ::Vector3& position);
        static TerrainRenderGeometryQuery CreateRenderGeometryQuery(QueryStatePtr state);
        static TerrainMeshHeightGapActivationPtr CaptureGapActivation(const QueryState& state);
        bool GetComposedTerrainExists(const QueryState& state, const AZ::Vector3& position,
            const TerrainMeshHeightGapActivationPtr& activation) const;

        float GetValue(const GradientSignal::GradientSampleParams& sampleParams) const override;
        void GetValues(AZStd::span<const AZ::Vector3> positions, AZStd::span<float> outValues) const override;
        bool IsEntityInHierarchy(const AZ::EntityId& entityId) const override;

        float GetHeight(AZ::EntityId terrainRegionEntityId, const AZ::Vector3& position, bool& terrainExists) const override;
        void GetHeights(
            AZ::EntityId terrainRegionEntityId,
            AZStd::span<const AZ::Vector3> positions,
            AZStd::span<float> outHeights,
            AZStd::span<bool> terrainExists) const override;

        void GetSurfaceWeights(
            AZ::EntityId terrainRegionEntityId,
            const AZ::Vector3& position,
            AzFramework::SurfaceData::SurfaceTagWeightList& outSurfaceWeights) const override;
        void GetSurfaceWeightsFromList(
            AZ::EntityId terrainRegionEntityId,
            AZStd::span<const AZ::Vector3> positions,
            AZStd::span<AzFramework::SurfaceData::SurfaceTagWeightList> outSurfaceWeights) const override;

        template<class Registration, class Classify>
        bool RegisterAndPublish(const Registration& registration, Classify classify);
        template<class Registration>
        void UnregisterAndPublish(AZ::EntityId entityId, const AZ::Uuid& registrationId, const AZ::Uuid& compositionSession);

        bool RegisterStamp(const HeightmapStampRegistrationData& registration) override;
        bool RegisterMeshCutout(const TerrainMeshCutoutRegistrationData& registration) override;
        bool RegisterMeshHeightStamp(const TerrainMeshHeightStampRegistrationData& registration) override;
        AZ::Uuid GetCompositionSession() const override;
        void UnregisterStamp(AZ::EntityId stampEntityId, const AZ::Uuid& registrationId, const AZ::Uuid& compositionSession) override;
        AZStd::vector<HeightmapStampRegistrationData> GetRegisteredStamps() const override;
        void UnregisterMeshCutout(AZ::EntityId cutoutEntityId, const AZ::Uuid& registrationId, const AZ::Uuid& compositionSession) override;
        AZStd::vector<TerrainMeshCutoutRegistrationData> GetRegisteredMeshCutouts() const override;
        void UnregisterMeshHeightStamp(
            AZ::EntityId stampEntityId, const AZ::Uuid& registrationId, const AZ::Uuid& compositionSession) override;
        AZStd::vector<TerrainMeshHeightStampRegistrationData> GetRegisteredMeshHeightStamps() const override;
        size_t GetOrderingClaimCount(AZStd::string_view stableOrderKey) const override;
        AZ::Aabb GetTargetRegionBounds() const override;

        void OnShapeChanged(LmbrCentral::ShapeComponentNotifications::ShapeChangeReasons reason) override;
        void OnEntityActivated(const AZ::EntityId& entityId) override;
        void OnEntityDeactivated(const AZ::EntityId& entityId) override;

        // Height-query workers share only the atomic immutable pointer below. Source
        // callbacks use their separate mailbox; no registry/mailbox locks enter the
        // query path.
        HeightmapControlThread m_controlThread;
        TerrainCompositionConfig m_configuration;
        TerrainCompositionConfig m_appliedConfiguration;
        bool m_hasAppliedConfiguration = false;
        TerrainCompositionAddress m_address;
        TerrainQualityController m_qualityController;
        TerrainQualityBaseline m_qualityBaseline;
        bool m_hasQualityBaseline = false;
        AZ::Uuid m_session{};
        AZ::u64 m_revision = 0;
        bool m_active = false;
        unsigned m_configurationUpdateDepth = 0;
        Internal::CompositionRegistrations m_registrations;
        AZStd::unordered_map<AZStd::string, AZStd::string> m_collisions;
        AZStd::vector<ReconstructionCacheEntry> m_reconstructionCache;
        AZStd::vector<AZStd::string> m_pendingDiagnostics;
        AZStd::vector<HeightmapStampFootprintChange> m_pendingChanges;
        AZStd::unordered_map<AZ::EntityId, AZ::u8> m_dirtyStamps;
        // Previous published contributors provide removal coverage. Pending
        // value-owned work retains it until dispatch, without invalid-to-invalid
        // edits repeatedly dirtying an already removed footprint.
        TerrainInvalidation m_pendingHeightTerrain;
        TerrainInvalidation m_pendingSurfaceTerrain;
        std::shared_ptr<SourceChanges> m_sourceChanges;
        bool m_sourceWasSampleable = false;
        LmbrCentral::DependencyMonitor m_sourceMonitor;
        bool m_regionDeactivating = false;
        AZ::Aabb m_regionBounds = AZ::Aabb::CreateNull();
        float m_publishedCollisionGridSpacing = 0.0f;
        std::atomic<QueryStatePtr> m_queryState{ std::make_shared<const QueryState>() };
        TerrainMeshHeightGapActivationPtr m_observedGapActivation;
    };
} // namespace TerrainCompositor
