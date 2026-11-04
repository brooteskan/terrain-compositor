#include <TerrainCompositor/Internal/TerrainCollision.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Physics/CharacterBus.h>
#include <AzFramework/Physics/RigidBodyBus.h>
#include <AzFramework/Physics/SystemBus.h>
#include <AzFramework/Terrain/TerrainDataRequestBus.h>
#include <Atom/RPI.Public/ViewportContext.h>
#include <Atom/RPI.Public/ViewportContextBus.h>
#include <LmbrCentral/Shape/BoxShapeComponentBus.h>
#include <LmbrCentral/Shape/ShapeComponentBus.h>
#include <Components/TerrainPhysicsColliderComponent.h>
#include <Source/HeightfieldColliderComponent.h>
#include <cmath>

namespace TerrainCompositor::Internal
{
    namespace
    {
        constexpr float TileWidth = 64.0f;
        constexpr size_t MaximumTiles = 1024;
        class CollisionHeightProvider final : public Terrain::TerrainPhysicsColliderComponent
        {
        public:
            AZ_CLASS_ALLOCATOR(CollisionHeightProvider, AZ::SystemAllocator);
            explicit CollisionHeightProvider(float spacing) : m_spacing(spacing) {}
            AZ::Vector2 GetHeightfieldGridSpacing() const override { return AZ::Vector2(m_spacing); }
        private:
            float m_spacing;
        };
    }

    CollisionCoverage TerrainCollisionCoverage(const AZ::Aabb& region,
        const AZ::Vector3& observer, float range, float spacing)
    {
        if (!region.IsValid() || !region.GetMin().IsFinite() || !region.GetMax().IsFinite() ||
            !std::isfinite(range) || range < 16 || range > 512 ||
            !std::isfinite(spacing) || spacing < .25f || spacing > 4)
            return {};
        const double width = std::ceil(double(TileWidth) / spacing) * spacing;
        if (!observer.IsFinite()) return {};
        const double minX = AZStd::max(double(region.GetMin().GetX()), double(observer.GetX()) - range);
        const double minY = AZStd::max(double(region.GetMin().GetY()), double(observer.GetY()) - range);
        const double maxX = AZStd::min(double(region.GetMax().GetX()), double(observer.GetX()) + range);
        const double maxY = AZStd::min(double(region.GetMax().GetY()), double(observer.GetY()) + range);
        if (minX >= maxX || minY >= maxY || std::abs(minX / width) > 1.e12 || std::abs(minY / width) > 1.e12 ||
            std::abs(maxX / width) > 1.e12 || std::abs(maxY / width) > 1.e12) return {};
        return {static_cast<AZ::s64>(std::floor(minX / width)),
            static_cast<AZ::s64>(std::floor(minY / width)),
            static_cast<AZ::s64>(std::ceil(maxX / width)) - 1,
            static_cast<AZ::s64>(std::ceil(maxY / width)) - 1};
    }

    CollisionTiles PlanTerrainCollisionTiles(const AZ::Aabb& region,
        const AZStd::vector<AZ::Vector3>& observers, float range, float spacing)
    {
        CollisionTiles result;
        if (!std::isfinite(spacing) || spacing < .25f || spacing > 4) return result;
        const double width = std::ceil(double(TileWidth) / spacing) * spacing;
        for (const auto& observer : observers)
        {
            const auto coverage = TerrainCollisionCoverage(region, observer, range, spacing);
            for (AZ::s64 y = coverage.m_firstY; y <= coverage.m_lastY; ++y)
                for (AZ::s64 x = coverage.m_firstX; x <= coverage.m_lastX; ++x)
                {
                    const double left = std::ceil(AZStd::max(double(region.GetMin().GetX()), x * width) / spacing) * spacing;
                    const double bottom = std::ceil(AZStd::max(double(region.GetMin().GetY()), y * width) / spacing) * spacing;
                    const double right = std::floor(AZStd::min(double(region.GetMax().GetX()), (x + 1) * width) / spacing) * spacing;
                    const double top = std::floor(AZStd::min(double(region.GetMax().GetY()), (y + 1) * width) / spacing) * spacing;
                    if (right <= left || top <= bottom) continue;
                    result.emplace(CollisionTileKey{x, y}, AZ::Aabb::CreateFromMinMax(
                        AZ::Vector3(float(left), float(bottom), region.GetMin().GetZ()),
                        AZ::Vector3(float(right), float(top), region.GetMax().GetZ())));
                    if (result.size() >= MaximumTiles) return result;
                }
        }
        return result;
    }

