#include "EditorTerrainMeshCutoutComponent.h"
#include "EditorPreviewStatus.h"

#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Math/Color.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzFramework/Terrain/TerrainDataRequestBus.h>
#include <TerrainCompositor/HeightmapStampIdentity.h>
#include <TerrainCompositor/TerrainMeshCutoutSampling.h>

namespace TerrainCompositor
{
    void EditorTerrainMeshCutoutComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection()) { TerrainMeshCutoutConfig::Reflect(context); }
            serialize->Class<EditorTerrainMeshCutoutComponent, BaseClass>()
                ->Version(1)
                ->Field("Configuration", &EditorTerrainMeshCutoutComponent::m_configuration);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<EditorTerrainMeshCutoutComponent>("Terrain Mesh Cutout",
                    "Remove or restore composed terrain where its final surface lies inside a closed static mesh volume.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Terrain")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorTerrainMeshCutoutComponent::m_configuration,
                        "Configuration", "Closed cutter model, component placement, target composition, operation, and margins.")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify,
                        &EditorTerrainMeshCutoutComponent::OnConfigurationChanged)
                    ->UIElement(AZ::Edit::UIHandlers::Label, "Status",
                        "Read-only validation and registration status; invalid cutters fail open.")
                    ->Attribute(AZ::Edit::Attributes::ValueText, &EditorTerrainMeshCutoutComponent::GetStatusText);
            }
        }
    }

    void EditorTerrainMeshCutoutComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainMeshCutoutComponent::GetProvidedServices(services); }
    void EditorTerrainMeshCutoutComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainMeshCutoutComponent::GetIncompatibleServices(services); }
    void EditorTerrainMeshCutoutComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainMeshCutoutComponent::GetRequiredServices(services); }
    void EditorTerrainMeshCutoutComponent::GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    { TerrainMeshCutoutComponent::GetDependentServices(services); }

    void EditorTerrainMeshCutoutComponent::Activate()
    {
        BaseClass::Activate();
        m_preview = AZStd::make_unique<TerrainMeshCutoutComponent>(m_configuration);
        m_preview->EditorActivate(GetEntityId());
        m_status = m_preview->GetStatusMessage();
        m_statusElapsed = 0.0f;
        AzFramework::EntityDebugDisplayEventBus::Handler::BusConnect(GetEntityId());
        AZ::TickBus::Handler::BusConnect();
    }

    void EditorTerrainMeshCutoutComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        AzFramework::EntityDebugDisplayEventBus::Handler::BusDisconnect();
        if (m_preview)
        {
            m_preview->EditorDeactivate(GetEntityId());
            m_preview.reset();
        }
        m_status = "Inactive: no mesh cutout contribution.";
        BaseClass::Deactivate();
    }

    AZ::TypeId EditorTerrainMeshCutoutComponent::GetUnderlyingComponentType() const
    { return azrtti_typeid<TerrainMeshCutoutComponent>(); }

    void EditorTerrainMeshCutoutComponent::SetExportData(const AZStd::string& key)
    {
        AZ_Assert(!m_preview, "Export data must only modify inactive conversion copies.");
        if (!m_preview)
        {
            m_configuration.m_stableOrderKey = key;
        }
    }

    void EditorTerrainMeshCutoutComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        if (!IsValidStampOrderKey(m_configuration.m_stableOrderKey))
        {
            AZ_Error("TerrainMeshCutoutExport", false,
                "Missing baked cutout ordering identity. Enable the TG ordering processor before Editor info remover.");
            return;
        }
        gameEntity->CreateComponent<TerrainMeshCutoutComponent>(m_configuration);
    }

    bool EditorTerrainMeshCutoutComponent::ReadInConfig(const AZ::ComponentConfig* configuration)
    {
        if (const auto* cutout = azrtti_cast<const TerrainMeshCutoutConfig*>(configuration))
        {
            m_configuration = *cutout;
            OnConfigurationChanged();
            return true;
        }
        return false;
    }

    bool EditorTerrainMeshCutoutComponent::WriteOutConfig(AZ::ComponentConfig* configuration) const
    {
        if (auto* cutout = azrtti_cast<TerrainMeshCutoutConfig*>(configuration))
        {
            *cutout = m_configuration;
            return true;
        }
        return false;
    }

    AZ::u32 EditorTerrainMeshCutoutComponent::OnConfigurationChanged()
    {
        if (m_preview)
        {
            m_preview->SetCutoutConfiguration(m_configuration);
            m_status = m_preview->GetStatusMessage();
        }
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }

    void EditorTerrainMeshCutoutComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        PollEditorPreviewStatus(*this, m_preview.get(), deltaTime, m_statusElapsed, m_status);
    }

    void EditorTerrainMeshCutoutComponent::DisplayEntityViewport(
        [[maybe_unused]] const AzFramework::ViewportInfo& viewportInfo, AzFramework::DebugDisplayRequests& display)
    {
        if (!IsSelected()) { return; }
        AzFramework::EntityContextId context{};
        AzFramework::EntityIdContextQueryBus::EventResult(
            context, GetEntityId(), &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
        AZStd::vector<TerrainMeshCutoutRegistrationData> registrations;
        TerrainCompositionRequestBus::EventResult(
            registrations, TerrainCompositionAddress{ context, m_configuration.m_targetCompositionEntityId },
            &TerrainCompositionRequestBus::Events::GetRegisteredMeshCutouts);
        const auto found = AZStd::find_if(registrations.begin(), registrations.end(), [this](const auto& candidate)
        {
            return candidate.m_cutoutEntityId == GetEntityId();
        });
        const AZ::EntityId componentEntityId = GetEntityId();
        AZ::Vector3 labelPosition = m_preview && AZ::TransformBus::HasHandlers(componentEntityId)
            ? [&]() { AZ::Transform transform = AZ::Transform::CreateIdentity();
                AZ::TransformBus::EventResult(transform, componentEntityId, &AZ::TransformBus::Events::GetWorldTM);
                return transform.GetTranslation(); }()
            : AZ::Vector3::CreateZero();
        if (found == registrations.end())
        {
            display.DrawTextLabel(labelPosition, 1.0f, m_status.c_str());
            return;
        }
        PreparedTerrainMeshCutout cutout;
        const auto validation = PrepareTerrainMeshCutout(
            *found, found->m_hasNonUniformScale, cutout);
        if (validation != TerrainMeshCutoutPlacementValidation::Valid)
        {
            display.DrawTextLabel(labelPosition, 1.0f, GetTerrainMeshCutoutPlacementValidationMessage(validation));
            return;
        }
        labelPosition = cutout.m_worldFromLocal.GetTranslation();

        constexpr size_t MaximumDisplayedTriangles = 5000;
        const size_t stride = AZStd::max<size_t>(1, cutout.m_data->m_triangles.size() / MaximumDisplayedTriangles);
        AZStd::vector<AZ::Vector3> lines;
        lines.reserve(AZStd::min(cutout.m_data->m_triangles.size(), MaximumDisplayedTriangles) * 6);
        for (size_t index = 0; index < cutout.m_data->m_triangles.size(); index += stride)
        {
            const auto& triangle = cutout.m_data->m_triangles[index];
            const AZ::Vector3 a = cutout.m_worldFromLocal.TransformPoint(triangle.m_a);
            const AZ::Vector3 b = cutout.m_worldFromLocal.TransformPoint(triangle.m_b);
            const AZ::Vector3 c = cutout.m_worldFromLocal.TransformPoint(triangle.m_c);
            lines.insert(lines.end(), { a, b, b, c, c, a });
        }
        display.DrawLines(lines, AZ::Color(0.95f, 0.5f, 0.1f, 0.9f));

        // Sample the same final terrain-height points as the runtime predicate, so vertical cutter moves
        // and height-stamp edits are visible as the actual affected footprint rather than an XY projection.
        constexpr size_t Grid = 24;
        display.DepthWriteOff();
        if (m_configuration.m_debugDrawCollisionMask)
        {
            float gridSpacing = 0.0f;
            AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
                gridSpacing, &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightQueryResolution);
            ApplyTerrainMeshCutoutCollisionCellPadding(cutout, gridSpacing);
        }
        const auto drawMask = [&](TerrainMeshCutoutConsumer consumer, const AZ::Color& color)
        {
            const AZ::Aabb& bounds = consumer == TerrainMeshCutoutConsumer::Rendering
                ? cutout.m_renderWorldBounds : cutout.m_collisionWorldBounds;
            const AZ::Vector3 minimum = bounds.GetMin();
            const AZ::Vector3 extents = bounds.GetExtents();
            display.SetColor(color);
            for (size_t y = 0; y < Grid; ++y)
            {
                for (size_t x = 0; x < Grid; ++x)
                {
                    const float x0 = minimum.GetX() + extents.GetX() * float(x) / Grid;
                    const float x1 = minimum.GetX() + extents.GetX() * float(x + 1) / Grid;
                    const float y0 = minimum.GetY() + extents.GetY() * float(y) / Grid;
                    const float y1 = minimum.GetY() + extents.GetY() * float(y + 1) / Grid;
                    float height = 0.0f;
                    AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
                        height, &AzFramework::Terrain::TerrainDataRequests::GetHeightFromFloats,
                        0.5f * (x0 + x1), 0.5f * (y0 + y1),
                        AzFramework::Terrain::TerrainDataRequests::Sampler::EXACT, nullptr);
                    if (SampleTerrainMeshCutout(
                        AZ::Vector3(0.5f * (x0 + x1), 0.5f * (y0 + y1), height), cutout, consumer))
                    {
                        constexpr float Lift = 0.08f;
                        display.DrawQuad(AZ::Vector3(x0, y0, height + Lift), AZ::Vector3(x1, y0, height + Lift),
                            AZ::Vector3(x1, y1, height + Lift), AZ::Vector3(x0, y1, height + Lift));
                    }
                }
            }
        };
        if (m_configuration.m_debugDrawRenderMask)
        {
            drawMask(TerrainMeshCutoutConsumer::Rendering, AZ::Color(1.0f, 0.05f, 0.15f, 0.35f));
        }
        if (m_configuration.m_debugDrawCollisionMask)
        {
            drawMask(TerrainMeshCutoutConsumer::CollisionQueries, AZ::Color(0.1f, 0.65f, 1.0f, 0.3f));
        }
        display.DepthWriteOn();
        display.DrawTextLabel(labelPosition, 1.0f,
            cutout.m_operation == TerrainExistenceOperation::RemoveTerrain
                ? "Mesh cutout volume / affected final surface" : "Mesh restore volume / affected final surface");
    }
} // namespace TerrainCompositor
