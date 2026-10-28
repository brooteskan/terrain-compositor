#include "EditorTerrainCompositionGradientComponent.h"
#include "../ComponentConfiguration.h"
#include "EditorConfiguration.h"

#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Math/Color.h>
#include <AzFramework/Terrain/TerrainDataRequestBus.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <Components/TerrainWorldRendererComponent.h>
#include <EditorComponents/EditorTerrainWorldRendererComponent.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>
#include <TerrainRenderer/TerrainFeatureProcessor.h>
#include <Atom/RPI.Public/Scene.h>

namespace TerrainCompositor
{
    class CollisionSurfaceRefreshBehaviorHandler final
        : public TerrainCollisionSurfaceRefreshNotificationBus::Handler, public AZ::BehaviorEBusHandler
    {
    public:
        AZ_EBUS_BEHAVIOR_BINDER(CollisionSurfaceRefreshBehaviorHandler, "{61CC5537-0391-47EE-A9BF-B2F502409990}",
            AZ::SystemAllocator, OnRefreshCollisionSurface);
        void OnRefreshCollisionSurface(AZ::EntityId composition) override { Call(FN_OnRefreshCollisionSurface, composition); }
    };
    class HeightPrecisionBehaviorHandler final
        : public TerrainHeightPrecisionNotificationBus::Handler, public AZ::BehaviorEBusHandler
    {
    public:
        AZ_EBUS_BEHAVIOR_BINDER(HeightPrecisionBehaviorHandler, "{5C931F20-9455-4C06-B7ED-58B2A54E8768}",
            AZ::SystemAllocator, OnHeightPrecisionChanged);
        void OnHeightPrecisionChanged() override { Call(FN_OnHeightPrecisionChanged); }
    };
    namespace
    {
        TerrainQualityBaseline CaptureTerrainQualityBaseline(AZ::EntityId ownerEntityId)
        {
            TerrainQualityBaseline baseline;
            AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
                baseline.m_heightQueryResolution,
                &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightQueryResolution);
            AzFramework::EntityContextId ownerContext{};
            AzFramework::EntityIdContextQueryBus::EventResult(
                ownerContext, ownerEntityId, &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
            AZ::ComponentApplicationBus::Broadcast(
                &AZ::ComponentApplicationRequests::EnumerateEntities,
                [&baseline, ownerContext](AZ::Entity* entity)
                {
                    if (!entity || baseline.m_meshSettingsAvailable)
                    {
                        return;
                    }
                    AzFramework::EntityContextId entityContext{};
                    AzFramework::EntityIdContextQueryBus::EventResult(
                        entityContext, entity->GetId(), &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
                    if (entityContext != ownerContext)
                    {
                        return;
                    }
                    for (AZ::Component* component : entity->GetComponents())
                    {
                        Terrain::TerrainWorldRendererConfig rendererConfig;
                        auto* runtimeRenderer = azrtti_cast<Terrain::TerrainWorldRendererComponent*>(component);
                        if (runtimeRenderer && runtimeRenderer->WriteOutConfig(&rendererConfig))
                        {
                            baseline.m_renderDistance = rendererConfig.m_meshConfig.m_renderDistance;
                            baseline.m_firstLodDistance = rendererConfig.m_meshConfig.m_firstLodDistance;
                            baseline.m_clodEnabled = rendererConfig.m_meshConfig.m_clodEnabled;
                            baseline.m_clodDistance = rendererConfig.m_meshConfig.m_clodDistance;
                            baseline.m_meshSettingsAvailable = true;
                            return;
                        }
                        if (auto* editorRenderer = azrtti_cast<Terrain::EditorTerrainWorldRendererComponent*>(component))
                        {
                            AZ::Entity runtimeCopy("Terrain renderer configuration probe");
                            editorRenderer->BuildGameEntity(&runtimeCopy);
                            for (AZ::Component* runtimeComponent : runtimeCopy.GetComponents())
                            {
                                auto* builtRenderer = azrtti_cast<Terrain::TerrainWorldRendererComponent*>(runtimeComponent);
                                if (builtRenderer && builtRenderer->WriteOutConfig(&rendererConfig))
                                {
                                    baseline.m_renderDistance = rendererConfig.m_meshConfig.m_renderDistance;
                                    baseline.m_firstLodDistance = rendererConfig.m_meshConfig.m_firstLodDistance;
                                    baseline.m_clodEnabled = rendererConfig.m_meshConfig.m_clodEnabled;
                                    baseline.m_clodDistance = rendererConfig.m_meshConfig.m_clodDistance;
                                    baseline.m_meshSettingsAvailable = true;
                                    return;
                                }
                            }
                        }
                    }
                });
            return baseline;
        }
    } // namespace

    void EditorTerrainCompositionGradientComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* behavior = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behavior->EBus<TerrainCollisionSurfaceRefreshNotificationBus>("TerrainCollisionSurfaceRefreshNotificationBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation)
                ->Attribute(AZ::Script::Attributes::Module, "editor")
                ->Handler<CollisionSurfaceRefreshBehaviorHandler>();
            behavior->EBus<TerrainCollisionSurfaceEditorRequestBus>("TerrainCollisionSurfaceEditorRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation)
                ->Attribute(AZ::Script::Attributes::Module, "editor")
                ->Event("RefreshCollisionSurface", &TerrainCollisionSurfaceEditorRequests::RefreshCollisionSurface)
                ->Event("SetRefreshStatus", &TerrainCollisionSurfaceEditorRequests::SetRefreshStatus)
                ->Event("GetRefreshStatus", &TerrainCollisionSurfaceEditorRequests::GetRefreshStatus)
                ->Event("IsRefreshInProgress", &TerrainCollisionSurfaceEditorRequests::IsRefreshInProgress);
            behavior->EBus<TerrainHeightPrecisionRequestBus>("TerrainHeightPrecisionRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation)
                ->Attribute(AZ::Script::Attributes::Module, "editor")
                ->Event("GetRenderedHeightPrecision", &TerrainHeightPrecisionRequests::GetRenderedHeightPrecision)
                ->Event("GetHeightVertexStride", &TerrainHeightPrecisionRequests::GetHeightVertexStride);
            behavior->EBus<TerrainHeightPrecisionNotificationBus>("TerrainHeightPrecisionNotificationBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation)
                ->Attribute(AZ::Script::Attributes::Module, "editor")
                ->Handler<HeightPrecisionBehaviorHandler>();
        }
        // Config reflection is idempotent; runtime owns its removal when descriptors are unreflected.
        Internal::ReflectEditorConfiguration<EditorTerrainCompositionGradientComponent, BaseClass>(context,
            &EditorTerrainCompositionGradientComponent::m_configuration, &EditorTerrainCompositionGradientComponent::OnConfigurationChanged,
            &EditorTerrainCompositionGradientComponent::GetStatusText, "Terrain Composition Gradient",
            "One height source combining procedural ground and scoped heightmap stamps. Use a separate entity from Terrain Region.",
            "References to the existing procedural source and target terrain region.",
            "Read-only composition preview status.");
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<EditorTerrainCompositionGradientComponent>("Terrain Composition Gradient", "")
                    ->UIElement(AZ::Edit::UIHandlers::Button, "Collision surface", "Rebuild the registered collision surface from this project's source terrains.")
                    ->Attribute(AZ::Edit::Attributes::ButtonText, "Refresh collision surface")
                    ->Attribute(AZ::Edit::Attributes::ReadOnly, &EditorTerrainCompositionGradientComponent::IsRefreshInProgress)
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, &EditorTerrainCompositionGradientComponent::RequestCollisionSurfaceRefresh)
                    ->UIElement(AZ::Edit::UIHandlers::Label, "Collision refresh", "Progress of the current collision surface refresh.")
                    ->Attribute(AZ::Edit::Attributes::ValueText, &EditorTerrainCompositionGradientComponent::GetRefreshStatus);
            }
        }
    }

    AZ::u32 EditorTerrainCompositionGradientComponent::RequestCollisionSurfaceRefresh()
    {
        RefreshCollisionSurface();
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }

    void EditorTerrainCompositionGradientComponent::RefreshCollisionSurface()
    {
        if (m_collisionRefreshBusy) return;
        if (!TerrainCollisionSurfaceRefreshNotificationBus::HasHandlers())
        {
            SetRefreshStatus("No collision surface refresher is registered for this project.", false);
            return;
        }
        SetRefreshStatus("Preparing collision surface...", true);
        TerrainCollisionSurfaceRefreshNotificationBus::Broadcast(
            &TerrainCollisionSurfaceRefreshNotifications::OnRefreshCollisionSurface, GetEntityId());
    }