    TerrainCollision::~TerrainCollision() { Stop(); }
    void TerrainCollision::Start(AZ::EntityId region, float range, float spacing)
    {
        Stop();
        AZ::Entity* owner = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(owner, &AZ::ComponentApplicationRequests::FindEntity, region);
        // Retain explicitly authored colliders and their material/filter settings.
        if (owner && owner->FindComponent<PhysX::HeightfieldColliderComponent>()) return;
        m_region = region; m_range = range; m_spacing = spacing;
        AzFramework::EntityIdContextQueryBus::EventResult(m_context, region,
            &AzFramework::EntityIdContextQueries::GetOwningContextId);
        AZ::EntitySystemBus::Handler::BusConnect();
        LmbrCentral::ShapeComponentNotificationsBus::Handler::BusConnect(region);
        AzFramework::Terrain::TerrainDataNotificationBus::Handler::BusConnect();
        AZ::RPI::ViewportContextManagerNotificationsBus::Handler::BusConnect();
        if (auto* system = AZ::Interface<AzPhysics::SystemInterface>::Get()) system->RegisterSceneAddedEvent(m_sceneAdded);
        // Discover existing observers once; subsequent changes arrive through entity/physics events.
        AZ::ComponentApplicationBus::Broadcast(&AZ::ComponentApplicationRequests::EnumerateEntities,
            AZ::ComponentApplicationRequests::EntityCallback([this](AZ::Entity* entity)
            { if (entity->GetState() == AZ::Entity::State::Active) TrackEntity(entity->GetId()); }));
        BindViewport();
        ScheduleUpdate();
    }
    void TerrainCollision::Stop()
    {
        AZ::TickBus::Handler::BusDisconnect();
        AZ::EntitySystemBus::Handler::BusDisconnect();
        AZ::TransformNotificationBus::MultiHandler::BusDisconnect();
        Physics::RigidBodyNotificationBus::MultiHandler::BusDisconnect();
        Physics::CharacterNotificationBus::MultiHandler::BusDisconnect();
        LmbrCentral::ShapeComponentNotificationsBus::Handler::BusDisconnect();
        AzFramework::Terrain::TerrainDataNotificationBus::Handler::BusDisconnect();
        AZ::RPI::ViewportContextManagerNotificationsBus::Handler::BusDisconnect();
        m_cameraChanged.Disconnect(); m_sceneAdded.Disconnect(); m_viewport.reset();
        m_observers.clear(); m_bounds = AZ::Aabb::CreateNull(); m_hasCamera = false; m_cameraCoverage = {};
        // Collider destruction drains refresh jobs before the source provider disconnects.
        m_tiles.clear();
        AzFramework::EntityIdContextQueryBus::MultiHandler::BusDisconnect();
        m_region.SetInvalid();
        m_context = AzFramework::EntityContextId::CreateNull();
    }
    AZStd::unique_ptr<AZ::Entity> TerrainCollision::CreateTile(const AZ::Aabb& bounds)
    {
        auto tile = AZStd::make_unique<AZ::Entity>("Composed terrain collision");
        auto* transform = tile->CreateComponent<AzFramework::TransformComponent>();
        AZ::TransformConfig transformConfig;
        transformConfig.m_worldTransform = AZ::Transform::CreateTranslation(bounds.GetCenter());
        transformConfig.m_localTransform = transformConfig.m_worldTransform;
        transform->SetConfiguration(transformConfig);
        auto* shape = tile->CreateComponent(LmbrCentral::AxisAlignedBoxShapeComponentTypeId);
        if (!shape) return {};
        const LmbrCentral::BoxShapeConfig shapeConfig(bounds.GetExtents());
        if (!shape->SetConfiguration(shapeConfig)) return {};
        tile->AddComponent(aznew CollisionHeightProvider(m_spacing));
        auto* collider = tile->CreateComponent<PhysX::HeightfieldColliderComponent>();
        // Match the provider's scene so admitted mesh-height holes share its publication.
        AzFramework::EntityIdContextQueryBus::MultiHandler::BusConnect(tile->GetId());
        tile->Init(); m_creatingTile = true; tile->Activate(); m_creatingTile = false;
        // Finish cooking before the first gravity step, including at streaming boundaries.
        collider->BlockOnPendingJobs();
        return tile;
    }
    void TerrainCollision::ScheduleUpdate()
    {
        if (m_region.IsValid() && !AZ::TickBus::Handler::BusIsConnected()) AZ::TickBus::Handler::BusConnect();
    }
    void TerrainCollision::TrackEntity(AZ::EntityId id)
    {
        if (m_creatingTile || m_observers.contains(id) ||
            (!Physics::CharacterRequestBus::HasHandlers(id) && !Physics::RigidBodyRequestBus::HasHandlers(id))) return;
        AzFramework::EntityContextId context;
        AzFramework::EntityIdContextQueryBus::EventResult(context, id, &AzFramework::EntityIdContextQueries::GetOwningContextId);
        if (context != m_context) return;
        m_observers.emplace(id, Observer{});
        AZ::TransformNotificationBus::MultiHandler::BusConnect(id);
        Physics::RigidBodyNotificationBus::MultiHandler::BusConnect(id);
        Physics::CharacterNotificationBus::MultiHandler::BusConnect(id);
        SampleObserver(id);
    }
    void TerrainCollision::OnEntityDeactivated(const AZ::EntityId& id)
    {
        if (m_observers.erase(id))
        {
            AZ::TransformNotificationBus::MultiHandler::BusDisconnect(id);
            Physics::RigidBodyNotificationBus::MultiHandler::BusDisconnect(id);
            Physics::CharacterNotificationBus::MultiHandler::BusDisconnect(id);
            ScheduleUpdate();
        }
    }
    void TerrainCollision::OnCharacterDeactivated(const AZ::EntityId& id)
    {
        if (auto it = m_observers.find(id); it != m_observers.end())
        { it->second.m_positions.clear(); it->second.m_coverage.clear(); ScheduleUpdate(); }
    }
    void TerrainCollision::SampleObserver(AZ::EntityId id, const AZ::Transform* world)
    {
        const auto it = m_observers.find(id); if (it == m_observers.end()) return;
        auto& observer = it->second;
        observer.m_positions.clear();
        Physics::CharacterRequestBus::EnumerateHandlersId(id, [&observer,world](Physics::CharacterRequests* character)
        { if (character->IsPresent()) observer.m_positions.push_back(world ? world->GetTranslation() : character->GetBasePosition()); return true; });
        Physics::RigidBodyRequestBus::EnumerateHandlersId(id, [&observer,world](Physics::RigidBodyRequests* body)
        { observer.m_positions.push_back(world ? world->GetRotation().TransformVector(body->GetCenterOfMassLocal()) + world->GetTranslation() : body->GetCenterOfMassWorld()); return true; });
        AZStd::vector<CollisionCoverage> coverage;
        for (const auto& position : observer.m_positions)
            coverage.push_back(TerrainCollisionCoverage(m_bounds, position, m_range, m_spacing));
        if (coverage != observer.m_coverage) { observer.m_coverage = AZStd::move(coverage); ScheduleUpdate(); }
    }
    void TerrainCollision::OnTransformChanged(const AZ::Transform&, const AZ::Transform& world)
    {
        // The transform notification can precede the physics component's listener
        // during a teleport. Use the announced pose rather than its old body pose.
        if (auto* id = AZ::TransformNotificationBus::GetCurrentBusId()) SampleObserver(*id, &world);
    }
    void TerrainCollision::OnTerrainDataChanged(const AZ::Aabb&,
        AzFramework::Terrain::TerrainDataNotifications::TerrainDataChangedMask mask)
    {
        using Mask = AzFramework::Terrain::TerrainDataNotifications::TerrainDataChangedMask;
        if ((mask & Mask::Settings) != Mask::None) ScheduleUpdate();
    }
    void TerrainCollision::BindViewport()
    {
        auto* manager = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get();
        auto viewport = manager ? manager->GetDefaultViewportContext() : nullptr;
        if (viewport == m_viewport.lock()) return;
        m_cameraChanged.Disconnect(); m_viewport = viewport;
        if (viewport) viewport->ConnectViewMatrixChangedHandler(m_cameraChanged);
        SampleCamera();
    }
    void TerrainCollision::OnViewportContextRemoved(AzFramework::ViewportId id)
    {
        if (auto viewport = m_viewport.lock(); viewport && viewport->GetId() != id) return;
        m_cameraChanged.Disconnect(); m_viewport.reset(); SampleCamera();
    }
    void TerrainCollision::SampleCamera()
    {
        auto viewport = m_viewport.lock();
        const bool present = bool(viewport);
        if (viewport) m_cameraPosition = viewport->GetCameraTransform().GetTranslation();
        const auto coverage = present ? TerrainCollisionCoverage(m_bounds, m_cameraPosition, m_range, m_spacing) : CollisionCoverage{};
        if (m_hasCamera != present || coverage != m_cameraCoverage)
        { m_hasCamera = present; m_cameraCoverage = coverage; ScheduleUpdate(); }
    }
    void TerrainCollision::OnTick(float, AZ::ScriptTimePoint)
    {
        // One deferred update, before physics. Unchanged coverage has no standing tick.
        AZ::TickBus::Handler::BusDisconnect();
        // ProviderLifecycle retries context ownership before connecting runtime sources.
        if (m_context.IsNull()) return;
        AzPhysics::SceneHandle scene = AzPhysics::InvalidSceneHandle;
        Physics::DefaultWorldBus::BroadcastResult(scene, &Physics::DefaultWorldRequests::GetDefaultSceneHandle);
        if (scene == AzPhysics::InvalidSceneHandle) return;
        AZ::Aabb region = AZ::Aabb::CreateNull();
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            region, m_region, &LmbrCentral::ShapeComponentRequests::GetEncompassingAabb);
        auto heights = AzFramework::Terrain::FloatRange::CreateNull();
        AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
            heights, &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightBounds);
        if (!region.IsValid() || !heights.IsValid() || heights.m_max <= heights.m_min) return;
        region.SetMin(AZ::Vector3(region.GetMin().GetX(), region.GetMin().GetY(), heights.m_min));
        region.SetMax(AZ::Vector3(region.GetMax().GetX(), region.GetMax().GetY(), heights.m_max));
        m_bounds = region;
        AZStd::vector<AZ::Vector3> observers;
        for (auto& [id, observer] : m_observers)
        {
            observer.m_coverage.clear();
            for (const auto& position : observer.m_positions)
            {
                observers.push_back(position);
                observer.m_coverage.push_back(TerrainCollisionCoverage(region, position, m_range, m_spacing));
            }
        }
        if (m_hasCamera)
        {
            observers.push_back(m_cameraPosition);
            m_cameraCoverage = TerrainCollisionCoverage(region, m_cameraPosition, m_range, m_spacing);
        }
        const auto desired = PlanTerrainCollisionTiles(region, observers, m_range, m_spacing);
        // Fill new coverage before retiring old tiles. Stable keys avoid recooking while walking.
        for (const auto& [key, bounds] : desired)
        {
            AZ::Aabb current = AZ::Aabb::CreateNull();
            if (auto it = m_tiles.find(key); it != m_tiles.end())
                LmbrCentral::ShapeComponentRequestsBus::EventResult(current, it->second->GetId(),
                    &LmbrCentral::ShapeComponentRequests::GetEncompassingAabb);
            if (!current.IsValid() || !current.GetMin().IsClose(bounds.GetMin()) || !current.GetMax().IsClose(bounds.GetMax()))
                if (auto tile = CreateTile(bounds))
                {
                    AZ::EntityId old;
                    if (auto it = m_tiles.find(key); it != m_tiles.end()) old = it->second->GetId();
                    m_tiles.insert_or_assign(key, AZStd::move(tile));
                    if (old.IsValid()) AzFramework::EntityIdContextQueryBus::MultiHandler::BusDisconnect(old);
                }
        }
        for (auto it = m_tiles.begin(); it != m_tiles.end();)
            if (!desired.contains(it->first))
            {
                const auto old = it->second->GetId();
                it = m_tiles.erase(it);
                AzFramework::EntityIdContextQueryBus::MultiHandler::BusDisconnect(old);
            }
            else ++it;
    }
}
