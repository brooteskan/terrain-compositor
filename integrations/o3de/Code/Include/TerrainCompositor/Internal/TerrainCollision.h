#pragma once

#include <AzCore/Component/Entity.h>
#include <AzCore/Component/EntityBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzFramework/Physics/RigidBodyBus.h>
#include <AzFramework/Physics/CharacterBus.h>
#include <AzFramework/Physics/PhysicsSystem.h>
#include <AzFramework/Terrain/TerrainDataRequestBus.h>
#include <Atom/RPI.Public/ViewportContext.h>
#include <Atom/RPI.Public/ViewportContextBus.h>
#include <LmbrCentral/Shape/ShapeComponentBus.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/std/containers/map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzFramework/Entity/EntityContextBus.h>

namespace TerrainCompositor::Internal
{
    using CollisionTileKey = AZStd::pair<AZ::s64, AZ::s64>;
    using CollisionTiles = AZStd::map<CollisionTileKey, AZ::Aabb>;

    struct CollisionCoverage
    {
        AZ::s64 m_firstX = 0, m_firstY = 0, m_lastX = -1, m_lastY = -1;
        bool operator==(const CollisionCoverage&) const = default;
    };
    CollisionCoverage TerrainCollisionCoverage(const AZ::Aabb& region,
        const AZ::Vector3& observer, float range, float spacing);

    CollisionTiles PlanTerrainCollisionTiles(const AZ::Aabb& region,
        const AZStd::vector<AZ::Vector3>& observers, float range, float spacing);

    //! Owns bounded runtime heightfields; previews never start physics simulation.
    class TerrainCollision final : private AZ::TickBus::Handler,
        private AzFramework::EntityIdContextQueryBus::MultiHandler,
        private AZ::EntitySystemBus::Handler,
        private AZ::TransformNotificationBus::MultiHandler,
        private Physics::RigidBodyNotificationBus::MultiHandler,
        private Physics::CharacterNotificationBus::MultiHandler,
        private LmbrCentral::ShapeComponentNotificationsBus::Handler,
        private AzFramework::Terrain::TerrainDataNotificationBus::Handler,
        private AZ::RPI::ViewportContextManagerNotificationsBus::Handler
    {
    public:
        ~TerrainCollision();
        void Start(AZ::EntityId region, float range, float spacing);
        void Stop();
        size_t GetTileCount() const { return m_tiles.size(); }
    private:
        void ScheduleUpdate();
        void TrackEntity(AZ::EntityId id);
        void SampleObserver(AZ::EntityId id, const AZ::Transform* world = nullptr);
        void BindViewport();
        void SampleCamera();
        void OnEntityActivated(const AZ::EntityId& id) override { TrackEntity(id); }
        void OnEntityDeactivated(const AZ::EntityId& id) override;
        void OnTransformChanged(const AZ::Transform&, const AZ::Transform&) override;
        void OnPhysicsEnabled(const AZ::EntityId& id) override { SampleObserver(id); }
        void OnPhysicsDisabled(const AZ::EntityId& id) override { SampleObserver(id); }
        void OnCharacterActivated(const AZ::EntityId& id) override { SampleObserver(id); }
        void OnCharacterDeactivated(const AZ::EntityId& id) override;
        void OnShapeChanged(LmbrCentral::ShapeComponentNotifications::ShapeChangeReasons) override { ScheduleUpdate(); }
        void OnTerrainDataCreateEnd() override { ScheduleUpdate(); }
        void OnTerrainDataChanged(const AZ::Aabb&,
            AzFramework::Terrain::TerrainDataNotifications::TerrainDataChangedMask mask) override;
        void OnViewportContextAdded(AZ::RPI::ViewportContextPtr) override { BindViewport(); }
        void OnViewportContextRemoved(AzFramework::ViewportId id) override;
        void OnTick(float, AZ::ScriptTimePoint) override;
        int GetTickOrder() override { return AZ::TICK_PHYSICS_SYSTEM - 1; }
        AZStd::unique_ptr<AZ::Entity> CreateTile(const AZ::Aabb& bounds);
        AzFramework::EntityContextId GetOwningContextId() override { return m_context; }
        struct Observer
        {
            AZStd::vector<AZ::Vector3> m_positions;
            AZStd::vector<CollisionCoverage> m_coverage;
        };
        AZStd::map<AZ::EntityId, Observer> m_observers;
        AZ::Aabb m_bounds = AZ::Aabb::CreateNull();
        AZStd::weak_ptr<AZ::RPI::ViewportContext> m_viewport;
        AZ::RPI::MatrixChangedEvent::Handler m_cameraChanged{[this](const AZ::Matrix4x4&) { SampleCamera(); }};
        AzPhysics::SystemEvents::OnSceneAddedEvent::Handler m_sceneAdded{[this](AzPhysics::SceneHandle) { ScheduleUpdate(); }};
        AZ::Vector3 m_cameraPosition = AZ::Vector3::CreateZero();
        CollisionCoverage m_cameraCoverage;
        bool m_hasCamera = false, m_creatingTile = false;
        AZ::EntityId m_region;
        AzFramework::EntityContextId m_context;
        float m_range = 128.0f;
        float m_spacing = 0.5f;
        AZStd::map<CollisionTileKey, AZStd::unique_ptr<AZ::Entity>> m_tiles;
    };
}
