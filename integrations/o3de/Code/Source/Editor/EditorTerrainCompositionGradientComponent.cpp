#include "EditorTerrainCompositionGradientComponent.h"
#include "EditorPreviewStatus.h"

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

namespace TerrainCompositor
{
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
        // Config reflection is idempotent; runtime owns its removal when descriptors are unreflected.
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection()) { TerrainCompositionConfig::Reflect(context); }
            serialize->Class<EditorTerrainCompositionGradientComponent, BaseClass>()
                ->Version(1)
                ->Field("Configuration", &EditorTerrainCompositionGradientComponent::m_configuration);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<EditorTerrainCompositionGradientComponent>("Terrain Composition Gradient",
                    "One height source combining procedural ground and scoped heightmap stamps. Use a separate entity from Terrain Region.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Terrain")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorTerrainCompositionGradientComponent::m_configuration,
                        "Configuration", "References to the existing procedural source and target terrain region.")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, &EditorTerrainCompositionGradientComponent::OnConfigurationChanged)
                    ->UIElement(AZ::Edit::UIHandlers::Label, "Status", "Read-only composition preview status.")
                    ->Attribute(AZ::Edit::Attributes::ValueText, &EditorTerrainCompositionGradientComponent::GetStatusText);
            }
        }
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
        m_preview = AZStd::make_unique<TerrainCompositionGradientComponent>(m_configuration);
        const TerrainQualityBaseline baseline = CaptureTerrainQualityBaseline(GetEntityId());
        m_preview->SetTerrainQualityBaseline(baseline);
        m_preview->EditorActivate(GetEntityId());
        m_status = m_preview->GetStatusMessage();
        m_statusElapsed = 0.0f;
        AzFramework::EntityDebugDisplayEventBus::Handler::BusConnect(GetEntityId());
        AZ::TickBus::Handler::BusConnect();
    }

    void EditorTerrainCompositionGradientComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        AzFramework::EntityDebugDisplayEventBus::Handler::BusDisconnect();
        if (m_preview)
        {
            m_preview->EditorDeactivate(GetEntityId());
            m_preview.reset();
        }
        m_status = "Inactive: no composed gradient.";
        BaseClass::Deactivate();
    }

    AZ::TypeId EditorTerrainCompositionGradientComponent::GetUnderlyingComponentType() const
    { return azrtti_typeid<TerrainCompositionGradientComponent>(); }

    void EditorTerrainCompositionGradientComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        // Entity references remain serialized EntityId fields and are remapped by prefab conversion.
        gameEntity->CreateComponent<TerrainCompositionGradientComponent>(m_configuration);
    }

    bool EditorTerrainCompositionGradientComponent::ReadInConfig(const AZ::ComponentConfig* configuration)
    {
        if (const auto* composition = azrtti_cast<const TerrainCompositionConfig*>(configuration))
        {
            m_configuration = *composition;
            OnConfigurationChanged();
            return true;
        }
        return false;
    }

    bool EditorTerrainCompositionGradientComponent::WriteOutConfig(AZ::ComponentConfig* configuration) const
    {
        if (auto* composition = azrtti_cast<TerrainCompositionConfig*>(configuration))
        {
            *composition = m_configuration;
            return true;
        }
        return false;
    }

    AZ::u32 EditorTerrainCompositionGradientComponent::OnConfigurationChanged()
    {
        if (m_preview)
        {
            // Publish one replacement source/region state without tearing down the composition session.
            m_preview->ReadInConfig(&m_configuration);
            m_status = m_preview->GetStatusMessage();
        }
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }

    void EditorTerrainCompositionGradientComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        PollEditorPreviewStatus(*this, m_preview.get(), deltaTime, m_statusElapsed, m_status);
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
