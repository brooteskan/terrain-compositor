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
            explicit CollisionHeightProvider(float spacing) : m_spacing(spacing) {}
            AZ::Vector2 GetHeightfieldGridSpacing() const override { return AZ::Vector2(m_spacing); }
        private:
            float m_spacing;
        };
    }

    CollisionTiles PlanTerrainCollisionTiles(const AZ::Aabb& region,
        const AZStd::vector<AZ::Vector3>& observers, float range, float spacing)
    {
        CollisionTiles result;
        if (!region.IsValid() || !region.GetMin().IsFinite() || !region.GetMax().IsFinite() ||
            !std::isfinite(range) || range < 16 || range > 512 ||
            !std::isfinite(spacing) || spacing < .25f || spacing > 4)
            return result;
        const double width = std::ceil(double(TileWidth) / spacing) * spacing;
        for (const auto& observer : observers)
        {
            if (!observer.IsFinite()) continue;
            const double minX = AZStd::max(double(region.GetMin().GetX()), double(observer.GetX()) - range);
            const double minY = AZStd::max(double(region.GetMin().GetY()), double(observer.GetY()) - range);
            const double maxX = AZStd::min(double(region.GetMax().GetX()), double(observer.GetX()) + range);
            const double maxY = AZStd::min(double(region.GetMax().GetY()), double(observer.GetY()) + range);
            if (minX >= maxX || minY >= maxY || std::abs(minX / width) > 1.e12 || std::abs(minY / width) > 1.e12 ||
                std::abs(maxX / width) > 1.e12 || std::abs(maxY / width) > 1.e12) continue;
            const auto firstX = static_cast<AZ::s64>(std::floor(minX / width));
            const auto firstY = static_cast<AZ::s64>(std::floor(minY / width));
            const auto lastX = static_cast<AZ::s64>(std::ceil(maxX / width)) - 1;
            const auto lastY = static_cast<AZ::s64>(std::ceil(maxY / width)) - 1;
            for (AZ::s64 y = firstY; y <= lastY; ++y)
                for (AZ::s64 x = firstX; x <= lastX; ++x)
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
        AZ::TickBus::Handler::BusConnect();
    }
    void TerrainCollision::Stop()
    {
        AZ::TickBus::Handler::BusDisconnect();
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
        tile->Init(); tile->Activate();
        // Finish cooking before the first gravity step, including at streaming boundaries.
        collider->BlockOnPendingJobs();
        return tile;
    }
    void TerrainCollision::OnTick(float, AZ::ScriptTimePoint)
    {
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
        AZStd::vector<AZ::Vector3> observers;
        Physics::CharacterRequestBus::EnumerateHandlers([&observers](Physics::CharacterRequests* character)
        {
            if (character->IsPresent()) observers.push_back(character->GetBasePosition());
            return true;
        });
        Physics::RigidBodyRequestBus::EnumerateHandlers([&observers](Physics::RigidBodyRequests* body)
        {
            observers.push_back(body->GetCenterOfMassWorld()); return true;
        });
        if (auto* viewports = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get())
            if (auto viewport = viewports->GetDefaultViewportContext())
                observers.push_back(viewport->GetCameraTransform().GetTranslation());
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
