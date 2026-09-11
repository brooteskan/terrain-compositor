#include "EditorHeightmapStampComponent.h"
#include "../ComponentConfiguration.h"

#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Math/Color.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>
#include <algorithm>
#include <cmath>

namespace TerrainCompositor
{
    void EditorHeightmapStampComponent::Reflect(AZ::ReflectContext* context)
    {
        // Ensure shared config metadata exists even when this descriptor is reflected first.
        // Runtime owns its unreflection; removing the editor descriptor must not remove shared config data.
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection()) { HeightmapStampConfig::Reflect(context); }
            serialize->Class<EditorHeightmapStampComponent, BaseClass>()
                ->Version(1)
                ->Field("Configuration", &EditorHeightmapStampComponent::m_configuration);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<EditorHeightmapStampComponent>("Heightmap Stamp",
                    "Place a yaw-only heightmap over procedural ground using feathered Replace blending.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Terrain")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorHeightmapStampComponent::m_configuration,
                        "Configuration", "Local-meter controls. XYZ, yaw and positive uniform scale come from Transform.")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, &EditorHeightmapStampComponent::OnConfigurationChanged)
                    ->UIElement(AZ::Edit::UIHandlers::Label, "Status", "Read-only preview status; invalid or unavailable stamps contribute nothing.")
                    ->Attribute(AZ::Edit::Attributes::ValueText, &EditorHeightmapStampComponent::GetStatusText);
            }
        }
    }

    void EditorHeightmapStampComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    { HeightmapStampComponent::GetProvidedServices(services); }
    void EditorHeightmapStampComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    { HeightmapStampComponent::GetIncompatibleServices(services); }
    void EditorHeightmapStampComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    { HeightmapStampComponent::GetRequiredServices(services); }
    void EditorHeightmapStampComponent::GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    { HeightmapStampComponent::GetDependentServices(services); }

    void EditorHeightmapStampComponent::Activate()
    {
        BaseClass::Activate();
        m_preview.Activate(m_configuration);
        AzFramework::EntityDebugDisplayEventBus::Handler::BusConnect(GetEntityId());
    }

    void EditorHeightmapStampComponent::Deactivate()
    {
        AzFramework::EntityDebugDisplayEventBus::Handler::BusDisconnect();
        m_preview.Deactivate();
        BaseClass::Deactivate();
    }

    AZ::TypeId EditorHeightmapStampComponent::GetUnderlyingComponentType() const
    { return azrtti_typeid<HeightmapStampComponent>(); }

    void EditorHeightmapStampComponent::SetExportOrderKey(const AZStd::string& key)
    {
        AZ_Assert(!m_preview, "Ordering export must only modify inactive conversion copies.");
        if (!m_preview) { m_configuration.m_stableOrderKey = key; }
    }

    void EditorHeightmapStampComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        // The prefab processor already baked the exact alias path on this conversion copy. Never use
        // the live editor mapper here; normal conversion remaps all EntityId fields after this step.
        if (!IsValidStampOrderKey(m_configuration.m_stableOrderKey))
        {
            AZ_Error("HeightmapStampExport", false,
                "Missing baked stamp identity. Enable the TG stamp ordering processor before Editor info remover.");
            return;
        }
        gameEntity->CreateComponent<HeightmapStampComponent>(m_configuration);
    }

    bool EditorHeightmapStampComponent::ReadInConfig(const AZ::ComponentConfig* configuration)
    {
        return Internal::ReadConfiguration<HeightmapStampConfig>(configuration, [this](const auto& value)
        {
            m_configuration = value;
            OnConfigurationChanged();
        });
    }

    bool EditorHeightmapStampComponent::WriteOutConfig(AZ::ComponentConfig* configuration) const
    {
        return Internal::WriteConfiguration(configuration, m_configuration);
    }

    AZ::u32 EditorHeightmapStampComponent::OnConfigurationChanged()
    {
        m_preview.Refresh(m_configuration);
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }

    void EditorHeightmapStampComponent::DisplayEntityViewport(
        [[maybe_unused]] const AzFramework::ViewportInfo& viewportInfo, AzFramework::DebugDisplayRequests& display)
    {
        if (!IsSelected()) { return; }
        HeightmapStampRegistrationData registration;
        registration.m_configuration = m_configuration;
        registration.m_transformAvailable = AZ::TransformBus::HasHandlers(GetEntityId());
        if (registration.m_transformAvailable)
        {
            AZ::TransformBus::EventResult(registration.m_worldTransform, GetEntityId(), &AZ::TransformBus::Events::GetWorldTM);
        }
        PreparedHeightmapStamp prepared;
        const auto validation = PrepareHeightmapStamp(registration,
            AZ::NonUniformScaleRequestBus::HasHandlers(GetEntityId()), prepared);
        const auto origin = registration.m_worldTransform.GetTranslation();
        if (!origin.IsFinite()) { return; }
        if (validation != HeightmapStampValidation::Valid)
        {
            display.DrawTextLabel(origin, 1.0f, GetHeightmapStampValidationMessage(validation));
            return; // Never draw a plausible yaw-only footprint for a rejected tilted transform.
        }

        // Use the sampler's normalized yaw and scale, at the entity's authoring Z (not terrain height).
        const auto& placement = prepared.m_placement;
        const auto point = [&](double x, double y)
        {
            return AZ::Vector3(
                static_cast<float>(placement.m_centerX + (placement.m_cosYaw * x - placement.m_sinYaw * y) / placement.m_inverseScale),
                static_cast<float>(placement.m_centerY + (placement.m_sinYaw * x + placement.m_cosYaw * y) / placement.m_inverseScale),
                origin.GetZ());
        };
        const auto rectangle = [&](double width, double depth, const AZ::Color& color)
        {
            const AZStd::vector<AZ::Vector3> lines{
                point(-width, -depth), point(width, -depth), point(width, -depth), point(width, depth),
                point(width, depth), point(-width, depth), point(-width, depth), point(-width, -depth) };
            display.DrawLines(lines, color);
        };
        rectangle(placement.m_halfWidth, placement.m_halfDepth, AZ::Color(0.2f, 0.8f, 1.0f, 1.0f));
        if (placement.m_edgeInset > 0.0)
        {
            rectangle(std::max(0.0, placement.m_halfWidth - placement.m_edgeInset),
                std::max(0.0, placement.m_halfDepth - placement.m_edgeInset), AZ::Color(1.0f, 0.4f, 0.8f, 1.0f));
        }
        if (prepared.m_feather > 0.0)
        {
            rectangle(std::max(0.0, placement.m_halfWidth - placement.m_edgeInset - prepared.m_feather),
                std::max(0.0, placement.m_halfDepth - placement.m_edgeInset - prepared.m_feather), AZ::Color(1.0f, 0.8f, 0.2f, 1.0f));
        }
        const double cue = 0.2 * std::min(placement.m_halfWidth, placement.m_halfDepth);
        const AZStd::vector<AZ::Vector3> arrows{
            point(0.0, 0.0), point(0.0, cue),
            point(-cue * 0.25, cue * 0.65), point(0.0, cue),
            point(cue * 0.25, cue * 0.65), point(0.0, cue),
            point(0.0, 0.0), point(cue, 0.0) };
        display.DrawLines(arrows, AZ::Color(0.2f, 1.0f, 0.4f, 1.0f));
        display.DrawTextLabel(point(0.0, cue), 1.0f, "Image top (+Y)");
        display.DrawTextLabel(point(cue, 0.0), 1.0f, "Image right (+X)");

        if (!m_configuration.m_holeMask.m_maskAsset.GetId().IsValid())
        {
            return;
        }
        AzFramework::EntityContextId context{};
        AzFramework::EntityIdContextQueryBus::EventResult(
            context, GetEntityId(), &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
        AZStd::vector<HeightmapStampRegistrationData> registrations;
        TerrainCompositionRequestBus::EventResult(
            registrations, TerrainCompositionAddress{ context, m_configuration.m_targetCompositionEntityId },
            &TerrainCompositionRequestBus::Events::GetRegisteredStamps);
        const auto found = std::find_if(registrations.begin(), registrations.end(), [this](const auto& candidate)
        {
            return candidate.m_stampEntityId == GetEntityId();
        });
        if (found == registrations.end())
        {
            return;
        }
        PreparedTerrainExistenceStamp existence;
        if (PrepareTerrainExistenceStamp(*found, AZ::NonUniformScaleRequestBus::HasHandlers(GetEntityId()), existence) !=
            TerrainExistenceStampValidation::Valid)
        {
            return;
        }

        // A compact sampled overlay communicates the actual thresholded local mask, including rotation and inset.
        constexpr size_t Grid = 20;
        display.DepthWriteOff();
        display.SetColor(existence.m_operation == TerrainExistenceOperation::RemoveTerrain
            ? AZ::Color(1.0f, 0.05f, 0.25f, 0.42f) : AZ::Color(0.1f, 1.0f, 0.35f, 0.42f));
        for (size_t y = 0; y < Grid; ++y)
        {
            for (size_t x = 0; x < Grid; ++x)
            {
                const double x0 = -existence.m_placement.m_halfWidth +
                    2.0 * existence.m_placement.m_halfWidth * double(x) / Grid;
                const double x1 = -existence.m_placement.m_halfWidth +
                    2.0 * existence.m_placement.m_halfWidth * double(x + 1) / Grid;
                const double y0 = -existence.m_placement.m_halfDepth +
                    2.0 * existence.m_placement.m_halfDepth * double(y) / Grid;
                const double y1 = -existence.m_placement.m_halfDepth +
                    2.0 * existence.m_placement.m_halfDepth * double(y + 1) / Grid;
                bool authoredExists = true;
                const auto center = point(0.5 * (x0 + x1), 0.5 * (y0 + y1));
                if (SampleTerrainExistenceStamp(center, existence, authoredExists))
                {
                    const float overlayZ = origin.GetZ() + 0.08f;
                    auto p0 = point(x0, y0); p0.SetZ(overlayZ);
                    auto p1 = point(x1, y0); p1.SetZ(overlayZ);
                    auto p2 = point(x1, y1); p2.SetZ(overlayZ);
                    auto p3 = point(x0, y1); p3.SetZ(overlayZ);
                    display.DrawQuad(p0, p1, p2, p3);
                }
            }
        }
        display.DepthWriteOn();
        display.DrawTextLabel(origin, 1.0f,
            existence.m_operation == TerrainExistenceOperation::RemoveTerrain
                ? "Stamp hole mask (red)" : "Stamp terrain restore mask (green)");
    }
} // namespace TerrainCompositor
