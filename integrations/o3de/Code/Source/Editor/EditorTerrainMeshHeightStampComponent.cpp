#include "EditorTerrainMeshHeightStampComponent.h"
#include "EditorPreviewStatus.h"

#include <AzCore/Math/Color.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzFramework/Terrain/TerrainDataRequestBus.h>
#include <TerrainCompositor/HeightmapStampIdentity.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>
#include <TerrainCompositor/TerrainMeshHeightStampSampling.h>

namespace TerrainCompositor
{
    void EditorTerrainMeshHeightStampComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection())
                TerrainMeshHeightStampConfig::Reflect(context);
            serialize->Class<EditorTerrainMeshHeightStampComponent, BaseClass>()->Version(1)->Field(
                "Configuration", &EditorTerrainMeshHeightStampComponent::m_configuration);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<EditorTerrainMeshHeightStampComponent>(
                        "Terrain Mesh Height Stamp", "Use a regular-grid terrain mesh descendant as composed terrain height.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Terrain")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &EditorTerrainMeshHeightStampComponent::m_configuration,
                        "Configuration",
                        "Terrain model, target composition, blending, and visibility.")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, &EditorTerrainMeshHeightStampComponent::OnConfigurationChanged)
                    ->UIElement(AZ::Edit::UIHandlers::Label, "Status", "Read-only mesh preparation and registration status.")
                    ->Attribute(AZ::Edit::Attributes::ValueText, &EditorTerrainMeshHeightStampComponent::GetStatusText);
            }
        }
    }

    void EditorTerrainMeshHeightStampComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        TerrainMeshHeightStampComponent::GetProvidedServices(services);
    }
    void EditorTerrainMeshHeightStampComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        TerrainMeshHeightStampComponent::GetIncompatibleServices(services);
    }
    void EditorTerrainMeshHeightStampComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        TerrainMeshHeightStampComponent::GetRequiredServices(services);
    }
    void EditorTerrainMeshHeightStampComponent::GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        TerrainMeshHeightStampComponent::GetDependentServices(services);
    }

    void EditorTerrainMeshHeightStampComponent::Activate()
    {
        BaseClass::Activate();
        m_preview = AZStd::make_unique<TerrainMeshHeightStampComponent>(m_configuration);
        m_preview->EditorActivate(GetEntityId());
        m_status = m_preview->GetStatusMessage();
        m_statusElapsed = 0.0f;
        AzFramework::EntityDebugDisplayEventBus::Handler::BusConnect(GetEntityId());
        AZ::TickBus::Handler::BusConnect();
    }

    void EditorTerrainMeshHeightStampComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        AzFramework::EntityDebugDisplayEventBus::Handler::BusDisconnect();
        if (m_preview)
        {
            m_preview->EditorDeactivate(GetEntityId());
            m_preview.reset();
        }
        m_status = "Inactive: no terrain mesh height contribution.";
        BaseClass::Deactivate();
    }

    AZ::TypeId EditorTerrainMeshHeightStampComponent::GetUnderlyingComponentType() const
    {
        return azrtti_typeid<TerrainMeshHeightStampComponent>();
    }

    void EditorTerrainMeshHeightStampComponent::SetExportData(const AZStd::string& key)
    {
        AZ_Assert(!m_preview, "Export data must only modify inactive conversion copies.");
        if (!m_preview)
            m_configuration.m_stableOrderKey = key;
    }

    void EditorTerrainMeshHeightStampComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        if (!IsValidStampOrderKey(m_configuration.m_stableOrderKey))
        {
            AZ_Error(
                "TerrainMeshHeightStampExport",
                false,
                "Missing baked mesh-height ordering identity. Enable the TG ordering processor before Editor info remover.");
            return;
        }
        gameEntity->CreateComponent<TerrainMeshHeightStampComponent>(m_configuration);
    }

    bool EditorTerrainMeshHeightStampComponent::ReadInConfig(const AZ::ComponentConfig* configuration)
    {
        if (const auto* stamp = azrtti_cast<const TerrainMeshHeightStampConfig*>(configuration))
        {
            m_configuration = *stamp;
            OnConfigurationChanged();
            return true;
        }
        return false;
    }

    bool EditorTerrainMeshHeightStampComponent::WriteOutConfig(AZ::ComponentConfig* configuration) const
    {
        if (auto* stamp = azrtti_cast<TerrainMeshHeightStampConfig*>(configuration))
        {
            *stamp = m_configuration;
            return true;
        }
        return false;
    }

    AZ::u32 EditorTerrainMeshHeightStampComponent::OnConfigurationChanged()
    {
        if (m_preview)
        {
            m_preview->SetStampConfiguration(m_configuration);
            m_status = m_preview->GetStatusMessage();
        }
        return AZ::Edit::PropertyRefreshLevels::AttributesAndValues;
    }

    void EditorTerrainMeshHeightStampComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        PollEditorPreviewStatus(*this, m_preview.get(), deltaTime, m_statusElapsed, m_status);
    }

    void EditorTerrainMeshHeightStampComponent::DisplayEntityViewport(
        [[maybe_unused]] const AzFramework::ViewportInfo& viewportInfo, AzFramework::DebugDisplayRequests& display)
    {
        if (!IsSelected() || !m_preview)
        {
            return;
        }

        const TerrainMeshHeightStampRegistrationData registration = m_preview->GetDiagnosticRegistration();
        const auto& mesh = registration.m_mesh;
        if (mesh.m_status == TerrainMeshHeightDataStatus::InvalidGeometry)
        {
            const auto& diagnostics = mesh.m_diagnostics;
            const bool hasGrid = diagnostics.m_gridWidth >= 2 && diagnostics.m_gridHeight >= 2 &&
                diagnostics.m_gridSpacing.IsFinite() && diagnostics.m_localBounds.IsValid();
            display.SetColor(AZ::Color(1.0f, 0.05f, 0.9f, 1.0f));
            for (const TerrainMeshHeightDiagnosticDetail& detail : diagnostics.m_details)
            {
                AZ::Vector3 worldPosition = registration.m_worldTransform.GetTranslation();
                if (hasGrid && detail.m_gridX != TerrainMeshHeightInvalidDiagnosticIndex &&
                    detail.m_gridY != TerrainMeshHeightInvalidDiagnosticIndex)
                {
                    const bool pointDiagnostic = detail.m_validation == TerrainMeshHeightValidation::MissingGridPoint ||
                        detail.m_validation == TerrainMeshHeightValidation::ConflictingHeight;
                    const float offset = pointDiagnostic ? 0.0f : 0.5f;
                    const AZ::Vector3 localPosition(
                        diagnostics.m_localOrigin.GetX() + (detail.m_gridX + offset) * diagnostics.m_gridSpacing.GetX(),
                        diagnostics.m_localOrigin.GetY() + (detail.m_gridY + offset) * diagnostics.m_gridSpacing.GetY(),
                        diagnostics.m_localBounds.GetCenter().GetZ());
                    worldPosition = registration.m_worldTransform.TransformPoint(localPosition);
                }
                display.DrawPoint(worldPosition, 12);
                const AZStd::string detailText = AZStd::string::format(
                    "%s; grid (%u, %u), triangle %s%u",
                    GetTerrainMeshHeightValidationMessage(detail.m_validation), detail.m_gridX, detail.m_gridY,
                    detail.m_triangleIndex == TerrainMeshHeightInvalidDiagnosticIndex ? "n/a " : "",
                    detail.m_triangleIndex == TerrainMeshHeightInvalidDiagnosticIndex ? 0 : detail.m_triangleIndex);
                display.DrawTextLabel(worldPosition, 0.8f, detailText.c_str());
            }
            display.DrawTextLabel(
                registration.m_worldTransform.GetTranslation(), 1.0f,
                "Rejected mesh: magenta markers identify retained validation cells/triangles.");
            return;
        }

        PreparedTerrainMeshHeightStamp prepared;
        if (PrepareTerrainMeshHeightStamp(registration, registration.m_hasNonUniformScale, prepared) !=
                TerrainMeshHeightStampPlacementValidation::Valid ||
            !prepared.m_data)
        {
            display.DrawTextLabel(registration.m_worldTransform.GetTranslation(), 1.0f, m_status.c_str());
            return;
        }

        constexpr size_t MaximumDisplayedAuthoredCells = 4096;
        const size_t cellWidth = prepared.m_data->m_width - 1;
        const size_t cellHeight = prepared.m_data->m_height - 1;
        const size_t cellStride = CalculateTerrainMeshHeightDiagnosticStride(
            cellWidth, cellHeight, MaximumDisplayedAuthoredCells);

        const auto worldPoint = [&prepared](float localX, float localY, float localZ)
        {
            const double worldX = prepared.m_originX + prepared.m_scale *
                (prepared.m_cosYaw * localX - prepared.m_sinYaw * localY);
            const double worldY = prepared.m_originY + prepared.m_scale *
                (prepared.m_sinYaw * localX + prepared.m_cosYaw * localY);
            return AZ::Vector3(
                aznumeric_cast<float>(worldX), aznumeric_cast<float>(worldY),
                aznumeric_cast<float>(prepared.m_heightOrigin + prepared.m_scale * localZ + 0.06));
        };

        display.DepthWriteOff();
        for (size_t y = 0; y < cellHeight; y += cellStride)
        {
            for (size_t x = 0; x < cellWidth; x += cellStride)
            {
                const size_t cell = y * cellWidth + x;
                const bool uncovered = IsTerrainMeshHeightCellUncovered(*prepared.m_data, cell);
                const bool removal = uncovered &&
                    prepared.m_uncoveredAreaPolicy == TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain &&
                    (prepared.m_affectTerrainRendering || prepared.m_affectTerrainCollisionQueries);
                display.SetColor(removal ? AZ::Color(1.0f, 0.08f, 0.08f, 0.34f)
                    : uncovered ? AZ::Color(0.55f, 0.55f, 0.55f, 0.28f)
                                : AZ::Color(0.08f, 0.9f, 0.2f, 0.12f));
                const float x0 = prepared.m_data->m_localOrigin.GetX() + x * prepared.m_data->m_gridSpacing.GetX();
                const float x1 = x0 + prepared.m_data->m_gridSpacing.GetX();
                const float y0 = prepared.m_data->m_localOrigin.GetY() + y * prepared.m_data->m_gridSpacing.GetY();
                const float y1 = y0 + prepared.m_data->m_gridSpacing.GetY();
                const size_t row = prepared.m_data->m_width;
                display.DrawQuad(
                    worldPoint(x0, y0, prepared.m_data->m_localHeights[y * row + x]),
                    worldPoint(x1, y0, prepared.m_data->m_localHeights[y * row + x + 1]),
                    worldPoint(x1, y1, prepared.m_data->m_localHeights[(y + 1) * row + x + 1]),
                    worldPoint(x0, y1, prepared.m_data->m_localHeights[(y + 1) * row + x]));
            }
        }

        float collisionGridSpacing = 0.0f;
        AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
            collisionGridSpacing, &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightQueryResolution);
        AZ::Aabb regionBounds = AZ::Aabb::CreateNull();
        TerrainCompositionRequestBus::EventResult(
            regionBounds,
            TerrainCompositionAddress{ registration.m_contextId, registration.m_configuration.m_targetCompositionEntityId },
            &TerrainCompositionRequestBus::Events::GetTargetRegionBounds);
        PreparedTerrainMeshHeightGap gap;
        if (PrepareTerrainMeshHeightGap(prepared, collisionGridSpacing, regionBounds, gap) && gap.m_collisionCells)
        {
            constexpr size_t MaximumDisplayedCollisionCells = 2048;
            const size_t collisionStride = AZStd::max<size_t>(
                1, (gap.m_collisionCells->m_cells.size() + MaximumDisplayedCollisionCells - 1) / MaximumDisplayedCollisionCells);
            AZStd::vector<AZ::Vector3> lines;
            lines.reserve(AZStd::min(MaximumDisplayedCollisionCells, gap.m_collisionCells->m_cells.size()) * 8);
            for (size_t index = 0; index < gap.m_collisionCells->m_cells.size(); index += collisionStride)
            {
                const auto& cell = gap.m_collisionCells->m_cells[index];
                const float x0 = aznumeric_cast<float>(cell.m_x * collisionGridSpacing);
                const float y0 = aznumeric_cast<float>(cell.m_y * collisionGridSpacing);
                const float x1 = x0 + collisionGridSpacing;
                const float y1 = y0 + collisionGridSpacing;
                float height = 0.0f;
                AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
                    height, &AzFramework::Terrain::TerrainDataRequests::GetHeightFromFloats,
                    0.5f * (x0 + x1), 0.5f * (y0 + y1),
                    AzFramework::Terrain::TerrainDataRequests::Sampler::EXACT, nullptr);
                height += 0.12f;
                lines.insert(lines.end(), {
                    { x0, y0, height }, { x1, y0, height }, { x1, y0, height }, { x1, y1, height },
                    { x1, y1, height }, { x0, y1, height }, { x0, y1, height }, { x0, y0, height } });
            }
            display.DrawLines(lines, AZ::Color(0.08f, 0.55f, 1.0f, 0.9f));
        }
        display.DepthWriteOn();
        display.DrawTextLabel(
            prepared.m_worldBounds.GetCenter(), 1.0f,
            "Mesh-height cells: green covered, gray neutral gap, red removal, blue conservative collision.");
    }
} // namespace TerrainCompositor
