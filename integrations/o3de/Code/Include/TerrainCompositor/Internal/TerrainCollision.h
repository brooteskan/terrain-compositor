#pragma once

#include <AzCore/Component/Entity.h>
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

    CollisionTiles PlanTerrainCollisionTiles(const AZ::Aabb& region,
        const AZStd::vector<AZ::Vector3>& observers, float range, float spacing);

    //! Owns bounded runtime heightfields; previews never start physics simulation.
    class TerrainCollision final : private AZ::TickBus::Handler,
        private AzFramework::EntityIdContextQueryBus::MultiHandler
    {
    public:
        ~TerrainCollision();
        void Start(AZ::EntityId region, float range, float spacing);
        void Stop();
        size_t GetTileCount() const { return m_tiles.size(); }
    private:
        void OnTick(float, AZ::ScriptTimePoint) override;
        int GetTickOrder() override { return AZ::TICK_PHYSICS_SYSTEM - 1; }
        AZStd::unique_ptr<AZ::Entity> CreateTile(const AZ::Aabb& bounds);
        AzFramework::EntityContextId GetOwningContextId() override { return m_context; }
        AZ::EntityId m_region;
        AzFramework::EntityContextId m_context;
        float m_range = 128.0f;
        float m_spacing = 0.5f;
        AZStd::map<CollisionTileKey, AZStd::unique_ptr<AZ::Entity>> m_tiles;
    };
}