    void EditorTerrainCompositionGradientComponent::SetRefreshStatus(const AZStd::string& status, bool busy)
    {
        m_collisionRefreshStatus = status;
        m_collisionRefreshBusy = busy;
        AzToolsFramework::ToolsApplicationNotificationBus::Broadcast(
            &AzToolsFramework::ToolsApplicationNotificationBus::Events::InvalidatePropertyDisplayForComponent,
            AZ::EntityComponentIdPair(GetEntityId(), GetId()), AzToolsFramework::Refresh_AttributesAndValues);
    }

    namespace
    {
        Terrain::TerrainFeatureProcessor* PrecisionRenderer(AZ::EntityId owner)
        {
            AzFramework::EntityContextId context;
            AzFramework::EntityIdContextQueryBus::EventResult(context, owner,
                &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
            auto* scene = AZ::RPI::Scene::GetSceneForEntityContextId(context);
            return scene ? scene->GetFeatureProcessor<Terrain::TerrainFeatureProcessor>() : nullptr;
        }
    }

    AZ::u32 EditorTerrainCompositionGradientComponent::GetRenderedHeightPrecision() const
    {
        auto* renderer = PrecisionRenderer(GetEntityId());
        return renderer ? renderer->GetHeightPrecision() : 0u;
    }

    AZ::u32 EditorTerrainCompositionGradientComponent::GetHeightVertexStride() const
    {
        auto* renderer = PrecisionRenderer(GetEntityId());
        return renderer ? renderer->GetHeightVertexStride() : 0u;
    }

    AZStd::string EditorTerrainCompositionGradientComponent::GetStatusText() const
    {
        const auto bits = GetRenderedHeightPrecision();
        auto result = m_preview.GetStatus();
        if (bits)
            result += AZStd::string::format(" GPU heights: R%u_FLOAT, %u-bit arithmetic, %u bytes/vertex (including normals).%s",
                bits, bits, GetHeightVertexStride(), bits != m_configuration.m_materialHeightPrecision ?
                    " Precision conflict: set all terrain compositors to the same precision." : "");
        return result;
    }

    void EditorTerrainCompositionGradientComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainCompositionGradientComponent::GetProvidedServices(services); }
    void EditorTerrainCompositionGradientComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainCompositionGradientComponent::GetIncompatibleServices(services); }
    void EditorTerrainCompositionGradientComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainCompositionGradientComponent::GetRequiredServices(services); }
    void EditorTerrainCompositionGradientComponent::GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainCompositionGradientComponent::GetDependentServices(services); }

    void EditorTerrainCompositionGradientComponent::Activate()
    {
        BaseClass::Activate();
        TerrainHeightPrecisionRequestBus::Handler::BusConnect(GetEntityId());
        TerrainCollisionSurfaceEditorRequestBus::Handler::BusConnect(GetEntityId());
        m_preview.Activate(m_configuration, [this](auto& preview)
        {
            const TerrainQualityBaseline baseline = CaptureTerrainQualityBaseline(GetEntityId());
            preview.SetTerrainQualityBaseline(baseline);
        });
        AzFramework::EntityDebugDisplayEventBus::Handler::BusConnect(GetEntityId());
    }

    void EditorTerrainCompositionGradientComponent::Deactivate()
    {
        TerrainCollisionSurfaceEditorRequestBus::Handler::BusDisconnect();
        if (m_collisionRefreshBusy)
            m_collisionRefreshStatus = "Refresh cancelled. Refresh again in edit mode.";
        m_collisionRefreshBusy = false;
        TerrainHeightPrecisionRequestBus::Handler::BusDisconnect();
        AzFramework::EntityDebugDisplayEventBus::Handler::BusDisconnect();
        m_preview.Deactivate();
        BaseClass::Deactivate();
    }

    AZ::TypeId EditorTerrainCompositionGradientComponent::GetUnderlyingComponentType() const
    {
        // This adapter owns its Configuration directly; it is not a generic
        // runtime-component wrapper. PropertyTreeEditor must use our layout.
        return azrtti_typeid<EditorTerrainCompositionGradientComponent>();
    }

    void EditorTerrainCompositionGradientComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        // Entity references remain serialized EntityId fields and are remapped by prefab conversion.
        gameEntity->CreateComponent<TerrainCompositionGradientComponent>(m_configuration);
    }

    bool EditorTerrainCompositionGradientComponent::ReadInConfig(const AZ::ComponentConfig* configuration)
    {
        return Internal::ReadConfiguration(configuration, m_configuration, [this] { OnConfigurationChanged(); });
    }

    bool EditorTerrainCompositionGradientComponent::WriteOutConfig(AZ::ComponentConfig* configuration) const
    {
        return Internal::WriteConfiguration(configuration, m_configuration);
    }

    AZ::u32 EditorTerrainCompositionGradientComponent::OnConfigurationChanged()
    {
        TerrainHeightPrecisionNotificationBus::Broadcast(&TerrainHeightPrecisionNotifications::OnHeightPrecisionChanged);
        return Internal::RefreshEditorConfiguration(m_preview, m_configuration);
    }

    void EditorTerrainCompositionGradientComponent::DisplayEntityViewport(
        [[maybe_unused]] const AzFramework::ViewportInfo& viewportInfo,
        AzFramework::DebugDisplayRequests& display)
    {
        if (!IsSelected()) { return; }
        AzFramework::EntityContextId context{};
        AzFramework::EntityIdContextQueryBus::EventResult(
            context, GetEntityId(), &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
        const TerrainCompositionAddress address{ context, GetEntityId() };
        AZ::Aabb regionBounds = AZ::Aabb::CreateNull();
        TerrainCompositionRequestBus::EventResult(
            regionBounds, address, &TerrainCompositionRequestBus::Events::GetTargetRegionBounds);
        if (!regionBounds.IsValid()) { return; }

        display.DepthWriteOff();
        display.SetColor(AZ::Color(1.0f, 0.0f, 0.12f, 0.52f));
        const auto renderFinalHoles = [&](const AZ::Aabb& bounds, size_t grid, bool shaded)
        {
            if (!bounds.IsValid()) { return; }
            const float width = bounds.GetXExtent() / float(grid);
            const float depth = bounds.GetYExtent() / float(grid);
            if (!(width > 0.0f) || !(depth > 0.0f)) { return; }
            for (size_t y = 0; y < grid; ++y)
            {
                for (size_t x = 0; x < grid; ++x)
                {
                    const float x0 = bounds.GetMin().GetX() + width * float(x);
                    const float y0 = bounds.GetMin().GetY() + depth * float(y);
                    const AZ::Vector3 center(x0 + width * 0.5f, y0 + depth * 0.5f, 0.0f);
                    bool exists = false;
                    float height = 0.0f;
                    TerrainCompositionHeightRequestBus::EventResult(
                        height, address, &TerrainCompositionHeightRequestBus::Events::GetHeight,
                        m_configuration.m_targetTerrainRegionEntityId, center, exists);
                    if (exists) { continue; }
                    const float z = height + 0.18f;
                    if (shaded)
                    {
                        display.DrawQuad(
                            AZ::Vector3(x0, y0, z), AZ::Vector3(x0 + width, y0, z),
                            AZ::Vector3(x0 + width, y0 + depth, z), AZ::Vector3(x0, y0 + depth, z));
                    }
                    else
                    {
                        display.DrawPoint(AZ::Vector3(center.GetX(), center.GetY(), z), 9);
                    }
                }
            }
        };

        // Sparse whole-region samples expose procedural holes; refined stamp-footprint samples show final overlap/restore results.
        renderFinalHoles(regionBounds, 32, false);
        AZStd::vector<HeightmapStampRegistrationData> registrations;
        TerrainCompositionRequestBus::EventResult(
            registrations, address, &TerrainCompositionRequestBus::Events::GetRegisteredStamps);
        for (const auto& registration : registrations)
        {
            PreparedTerrainExistenceStamp prepared;
            if (PrepareTerrainExistenceStamp(registration,
                AZ::NonUniformScaleRequestBus::HasHandlers(registration.m_stampEntityId), prepared) ==
                TerrainExistenceStampValidation::Valid)
            {
                renderFinalHoles(prepared.m_placement.m_worldBounds, 20, true);
            }
        }
        display.DepthWriteOn();
        display.DrawTextLabel(regionBounds.GetCenter(), 1.0f,
            "Final composed terrain holes (red sampled overlay)");
    }
} // namespace TerrainCompositor
