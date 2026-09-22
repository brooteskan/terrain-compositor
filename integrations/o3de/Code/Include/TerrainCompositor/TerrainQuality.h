#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityBus.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/std/string/string.h>
#include <AzFramework/Entity/EntityContextBus.h>
#include <AzFramework/Scene/Scene.h>
#include <AzFramework/Scene/SceneSystemInterface.h>
#include <AzFramework/Terrain/TerrainDataRequestBus.h>

namespace TerrainCompositor
{
    class TerrainRendererMeshQualityConfig final
    {
    public:
        AZ_CLASS_ALLOCATOR(TerrainRendererMeshQualityConfig, AZ::SystemAllocator);
        AZ_TYPE_INFO(TerrainRendererMeshQualityConfig, "{B0600B70-EB55-47C8-AB60-D80A9E4428B3}");

        static void Reflect(AZ::ReflectContext* context);

        bool m_overrideMeshSettings = false;
        float m_renderDistance = 4096.0f;
        float m_firstLodDistance = 128.0f;
        bool m_clodEnabled = true;
        float m_clodDistance = 16.0f;
    };

    class TerrainQualityConfig final
    {
    public:
        AZ_CLASS_ALLOCATOR(TerrainQualityConfig, AZ::SystemAllocator);
        AZ_TYPE_INFO(TerrainQualityConfig, "{1859ED9E-0483-4F60-8B2A-64262CE6BB65}");

        static void Reflect(AZ::ReflectContext* context);

        bool m_overrideTerrainQuality = false;
        float m_heightQueryResolution = 1.0f;
        TerrainRendererMeshQualityConfig m_renderer;
    };

    struct TerrainQualityBaseline
    {
        float m_heightQueryResolution = 1.0f;
        float m_renderDistance = 4096.0f;
        float m_firstLodDistance = 128.0f;
        bool m_clodEnabled = true;
        float m_clodDistance = 16.0f;
        bool m_meshSettingsAvailable = false;
    };

    enum class TerrainQualityStatus
    {
        Disabled,
        Pending,
        Applied,
        Invalid,
        Conflict
    };

    //! Main-thread lifecycle adapter for the process-global terrain grid and context-local renderer mesh.
    //! The stock Terrain World and Terrain World Renderer retain service and feature-processor ownership.
    class TerrainQualityController final
        : private AZ::TickBus::Handler
        , private AZ::EntitySystemBus::Handler
        , private AzFramework::Terrain::TerrainDataNotificationBus::Handler
    {
    public:
        TerrainQualityController() = default;
        ~TerrainQualityController();
        TerrainQualityController(const TerrainQualityController&) = delete;
        TerrainQualityController& operator=(const TerrainQualityController&) = delete;

        void Activate(AzFramework::EntityContextId contextId, AZ::EntityId ownerEntityId,
            const TerrainQualityBaseline* baseline = nullptr);
        void Deactivate();
        void Update(const TerrainQualityConfig& configuration);

        TerrainQualityStatus GetStatus() const { return m_status; }
        AZStd::string GetStatusMessage() const;
        bool ConsumeHeightSettingsChanged();

    private:
        friend class TerrainQualityRegistry;

        bool Validate() const;
        bool CaptureBaseline(TerrainQualityBaseline& baseline) const;
        bool Apply(const TerrainQualityBaseline& baseline);
        void Restore(const TerrainQualityBaseline& baseline);
        void SetStatus(TerrainQualityStatus status, AZStd::string message = {});
        void WakeReadiness();
        void RefreshSceneSubscription();
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
        int GetTickOrder() override { return AZ::TICK_DEFAULT - 2; }
        void OnEntityActivated(const AZ::EntityId& entityId) override;
        void OnEntityDeactivated(const AZ::EntityId& entityId) override;
        void OnTerrainDataCreateEnd() override;
        void OnTerrainDataDestroyEnd() override;
        void OnTerrainDataChanged(
            const AZ::Aabb& dirtyRegion,
            AzFramework::Terrain::TerrainDataNotifications::TerrainDataChangedMask dataChangedMask) override;

        AzFramework::EntityContextId m_contextId{};
        AZ::EntityId m_ownerEntityId{};
        TerrainQualityConfig m_configuration;
        TerrainQualityBaseline m_preferredBaseline;
        TerrainQualityStatus m_status = TerrainQualityStatus::Disabled;
        AZStd::string m_statusDetail;
        bool m_hasPreferredBaseline = false;
        bool m_active = false;
        bool m_applyIssued = false;
        bool m_meshOverrideWasApplied = false;
        bool m_heightSettingsChanged = false;
        static constexpr AZ::u8 ReadinessAttempts = 8;
        AZ::u8 m_readinessAttemptsRemaining = 0;
        AzFramework::ISceneSystem::SceneEvent::Handler m_sceneEventHandler;
        AzFramework::Scene::SubsystemEvent::Handler m_sceneSubsystemEventHandler;
        AZStd::weak_ptr<AzFramework::Scene> m_observedScene;
    };
} // namespace TerrainCompositor
