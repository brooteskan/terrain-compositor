#include <TerrainCompositor/Components/TerrainCompositionGradientComponent.h>
#include "../ComponentConfiguration.h"
#include "../PublicationState.h"

#include "TerrainCompositionQueryHelpers.h"

#include <Atom/RPI.Public/Scene.h>
#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/containers/array.h>
#include <AzFramework/Entity/EntityContextBus.h>
#include <AzFramework/Terrain/TerrainDataRequestBus.h>
#include <TerrainCompositor/HeightmapStampIdentity.h>
#include <TerrainCompositor/TerrainMeshCutoutFeatureProcessor.h>
#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>
#include <TerrainCompositor/TerrainMeshHeightStampSampling.h>
#include <Terrain/Ebuses/TerrainAreaSurfaceRequestBus.h>
#include <TerrainSystem/TerrainSystemBus.h>
#include <cmath>

namespace TerrainCompositor
{
    namespace
    {
        using Internal::DirtyHeight;
        using Internal::DirtySurface;
        using Internal::DirtyExistence;
        using Internal::DirtyAll;
        using Internal::DirtyMeshHeightAll;

        bool CanNotifyAddress(const TerrainCompositionAddress& address)
        {
            if (!address.second.IsValid())
            {
                return false;
            }
            AzFramework::EntityContextId currentContext{};
            AzFramework::EntityIdContextQueryBus::EventResult(
                currentContext, address.second, &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
            // A removed entity no longer has an owner, but its consumers still need
            // cleanup. An ID now owned by another context must not receive the previous
            // context's delayed notifications.
            return currentContext.IsNull() || currentContext == address.first;
        }

        bool SnapshotsEqual(const HeightmapDataSnapshot& left, const HeightmapDataSnapshot& right)
        {
            return left.m_status == right.m_status && left.m_revision == right.m_revision && left.m_assetId == right.m_assetId &&
                left.m_data == right.m_data;
        }

        bool CommonStampDataEqual(const HeightmapStampRegistrationData& left, const HeightmapStampRegistrationData& right)
        {
            const auto& a = left.m_configuration;
            const auto& b = right.m_configuration;
            return a.m_targetCompositionEntityId == b.m_targetCompositionEntityId && a.m_footprintWidth == b.m_footprintWidth &&
                a.m_footprintDepth == b.m_footprintDepth && a.m_strength == b.m_strength && a.m_featherWidth == b.m_featherWidth &&
                a.m_featherExponent == b.m_featherExponent && a.m_edgeInset == b.m_edgeInset && a.m_priority == b.m_priority &&
                a.m_orderingId == b.m_orderingId && a.m_stableOrderKey == b.m_stableOrderKey &&
                left.m_worldTransform == right.m_worldTransform && left.m_transformAvailable == right.m_transformAvailable &&
                left.m_identityPending == right.m_identityPending;
        }

        AZ::u8 ClassifyStampChange(const HeightmapStampRegistrationData* previous, const HeightmapStampRegistrationData& current)
        {
            if (!previous || !CommonStampDataEqual(*previous, current))
            {
                return DirtyAll;
            }
            AZ::u8 dirty = 0;
            const auto& a = previous->m_configuration;
            const auto& b = current.m_configuration;
            if (a.m_heightmapAsset.GetId() != b.m_heightmapAsset.GetId() || a.m_heightScale != b.m_heightScale ||
                a.m_verticalOffset != b.m_verticalOffset || a.m_samplingMode != b.m_samplingMode ||
                a.m_reconstructionRadius != b.m_reconstructionRadius || a.m_relativeEdgeBlend != b.m_relativeEdgeBlend ||
                !SnapshotsEqual(previous->m_heightmap, current.m_heightmap))
            {
                dirty |= DirtyHeight;
            }
            if (a.m_surfaceMaps.m_surfaceIdAAsset.GetId() != b.m_surfaceMaps.m_surfaceIdAAsset.GetId() ||
                a.m_surfaceMaps.m_surfaceIdBAsset.GetId() != b.m_surfaceMaps.m_surfaceIdBAsset.GetId() ||
                a.m_surfaceMaps.m_blendMaskAsset.GetId() != b.m_surfaceMaps.m_blendMaskAsset.GetId() ||
                !SnapshotsEqual(previous->m_surfaceIdA, current.m_surfaceIdA) ||
                !SnapshotsEqual(previous->m_surfaceIdB, current.m_surfaceIdB) ||
                !SnapshotsEqual(previous->m_surfaceBlend, current.m_surfaceBlend))
            {
                dirty |= DirtySurface;
            }
            if (a.m_holeMask.m_maskAsset.GetId() != b.m_holeMask.m_maskAsset.GetId() ||
                a.m_holeMask.m_threshold != b.m_holeMask.m_threshold || a.m_holeMask.m_operation != b.m_holeMask.m_operation ||
                !SnapshotsEqual(previous->m_holeMask, current.m_holeMask))
            {
                dirty |= DirtyExistence;
            }
            return dirty;
        }

        bool MeshSnapshotsEqual(const TerrainMeshCutoutDataSnapshot& left, const TerrainMeshCutoutDataSnapshot& right)
        {
            return left.m_status == right.m_status && left.m_validation == right.m_validation && left.m_revision == right.m_revision &&
                left.m_assetId == right.m_assetId && left.m_data == right.m_data;
        }

        bool MeshCutoutRegistrationsEqual(const TerrainMeshCutoutRegistrationData& left, const TerrainMeshCutoutRegistrationData& right)
        {
            const auto& a = left.m_configuration;
            const auto& b = right.m_configuration;
            return a.m_targetCompositionEntityId == b.m_targetCompositionEntityId &&
                a.m_cutoutMeshAsset.GetId() == b.m_cutoutMeshAsset.GetId() && a.m_operation == b.m_operation &&
                a.m_priority == b.m_priority && a.m_affectTerrainRendering == b.m_affectTerrainRendering &&
                a.m_affectTerrainCollisionQueries == b.m_affectTerrainCollisionQueries && a.m_renderMargin == b.m_renderMargin &&
                a.m_collisionMargin == b.m_collisionMargin && a.m_orderingId == b.m_orderingId &&
                a.m_stableOrderKey == b.m_stableOrderKey && left.m_worldTransform == right.m_worldTransform &&
                left.m_transformAvailable == right.m_transformAvailable && left.m_hasNonUniformScale == right.m_hasNonUniformScale &&
                left.m_identityPending == right.m_identityPending && MeshSnapshotsEqual(left.m_mesh, right.m_mesh);
        }

        bool MeshHeightSnapshotsEqual(const TerrainMeshHeightDataSnapshot& left, const TerrainMeshHeightDataSnapshot& right)
        {
            return left.m_status == right.m_status && left.m_modelValidation == right.m_modelValidation &&
                left.m_validation == right.m_validation && left.m_revision == right.m_revision && left.m_assetId == right.m_assetId &&
                left.m_data == right.m_data;
        }

        AZ::u8 ClassifyMeshHeightChange(
            const TerrainMeshHeightStampRegistrationData* previous, const TerrainMeshHeightStampRegistrationData& current)
        {
            if (!previous)
            {
                return DirtyMeshHeightAll;
            }
            const auto& a = previous->m_configuration;
            const auto& b = current.m_configuration;
            if (a.m_targetCompositionEntityId != b.m_targetCompositionEntityId ||
                a.m_terrainMeshAsset.GetId() != b.m_terrainMeshAsset.GetId() || a.m_priority != b.m_priority ||
                a.m_orderingId != b.m_orderingId || a.m_stableOrderKey != b.m_stableOrderKey ||
                previous->m_worldTransform != current.m_worldTransform || previous->m_transformAvailable != current.m_transformAvailable ||
                previous->m_hasNonUniformScale != current.m_hasNonUniformScale ||
                previous->m_identityPending != current.m_identityPending || !MeshHeightSnapshotsEqual(previous->m_mesh, current.m_mesh))
            {
                return DirtyMeshHeightAll;
            }
            AZ::u8 dirty = 0;
            if (a.m_strength != b.m_strength || a.m_featherWidth != b.m_featherWidth || a.m_featherExponent != b.m_featherExponent ||
                a.m_edgeInset != b.m_edgeInset || a.m_relativeEdgeBlend != b.m_relativeEdgeBlend)
            {
                dirty |= DirtyHeight;
            }
            if (a.m_uncoveredAreaPolicy != b.m_uncoveredAreaPolicy || a.m_affectTerrainRendering != b.m_affectTerrainRendering ||
                a.m_affectTerrainCollisionQueries != b.m_affectTerrainCollisionQueries)
            {
                dirty |= DirtyExistence;
            }
            return dirty;
        }

        AZStd::string GetMeshCutoutDataDiagnostic(const TerrainMeshCutoutDataSnapshot& mesh)
        {
            switch (mesh.m_status)
            {
            case TerrainMeshCutoutDataStatus::Unassigned:
                return "Select a closed Cutout Mesh model asset.";
            case TerrainMeshCutoutDataStatus::Loading:
                // Asset loading and geometry preparation are asynchronous. The registration
                // will publish another composition update when it becomes ready, so this is
                // pending rather than a failure.
                return {};
            case TerrainMeshCutoutDataStatus::Missing:
                return "The cutout model product is missing from the asset catalog.";
            case TerrainMeshCutoutDataStatus::Error:
                return "The cutout model failed to load or reload.";
            case TerrainMeshCutoutDataStatus::Unsupported:
                return "The selected asset is not a supported Atom Model product.";
            case TerrainMeshCutoutDataStatus::InvalidGeometry:
                return GetTerrainMeshCutoutValidationMessage(mesh.m_validation);
            case TerrainMeshCutoutDataStatus::Ready:
                return "Prepared cutter geometry is unavailable.";
            }
            return "Cutter model data is unavailable.";
        }

        AZ::Aabb IntersectFootprintsXY(const AZ::Aabb& left, const AZ::Aabb& right)
        {
            if (!left.IsValid() || !right.IsValid())
            {
                return AZ::Aabb::CreateNull();
            }
            const AZ::Vector3 minimum(
                AZStd::max(left.GetMin().GetX(), right.GetMin().GetX()), AZStd::max(left.GetMin().GetY(), right.GetMin().GetY()), 0.0f);
            const AZ::Vector3 maximum(
                AZStd::min(left.GetMax().GetX(), right.GetMax().GetX()), AZStd::min(left.GetMax().GetY(), right.GetMax().GetY()), 0.0f);
            return minimum.GetX() <= maximum.GetX() && minimum.GetY() <= maximum.GetY() ? AZ::Aabb::CreateFromMinMax(minimum, maximum)
                                                                                        : AZ::Aabb::CreateNull();
        }
    } // namespace

    AZ_COMPONENT_IMPL(
        TerrainCompositionGradientComponent,
        "TerrainCompositionGradientComponent",
        TerrainCompositionGradientComponentTypeId,
        AzFramework::EditorEntityEvents);

    void TerrainCompositionConfig::Reflect(AZ::ReflectContext* context)
    {
        TerrainQualityConfig::Reflect(context);
        SurfacePaletteEntry::Reflect(context);
        SurfaceBaseWeight::Reflect(context);
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serializeContext->IsRemovingReflection() && serializeContext->FindClassData(azrtti_typeid<TerrainCompositionConfig>()))
            {
                return;
            }
            serializeContext->Class<TerrainCompositionConfig, AZ::ComponentConfig>()
                ->Version(3)
                ->Field("ProceduralSourceEntityId", &TerrainCompositionConfig::m_proceduralSourceEntityId)
                ->Field("TargetTerrainRegionEntityId", &TerrainCompositionConfig::m_targetTerrainRegionEntityId)
                ->Field("TerrainQuality", &TerrainCompositionConfig::m_terrainQuality)
                ->Field("SurfacePalette", &TerrainCompositionConfig::m_surfacePalette)
                ->Field("BaseSurfaceWeights", &TerrainCompositionConfig::m_baseSurfaceWeights);

            if (AZ::EditContext* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<TerrainCompositionConfig>("Terrain Composition Configuration", "Procedural base and elevation mapping.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainCompositionConfig::m_proceduralSourceEntityId,
                        "Procedural Source",
                        "Existing procedural ground gradient. Self-references "
                        "and dependency cycles are rejected.")
                    ->Attribute(AZ::Edit::Attributes::RequiredService, AZ_CRC_CE("GradientService"))
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainCompositionConfig::m_targetTerrainRegionEntityId,
                        "Target Terrain Region",
                        "Separate entity with Terrain Height Gradient List and its "
                        "region shape.")
                    ->Attribute(AZ::Edit::Attributes::RequiredService, AZ_CRC_CE("TerrainHeightProviderService"))
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainCompositionConfig::m_terrainQuality,
                        "Terrain Quality",
                        "Opt-in ownership of the O3DE height grid and renderer "
                        "mesh settings.")
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainCompositionConfig::m_surfacePalette,
                        "Surface Palette",
                        "Stable exported ID to O3DE surface-tag definitions "
                        "shared by every stamp in this composition.")
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainCompositionConfig::m_baseSurfaceWeights,
                        "Base Surface Weights",
                        "Explicit weighted base. O3DE's Default Material is "
                        "only used when no positive mapped surface remains.");
            }
        }
    }

    void TerrainCompositionGradientComponent::Reflect(AZ::ReflectContext* context)
    {
        TerrainCompositionConfig::Reflect(context);
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<TerrainCompositionGradientComponent, AZ::Component>()->Version(1)->Field(
                "Configuration", &TerrainCompositionGradientComponent::m_configuration);

            if (AZ::EditContext* editContext = serializeContext->GetEditContext())
            {
                editContext
                    ->Class<TerrainCompositionGradientComponent>(
                        "Terrain Composition Gradient",
                        "Procedural ground and registered heightmap stamps. Add to a "
                        "separate entity, not the terrain region.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Gradients")
                    // Keep serialization/edit metadata for legacy wrappers, but author
                    // new instances with the editor component.
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &TerrainCompositionGradientComponent::m_configuration,
                        "Configuration",
                        "Composition sources and target region.")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, &TerrainCompositionGradientComponent::OnConfigurationChanged);
            }
        }
    }

    TerrainCompositionGradientComponent::TerrainCompositionGradientComponent(const TerrainCompositionConfig& configuration)
        : m_configuration(configuration)
    {
    }

    void TerrainCompositionGradientComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("GradientService"));
        provided.push_back(AZ_CRC_CE("TerrainCompositionService"));
    }

    void TerrainCompositionGradientComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("GradientService"));
        incompatible.push_back(AZ_CRC_CE("GradientTransformService"));
        incompatible.push_back(AZ_CRC_CE("TerrainCompositionService"));
        incompatible.push_back(AZ_CRC_CE("TerrainHeightProviderService"));
    }

    void TerrainCompositionGradientComponent::GetRequiredServices([[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        // Source gradient and region shape live on referenced entities, not this
        // entity.
    }

    void TerrainCompositionGradientComponent::GetDependentServices([[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
    }

    void TerrainCompositionGradientComponent::Activate()
    {
        StartComposition(GetEntityId());
    }

    void TerrainCompositionGradientComponent::EditorActivate(AZ::EntityId entityId)
    {
        StartComposition(entityId);
    }

    void TerrainCompositionGradientComponent::EditorDeactivate([[maybe_unused]] AZ::EntityId entityId)
    {
        Deactivate();
    }

    void TerrainCompositionGradientComponent::StartComposition(AZ::EntityId entityId)
    {
        if (!m_active)
        {
            m_controlThread.BindForActivation();
        }
        if (!m_controlThread.Check())
        {
            return;
        }
        Deactivate();
        m_active = true;
        m_session = AZ::Uuid::CreateRandom(); // A lifetime token, never persistent
                                              // ordering identity.
        m_address = { AzFramework::EntityContextId::CreateNull(), entityId };
        AzFramework::EntityIdContextQueryBus::EventResult(
            m_address.first, entityId, &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
        if (AZ::RPI::Scene* scene = AZ::RPI::Scene::GetSceneForEntityContextId(m_address.first))
        {
            if (!scene->GetFeatureProcessor<TerrainMeshCutoutFeatureProcessor>())
            {
                scene->EnableFeatureProcessor<TerrainMeshCutoutFeatureProcessor>();
            }
        }
        m_qualityController.Activate(m_address.first, entityId, m_hasQualityBaseline ? &m_qualityBaseline : nullptr);
        OnConfigurationChanged();
        GradientSignal::GradientRequestBus::Handler::BusConnect(entityId);
        AZ::SystemTickBus::Handler::BusConnect();
        AZ::TickBus::Handler::BusConnect();
        if (!m_address.first.IsNull())
        {
            TerrainCompositionRequestBus::Handler::BusConnect(m_address);
            TerrainCompositionHeightRequestBus::Handler::BusConnect(m_address);
            TerrainCompositionSurfaceRequestBus::Handler::BusConnect(m_address);
            const auto session = m_session;
            TerrainCompositionNotificationBus::Event(
                m_address, &TerrainCompositionNotificationBus::Events::OnCompositionAvailable, session);
        }
    }

    void TerrainCompositionGradientComponent::Deactivate()
    {
        if (!m_active || !m_controlThread.Check())
        {
            return;
        }
        m_active = false; // Close registrations before any external call.
        m_qualityController.Deactivate();
        TerrainCompositionRequestBus::Handler::BusDisconnect();
        TerrainCompositionHeightRequestBus::Handler::BusDisconnect();
        TerrainCompositionSurfaceRequestBus::Handler::BusDisconnect();
        // Shared-dispatch disconnect drains in-flight queries. Never hold an
        // update/publication lock here.
        GradientSignal::GradientRequestBus::Handler::BusDisconnect();
        AZ::SystemTickBus::Handler::BusDisconnect();
        AZ::TickBus::Handler::BusDisconnect();
        m_sourceChanges.reset();
        m_sourceMonitor.Reset();
        LmbrCentral::ShapeComponentNotificationsBus::Handler::BusDisconnect();
        AZ::EntityBus::Handler::BusDisconnect();
        const auto address = m_address;
        const auto session = m_session;
        if (auto* renderRegistry = AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get())
        {
            renderRegistry->Remove(session);
        }
        const auto previous = GetQueryState();
        // Readers own old arrays/images independently. No raw component state is
        // retained.
        m_queryState.exchange(std::make_shared<const QueryState>(), std::memory_order_acq_rel);
        m_observedGapActivation.reset();
        for (const auto& contributor : previous->m_heightContributors)
        {
            HeightmapStampFootprintChange change;
            change.m_stampEntityId = contributor.GetEntityId();
            change.m_address = address;
            change.m_compositionSession = session;
            change.m_snapshotRevision = m_revision + 1;
            change.m_previousBounds = contributor.GetWorldBounds();
            change.m_previousRegionEntityId = previous->m_regionEntityId;
            change.m_previousRegionBounds = previous->m_regionBounds;
            QueueHeightFootprintChange(change);
        }
        for (const auto& stamp : previous->m_surfaceStamps)
        {
            QueueSurfaceFootprintChange(*previous, stamp.m_placement.m_worldBounds);
        }
        // Removing the compositor removes its base too. All retained contexts require
        // whole-region cleanup, which does not need a live terrain spacing service
        // during shutdown.
        QueueHeightRegionChange(*previous);
        QueueSurfaceRegionChange(*previous);
        m_pendingHeightTerrain.MakeWholeRegions();
        m_pendingSurfaceTerrain.MakeWholeRegions();
        auto heightRegions = m_pendingHeightTerrain.BuildRegions(0.0f);
        auto surfaceRegions = m_pendingSurfaceTerrain.BuildRegions(0.0f);
        m_pendingHeightTerrain = {};
        m_pendingSurfaceTerrain = {};
        auto changes = AZStd::move(m_pendingChanges);
        m_pendingChanges.clear();
        m_registrations.Clear();
        m_dirtyStamps.clear();
        m_collisions.clear();
        m_reconstructionCache.clear();
        m_pendingDiagnostics.clear();
        m_regionBounds = AZ::Aabb::CreateNull();
        m_publishedCollisionGridSpacing = 0.0f;
        m_session = {};
        m_address = TerrainCompositionAddress{};
        m_sourceWasSampleable = false;
        m_hasAppliedConfiguration = false;
        TerrainCompositionNotificationBus::Event(address, &TerrainCompositionNotificationBus::Events::OnCompositionUnavailable, session);
        // Value-only removal notifications survive destruction and cannot resurrect
        // registrations.
        AZ::TickBus::QueueFunction(
            [address,
             changes = AZStd::move(changes),
             heightRegions = AZStd::move(heightRegions),
             surfaceRegions = AZStd::move(surfaceRegions)]()
            {
                DispatchChanges(address, changes, heightRegions, surfaceRegions);
            });
    }

    bool TerrainCompositionGradientComponent::ReadInConfig(const AZ::ComponentConfig* baseConfig)
    {
        if (!m_controlThread.Check())
        {
            return false;
        }
        return Internal::ReadConfiguration<TerrainCompositionConfig>(baseConfig, [this](const auto& value)
        {
            m_configuration = value;
            OnConfigurationChanged();
        });
    }

    bool TerrainCompositionGradientComponent::WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const
    {
        if (!m_controlThread.Check())
        {
            return false;
        }
        return Internal::WriteConfiguration(outBaseConfig, m_configuration);
    }

    void TerrainCompositionGradientComponent::SetTerrainQualityBaseline(const TerrainQualityBaseline& baseline)
    {
        if (!m_active && m_controlThread.Check())
        {
            m_qualityBaseline = baseline;
            m_hasQualityBaseline = true;
        }
    }

    AZStd::string TerrainCompositionGradientComponent::GetStatusMessage() const
    {
        if (!m_controlThread.Check())
        {
            return "Unavailable off the control thread.";
        }
        if (!m_active)
        {
            return "Inactive: no composed gradient.";
        }
        const auto state = GetQueryState();
        if (state->m_address.first.IsNull())
        {
            return "Waiting for entity context ownership.";
        }
        if (!state->m_sourceEntityId.IsValid())
        {
            return "Select the existing Procedural Ground Source (GradientService).";
        }
        if (!GradientSignal::GradientRequestBus::HasHandlers(state->m_sourceEntityId) || !CanSampleSource(*state))
        {
            return "Source unavailable or cyclic. Select a separate active gradient, "
                   "never Terrain Region or this composition.";
        }
        if (!state->m_regionEntityId.IsValid())
        {
            return "Select Terrain Region with Terrain Height Gradient List and its "
                   "shape.";
        }
        if (state->m_regionMapping.m_range <= 0.0)
        {
            return "Region shape/elevation range unavailable: procedural passthrough "
                   "only.";
        }
        if (!state->m_surfacePalette.IsValid())
        {
            return AZStd::string::format("Height ready; surface palette invalid: %s", state->m_surfacePalette.m_error.c_str());
        }
        bool surfaceConfigured = !m_configuration.m_surfacePalette.empty() || !m_configuration.m_baseSurfaceWeights.empty();
        for (const auto& entry : m_registrations.Get<HeightmapStampRegistrationData>())
        {
            const auto& registration = entry.second;
            const auto& maps = registration.m_configuration.m_surfaceMaps;
            surfaceConfigured = surfaceConfigured || maps.m_surfaceIdAAsset.GetId().IsValid() || maps.m_surfaceIdBAsset.GetId().IsValid() ||
                maps.m_blendMaskAsset.GetId().IsValid();
        }
        if (surfaceConfigured && !Terrain::TerrainAreaSurfaceRequestBus::HasHandlers(state->m_regionEntityId))
        {
            return "Height ready; add Terrain Composition Surface Provider to Terrain "
                   "Region for surface output.";
        }
        return AZStd::string::format(
            "Ready: %zu height stamp(s), %zu surface stamp(s), %zu existence "
            "contributor(s). %s Terrain Height Gradient List must "
            "reference only this entity.",
            state->m_heightContributors.size(),
            state->m_surfaceStamps.size(),
            state->m_existenceContributors.size(),
            m_qualityController.GetStatusMessage().c_str());
    }

    AZ::u32 TerrainCompositionGradientComponent::OnConfigurationChanged()
    {
        if (m_controlThread.Check() && m_active)
        {
            m_qualityController.Update(m_configuration.m_terrainQuality);
            const bool dependenciesChanged = !m_hasAppliedConfiguration ||
                m_configuration.m_proceduralSourceEntityId != m_appliedConfiguration.m_proceduralSourceEntityId ||
                m_configuration.m_targetTerrainRegionEntityId != m_appliedConfiguration.m_targetTerrainRegionEntityId;
            if (dependenciesChanged)
            {
                // EntityBus can deliver activation inline during dependency connection.
                // Publish only after the entire source/region transition, never an
                // interim new-reference/old-bounds combination.
                ++m_configurationUpdateDepth;
                m_sourceChanges.reset();
                m_sourceMonitor.Reset();
                LmbrCentral::ShapeComponentNotificationsBus::Handler::BusDisconnect();
                AZ::EntityBus::Handler::BusDisconnect();
                ConnectDependencies();
                --m_configurationUpdateDepth;
            }
            if (m_active && m_configurationUpdateDepth == 0)
            {
                PublishStamps();
                const auto state = GetQueryState();
                m_sourceWasSampleable = CanSampleSource(*state);
                AZ_Warning(
                    "TerrainComposition",
                    state->m_regionEntityId != state->m_ownerEntityId,
                    "Target Terrain Region must be a separate entity with Terrain "
                    "Height Gradient List and a shape.");
                AZ_Warning(
                    "TerrainComposition",
                    !state->m_sourceEntityId.IsValid() || m_sourceWasSampleable,
                    "Procedural Source is self-referencing, region-referencing, "
                    "or cyclic; sampling is disabled.");
                if (dependenciesChanged)
                {
                    // Reconnection retires the old source inbox. Cover the new region so an
                    // undelivered base edit from that inbox cannot be lost.
                    QueueHeightRegionChange(*state);
                }
                m_appliedConfiguration = m_configuration;
                m_hasAppliedConfiguration = true;
            }
        }
        return AZ::Edit::PropertyRefreshLevels::None;
    }

    void TerrainCompositionGradientComponent::ConnectDependencies()
    {
        const auto owner = m_address.second;
        const auto source = m_configuration.m_proceduralSourceEntityId;
        const auto region = m_configuration.m_targetTerrainRegionEntityId;
        m_sourceMonitor.ConnectOwner(owner);
        m_sourceChanges = std::make_shared<SourceChanges>();
        m_sourceMonitor.SetEntityNotificationFunction(
            [inbox = std::weak_ptr<SourceChanges>(m_sourceChanges), owner, source](
                const AZ::EntityId& ownerId, const AZ::EntityId& dependentId, const AZ::Aabb& dirtyRegion)
            {
                if (ownerId != owner || dependentId != source)
                {
                    return;
                }
                if (auto changes = inbox.lock())
                {
                    // Never call gradients, terrain, or dependency buses under this
                    // mailbox lock.
                    std::lock_guard lock(changes->m_mutex);
                    if (!dirtyRegion.IsValid() || !dirtyRegion.GetMin().IsFinite() || !dirtyRegion.GetMax().IsFinite())
                    {
                        changes->m_wholeRegion = true;
                        changes->m_regions.clear();
                    }
                    else if (!changes->m_wholeRegion)
                    {
                        // Bound repeated source edits while tools are unfocused (regular
                        // ticks can pause).
                        for (const auto& pending : changes->m_regions)
                        {
                            if (pending.Contains(dirtyRegion))
                            {
                                return;
                            }
                        }
                        AZStd::erase_if(
                            changes->m_regions,
                            [&dirtyRegion](const AZ::Aabb& pending)
                            {
                                return dirtyRegion.Contains(pending);
                            });
                        changes->m_regions.push_back(dirtyRegion);
                    }
                }
            });
        if (source != owner && source != region)
        {
            m_sourceMonitor.ConnectDependency(source);
        }
        // The terrain region consumes us. Follow only its shape/lifecycle, never
        // dependency notifications.
        m_regionDeactivating = false;
        if (region.IsValid() && region != owner)
        {
            LmbrCentral::ShapeComponentNotificationsBus::Handler::BusConnect(region);
            AZ::EntityBus::Handler::BusConnect(region);
        }
        RefreshRegionBounds();
    }

    void TerrainCompositionGradientComponent::RefreshRegionBounds()
    {
        const auto region = m_configuration.m_targetTerrainRegionEntityId;
        const auto session = m_session;
        AZ::Aabb bounds = AZ::Aabb::CreateNull();
        if (!m_regionDeactivating && region.IsValid() && region != m_address.second)
        {
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                bounds, region, &LmbrCentral::ShapeComponentRequestsBus::Events::GetEncompassingAabb);
        }
        if (m_active && session == m_session && region == m_configuration.m_targetTerrainRegionEntityId)
        {
            m_regionBounds = bounds;
        }
    }

    void TerrainCompositionGradientComponent::OnShapeChanged(
        [[maybe_unused]] LmbrCentral::ShapeComponentNotifications::ShapeChangeReasons reason)
    {
        if (!m_controlThread.Check() || !m_active)
        {
            return;
        }
        RefreshRegionBounds();
        if (m_configurationUpdateDepth == 0)
        {
            PublishStamps();
        }
    }

    void TerrainCompositionGradientComponent::OnEntityActivated(const AZ::EntityId& entityId)
    {
        if (!m_controlThread.Check() || !m_active || entityId != m_configuration.m_targetTerrainRegionEntityId)
        {
            return;
        }
        m_regionDeactivating = false;
        OnShapeChanged(LmbrCentral::ShapeComponentNotifications::ShapeChangeReasons::ShapeChanged);
    }

    void TerrainCompositionGradientComponent::OnEntityDeactivated(const AZ::EntityId& entityId)
    {
        if (!m_controlThread.Check() || !m_active || entityId != m_configuration.m_targetTerrainRegionEntityId)
        {
            return;
        }
        m_regionDeactivating = true;
        OnShapeChanged(LmbrCentral::ShapeComponentNotifications::ShapeChangeReasons::ShapeChanged);
    }

    void TerrainCompositionGradientComponent::QueueHeightRegionChange(const QueryState& state)
    {
        m_pendingHeightTerrain.AddRegion(state.m_regionEntityId, state.m_regionBounds);
    }

    void TerrainCompositionGradientComponent::QueueSurfaceRegionChange(const QueryState& state)
    {
        m_pendingSurfaceTerrain.AddRegion(state.m_regionEntityId, state.m_regionBounds);
    }

    void TerrainCompositionGradientComponent::QueueHeightFootprintChange(const HeightmapStampFootprintChange& change)
    {
        m_pendingChanges.push_back(change);
        m_pendingHeightTerrain.AddFootprint(change.m_previousRegionEntityId, change.m_previousRegionBounds, change.m_previousBounds);
        m_pendingHeightTerrain.AddFootprint(change.m_currentRegionEntityId, change.m_currentRegionBounds, change.m_currentBounds);
    }

    void TerrainCompositionGradientComponent::QueueSurfaceFootprintChange(const QueryState& state, const AZ::Aabb& footprint)
    {
        m_pendingSurfaceTerrain.AddFootprint(state.m_regionEntityId, state.m_regionBounds, footprint);
    }

    void TerrainCompositionGradientComponent::CollectSourceChanges()
    {
        const auto inbox = m_sourceChanges;
        if (!inbox)
        {
            return;
        }
        AZStd::vector<AZ::Aabb> regions;
        bool wholeRegion = false;
        {
            std::lock_guard lock(inbox->m_mutex);
            regions.swap(inbox->m_regions);
            wholeRegion = inbox->m_wholeRegion;
            inbox->m_wholeRegion = false;
        }
        if (!wholeRegion && regions.empty())
        {
            return;
        }
        const auto state = GetQueryState();
        const bool sampleable = CanSampleSource(*state);
        if (!m_active || state->m_session != m_session || inbox != m_sourceChanges)
        {
            return;
        }
        // A late cycle changes the result to zero throughout the region. Notify that
        // transition once, then suppress invalid links: deferral must not create a
        // frame-to-frame dependency ping-pong.
        if (sampleable != m_sourceWasSampleable)
        {
            QueueHeightRegionChange(*state);
            QueueSurfaceRegionChange(*state);
        }
        else if (sampleable)
        {
            if (wholeRegion)
            {
                QueueHeightRegionChange(*state);
                QueueSurfaceRegionChange(*state);
            }
            else
            {
                for (const auto& region : regions)
                {
                    m_pendingHeightTerrain.AddFootprint(state->m_regionEntityId, state->m_regionBounds, region);
                    m_pendingSurfaceTerrain.AddFootprint(state->m_regionEntityId, state->m_regionBounds, region);
                }
            }
        }
        m_sourceWasSampleable = sampleable;
    }

    void TerrainCompositionGradientComponent::DispatchChanges(
        TerrainCompositionAddress address,
        const AZStd::vector<HeightmapStampFootprintChange>& changes,
        const AZStd::vector<AZ::Aabb>& heightRegions,
        const AZStd::vector<AZ::Aabb>& surfaceRegions)
    {
        for (const auto& change : changes)
        {
            if (!CanNotifyAddress(address))
            {
                return;
            }
            TerrainCompositionNotificationBus::Event(address, &TerrainCompositionNotificationBus::Events::OnStampFootprintChanged, change);
        }
        for (const auto& region : heightRegions)
        {
            if (!CanNotifyAddress(address))
            {
                return;
            }
            // Never pass a null AABB: the installed height list interprets that as
            // RefreshArea.
            const bool valid = region.IsValid() && region.GetMin().IsFinite() && region.GetMax().IsFinite();
            AZ_Assert(valid, "Terrain dirty notifications must have finite, valid bounds.");
            if (!valid)
            {
                continue;
            }
            LmbrCentral::DependencyNotificationBus::Event(
                address.second, &LmbrCentral::DependencyNotificationBus::Events::OnCompositionRegionChanged, region);
        }
        for (const auto& region : surfaceRegions)
        {
            if (!CanNotifyAddress(address))
            {
                return;
            }
            const bool valid = region.IsValid() && region.GetMin().IsFinite() && region.GetMax().IsFinite();
            AZ_Assert(valid, "Terrain surface dirty notifications must have finite, valid bounds.");
            if (!valid)
            {
                continue;
            }
            Terrain::TerrainSystemServiceRequestBus::Broadcast(
                &Terrain::TerrainSystemServiceRequests::RefreshRegion,
                region,
                AzFramework::Terrain::TerrainDataNotifications::TerrainDataChangedMask::SurfaceData);
        }
    }

    void TerrainCompositionGradientComponent::OnSystemTick()
    {
        if (!m_controlThread.Check() || !m_active)
        {
            return;
        }
        const auto state = GetQueryState();
        const auto activation = CaptureGapActivation(*state);
        if (activation != m_observedGapActivation)
        {
            const AZStd::span<const PreparedTerrainMeshHeightGap> oldGaps = m_observedGapActivation
                ? AZStd::span<const PreparedTerrainMeshHeightGap>(m_observedGapActivation->m_gaps) : AZStd::span<const PreparedTerrainMeshHeightGap>{};
            const AZStd::span<const PreparedTerrainMeshHeightGap> newGaps = activation
                ? AZStd::span<const PreparedTerrainMeshHeightGap>(activation->m_gaps) : AZStd::span<const PreparedTerrainMeshHeightGap>{};
            for (const auto& gap : state->m_meshHeightGaps)
            {
                if (gap.m_affectTerrainCollisionQueries && IsTerrainMeshHeightGapAdmitted(gap, oldGaps) != IsTerrainMeshHeightGapAdmitted(gap, newGaps))
                {
                    m_pendingHeightTerrain.AddFootprint(state->m_regionEntityId, state->m_regionBounds, gap.m_collisionWorldBounds);
                    QueueSurfaceFootprintChange(*state, gap.m_collisionWorldBounds);
                }
            }
            m_observedGapActivation = activation;
        }
        AzFramework::EntityContextId context{};
        AzFramework::EntityIdContextQueryBus::EventResult(
            context, m_address.second, &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
        if (context != m_address.first)
        {
            // This also handles context ownership becoming available after activation.
            // A new session prevents any old-context callback/lease from being replayed
            // into the replacement registry.
            const auto owner = m_address.second;
            StartComposition(owner);
            return;
        }
    }

    void TerrainCompositionGradientComponent::OnTick([[maybe_unused]] float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (!m_controlThread.Check() || !m_active || m_configurationUpdateDepth != 0)
        {
            return;
        }
        m_qualityController.Tick();
        const bool heightSettingsChanged = m_qualityController.ConsumeHeightSettingsChanged();
        float liveCollisionGridSpacing = 0.0f;
        AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
            liveCollisionGridSpacing, &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightQueryResolution);
        if (!std::isfinite(liveCollisionGridSpacing) || liveCollisionGridSpacing <= 0.0f)
        {
            liveCollisionGridSpacing = 0.0f;
        }
        const bool collisionGridSpacingChanged = liveCollisionGridSpacing != m_publishedCollisionGridSpacing;
        if (collisionGridSpacingChanged)
        {
            for (const auto& [id, registration] : m_registrations.Get<TerrainMeshHeightStampRegistrationData>())
            {
                if (registration.m_configuration.m_uncoveredAreaPolicy == TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain &&
                    registration.m_configuration.m_affectTerrainCollisionQueries)
                {
                    m_dirtyStamps[id] |= DirtyExistence;
                }
            }
            for (const auto& [id, registration] : m_registrations.Get<TerrainMeshCutoutRegistrationData>())
            {
                if (registration.m_configuration.m_affectTerrainCollisionQueries)
                {
                    m_dirtyStamps[id] |= DirtyExistence;
                }
            }
        }
        if (heightSettingsChanged || collisionGridSpacingChanged)
        {
            // Rebuild collision contributors with the now-current grid-cell classification
            // before invalidation.
            PublishStamps();
        }
        if (heightSettingsChanged)
        {
            const auto state = GetQueryState();
            QueueHeightRegionChange(*state);
            for (const auto& contributor : state->m_existenceContributors)
            {
                if (contributor.m_type == PreparedTerrainExistenceContributor::Type::MeshCutout)
                {
                    QueueSurfaceFootprintChange(*state, contributor.GetWorldBounds());
                }
            }
        }
        const auto session = m_session;
        OnSystemTick(); // Do not dispatch into a context that changed since the last
                        // system tick.
        if (!m_active || session != m_session)
        {
            return;
        }
        CollectSourceChanges();
        if (!m_active || session != m_session)
        {
            return;
        }
        float heightSpacing = 0.0f;
        if (m_pendingHeightTerrain.RequiresQueryResolution())
        {
            // Settings only, outside query/update/source-mailbox locks. Never query
            // final terrain heights.
            AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
                heightSpacing, &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightQueryResolution);
        }
        float surfaceSpacing = 0.0f;
        if (m_pendingSurfaceTerrain.RequiresQueryResolution())
        {
            AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
                surfaceSpacing, &AzFramework::Terrain::TerrainDataRequests::GetTerrainSurfaceDataQueryResolution);
        }
        if (!m_active || session != m_session)
        {
            return;
        }
        AZStd::vector<AZ::Aabb> heightRegions;
        if (!m_pendingHeightTerrain.RequiresQueryResolution() || (std::isfinite(heightSpacing) && heightSpacing > 0.0f))
        {
            heightRegions = m_pendingHeightTerrain.BuildRegions(heightSpacing);
            m_pendingHeightTerrain = {};
        }
        AZStd::vector<AZ::Aabb> surfaceRegions;
        if (!m_pendingSurfaceTerrain.RequiresQueryResolution() || (std::isfinite(surfaceSpacing) && surfaceSpacing > 0.0f))
        {
            surfaceRegions = m_pendingSurfaceTerrain.BuildRegions(surfaceSpacing);
            m_pendingSurfaceTerrain = {};
        }
        // Missing/invalid terrain spacing retains footprint work until the service is
        // ready. Metadata and diagnostics can still be delivered; shutdown promotes
        // retained work to whole-region cleanup.
        const auto address = m_address;
        auto changes = AZStd::move(m_pendingChanges);
        auto diagnostics = AZStd::move(m_pendingDiagnostics);
        m_pendingChanges.clear();
        m_pendingDiagnostics.clear();
        // Only local copies are used after external calls: listeners can
        // retarget/deactivate components.
        for (const auto& diagnostic : diagnostics)
        {
            AZ_Warning("TerrainComposition", false, "%s", diagnostic.c_str());
        }
        DispatchChanges(address, changes, heightRegions, surfaceRegions);
    }

    TerrainCompositionGradientComponent::QueryStatePtr TerrainCompositionGradientComponent::GetQueryState() const
    {
        return m_queryState.load(std::memory_order_acquire);
    }

    bool TerrainCompositionGradientComponent::CanSampleSource(const QueryState& state)
    {
        if (!state.m_sourceEntityId.IsValid() || state.m_sourceEntityId == state.m_ownerEntityId ||
            state.m_sourceEntityId == state.m_regionEntityId)
        {
            return false;
        }
        bool cyclic = false;
        GradientSignal::GradientRequestBus::EventResult(
            cyclic, state.m_sourceEntityId, &GradientSignal::GradientRequestBus::Events::IsEntityInHierarchy, state.m_ownerEntityId);
        return !cyclic;
    }

    float TerrainCompositionGradientComponent::GetNormalizedHeight(const QueryState& state, const AZ::Vector3& position)
    {
        if (GradientSignal::GradientRequestBus::HasReentrantEBusUseThisThread() ||
            !GradientSignal::GradientRequestBus::HasHandlers(state.m_sourceEntityId) || !CanSampleSource(state))
        {
            return 0.0f; // Preserve the existing unavailable/cyclic-source fallback,
                         // including stamp suppression.
        }
        float value = 0.0f;
        const GradientSignal::GradientSampleParams sampleParams(position);
        GradientSignal::GradientRequestBus::EventResult(
            value, state.m_sourceEntityId, &GradientSignal::GradientRequestBus::Events::GetValue, sampleParams);
        return ComposeHeightContributors(
            position,
            value,
            state.m_regionMapping,
            AZStd::span<const PreparedHeightContributor>(state.m_heightContributors.data(), state.m_heightContributors.size()));
    }

    TerrainMeshHeightGapActivationPtr TerrainCompositionGradientComponent::CaptureGapActivation(const QueryState& state)
    {
        const auto channel = state.m_renderChannel.lock();
        return channel ? channel->m_activation.load(std::memory_order_acquire) : TerrainMeshHeightGapActivationPtr{};
    }

    TerrainRenderGeometryQuery TerrainCompositionGradientComponent::CreateRenderGeometryQuery(QueryStatePtr state)
    {
        TerrainRenderGeometryQuery query;
        query.m_regionBounds = state->m_regionBounds;
        query.m_getHeight = [state](const AZ::Vector3& position)
        {
            return float(state->m_regionMapping.m_minZ + double(GetNormalizedHeight(*state, position)) * state->m_regionMapping.m_range);
        };
        query.m_getTerrainExists = [state](const AZ::Vector3& position)
        {
            bool baseExists = true;
            if (!TerrainExistenceSourceRequestBus::HasReentrantEBusUseThisThread() &&
                TerrainExistenceSourceRequestBus::HasHandlers(state->m_sourceEntityId) && CanSampleSource(*state))
            {
                TerrainExistenceSourceRequestBus::EventResult(
                    baseExists, state->m_sourceEntityId, &TerrainExistenceSourceRequestBus::Events::GetTerrainExists, position);
            }
            return ComposeTerrainRenderGeometryExists(position, baseExists, state->m_existenceContributors);
        };
        query.m_getGeometry = [state](AZStd::span<const AZ::Vector3> positions, AZStd::span<float> heights, AZStd::span<bool> exists)
        {
            AZ_Assert(positions.size() == heights.size() && positions.size() == exists.size(), "Render geometry batch size mismatch.");
            if (positions.empty() || positions.size() != heights.size() || positions.size() != exists.size())
                return;

            AZStd::fill(heights.begin(), heights.end(), 0.0f);
            const bool sampleHeights = !GradientSignal::GradientRequestBus::HasReentrantEBusUseThisThread() &&
                GradientSignal::GradientRequestBus::HasHandlers(state->m_sourceEntityId) && CanSampleSource(*state);
            if (sampleHeights)
            {
                GradientSignal::GradientRequestBus::Event(
                    state->m_sourceEntityId, &GradientSignal::GradientRequestBus::Events::GetValues, positions, heights);
            }
            AZStd::fill(exists.begin(), exists.end(), true);
            if (!TerrainExistenceSourceRequestBus::HasReentrantEBusUseThisThread() &&
                TerrainExistenceSourceRequestBus::HasHandlers(state->m_sourceEntityId) && CanSampleSource(*state))
            {
                TerrainExistenceSourceRequestBus::Event(
                    state->m_sourceEntityId, &TerrainExistenceSourceRequestBus::Events::GetTerrainExistsFromList, positions, exists);
            }
            for (size_t index = 0; index < positions.size(); ++index)
            {
                // Match the scalar unavailable/cyclic-source fallback: suppress
                // height stamps too. Existence still fails open and applies image holes.
                if (sampleHeights)
                {
                    heights[index] =
                        ComposeHeightContributors(positions[index], heights[index], state->m_regionMapping, state->m_heightContributors);
                }
                heights[index] = float(state->m_regionMapping.m_minZ + double(heights[index]) * state->m_regionMapping.m_range);
                exists[index] = ComposeTerrainRenderGeometryExists(positions[index], exists[index], state->m_existenceContributors);
            }
        };
        return query;
    }

    bool TerrainCompositionGradientComponent::GetComposedTerrainExists(const QueryState& state, const AZ::Vector3& position,
        const TerrainMeshHeightGapActivationPtr& activation) const
    {
        bool baseExists = true; // Existence is opt-in; missing/unavailable sources fail open.
        if (!TerrainExistenceSourceRequestBus::HasReentrantEBusUseThisThread() &&
            TerrainExistenceSourceRequestBus::HasHandlers(state.m_sourceEntityId) && CanSampleSource(state))
        {
            TerrainExistenceSourceRequestBus::EventResult(
                baseExists, state.m_sourceEntityId, &TerrainExistenceSourceRequestBus::Events::GetTerrainExists, position);
        }
        const AZStd::span<const PreparedTerrainMeshHeightGap> admitted = activation
            ? AZStd::span<const PreparedTerrainMeshHeightGap>(activation->m_gaps) : AZStd::span<const PreparedTerrainMeshHeightGap>{};
        return ComposeTerrainExists(
            position,
            baseExists,
            AZStd::span<const PreparedTerrainExistenceContributor>(
                state.m_existenceContributors.data(), state.m_existenceContributors.size()), &admitted);
    }

    bool TerrainCompositionGradientComponent::IsEntityInHierarchy(const AZ::EntityId& entityId) const
    {
        const auto state = GetQueryState();
        if (entityId == state->m_ownerEntityId || entityId == state->m_sourceEntityId)
        {
            return true;
        }
        if (GradientSignal::GradientRequestBus::HasReentrantEBusUseThisThread())
        {
            return true;
        }
        bool inHierarchy = false;
        if (state->m_sourceEntityId.IsValid() && state->m_sourceEntityId != state->m_regionEntityId)
        {
            GradientSignal::GradientRequestBus::EventResult(
                inHierarchy, state->m_sourceEntityId, &GradientSignal::GradientRequestBus::Events::IsEntityInHierarchy, entityId);
        }
        return inHierarchy;
    }

    float TerrainCompositionGradientComponent::GetValue(const GradientSignal::GradientSampleParams& sampleParams) const
    {
        const auto state = GetQueryState();
        return GetNormalizedHeight(*state, sampleParams.m_position);
    }

    void TerrainCompositionGradientComponent::GetValues(AZStd::span<const AZ::Vector3> positions, AZStd::span<float> outValues) const
    {
        if (positions.size() != outValues.size())
        {
            AZ_Assert(false, "Input and output lists are different sizes (%zu vs %zu).", positions.size(), outValues.size());
            return;
        }
        if (positions.empty())
        {
            return;
        }
        AZStd::fill(outValues.begin(), outValues.end(), 0.0f);
        const auto state = GetQueryState(); // Exactly one retained state for the whole batch.
        if (GradientSignal::GradientRequestBus::HasReentrantEBusUseThisThread() ||
            !GradientSignal::GradientRequestBus::HasHandlers(state->m_sourceEntityId) || !CanSampleSource(*state))
        {
            return;
        }
        GradientSignal::GradientRequestBus::Event(
            state->m_sourceEntityId, &GradientSignal::GradientRequestBus::Events::GetValues, positions, outValues);
        if (state->m_heightContributors.empty())
        {
            return; // Exact batched procedural pass-through, without a per-position
                    // composition loop.
        }
        const AZStd::span<const PreparedHeightContributor> contributors(
            state->m_heightContributors.data(), state->m_heightContributors.size());
        for (size_t index = 0; index < positions.size(); ++index)
        {
            outValues[index] = ComposeHeightContributors(positions[index], outValues[index], state->m_regionMapping, contributors);
        }
    }

    float TerrainCompositionGradientComponent::GetHeight(
        AZ::EntityId terrainRegionEntityId, const AZ::Vector3& position, bool& terrainExists) const
    {
        terrainExists = false;
        const auto state = GetQueryState();
        if (terrainRegionEntityId != state->m_regionEntityId || state->m_regionMapping.m_range <= 0.0)
        {
            return 0.0f;
        }
        const float height = static_cast<float>(
            state->m_regionMapping.m_minZ + double(GetNormalizedHeight(*state, position)) * state->m_regionMapping.m_range);
        AZ::Vector3 surfacePoint = position;
        surfacePoint.SetZ(height);
        terrainExists = GetComposedTerrainExists(*state, surfacePoint, CaptureGapActivation(*state));
        return height;
    }

    void TerrainCompositionGradientComponent::GetHeights(
        AZ::EntityId terrainRegionEntityId,
        AZStd::span<const AZ::Vector3> positions,
        AZStd::span<float> outHeights,
        AZStd::span<bool> terrainExists) const
    {
        if (positions.size() != outHeights.size() || positions.size() != terrainExists.size())
        {
            AZ_Assert(false, "Terrain composition height input/output lists have different sizes.");
            return;
        }
        const auto state = GetQueryState();
        if (terrainRegionEntityId != state->m_regionEntityId || state->m_regionMapping.m_range <= 0.0)
        {
            AZStd::fill(outHeights.begin(), outHeights.end(), 0.0f);
            AZStd::fill(terrainExists.begin(), terrainExists.end(), false);
            return;
        }
        AZStd::fill(outHeights.begin(), outHeights.end(), 0.0f);
        const bool sampleable = !GradientSignal::GradientRequestBus::HasReentrantEBusUseThisThread() &&
            GradientSignal::GradientRequestBus::HasHandlers(state->m_sourceEntityId) && CanSampleSource(*state);
        if (sampleable && !positions.empty())
        {
            GradientSignal::GradientRequestBus::Event(
                state->m_sourceEntityId, &GradientSignal::GradientRequestBus::Events::GetValues, positions, outHeights);
            const AZStd::span<const PreparedHeightContributor> contributors(
                state->m_heightContributors.data(), state->m_heightContributors.size());
            for (size_t index = 0; index < positions.size(); ++index)
            {
                outHeights[index] = ComposeHeightContributors(positions[index], outHeights[index], state->m_regionMapping, contributors);
            }
        }
        constexpr size_t ExistenceBatchSize = 256;
        const auto activation = CaptureGapActivation(*state);
        const AZStd::span<const PreparedTerrainMeshHeightGap> admitted = activation
            ? AZStd::span<const PreparedTerrainMeshHeightGap>(activation->m_gaps) : AZStd::span<const PreparedTerrainMeshHeightGap>{};
        AZStd::array<AZ::Vector3, ExistenceBatchSize> surfacePoints;
        AZStd::array<bool, ExistenceBatchSize> baseExists;
        const bool canSampleExistence = !TerrainExistenceSourceRequestBus::HasReentrantEBusUseThisThread() &&
            TerrainExistenceSourceRequestBus::HasHandlers(state->m_sourceEntityId) && CanSampleSource(*state);
        const AZStd::span<const PreparedTerrainExistenceContributor> contributors(
            state->m_existenceContributors.data(), state->m_existenceContributors.size());
        for (size_t offset = 0; offset < positions.size(); offset += ExistenceBatchSize)
        {
            const size_t count = AZStd::min(ExistenceBatchSize, positions.size() - offset);
            for (size_t index = 0; index < count; ++index)
            {
                outHeights[offset + index] =
                    static_cast<float>(state->m_regionMapping.m_minZ + double(outHeights[offset + index]) * state->m_regionMapping.m_range);
                surfacePoints[index] = positions[offset + index];
                surfacePoints[index].SetZ(outHeights[offset + index]);
                baseExists[index] = true;
            }
            if (canSampleExistence)
            {
                TerrainExistenceSourceRequestBus::Event(
                    state->m_sourceEntityId,
                    &TerrainExistenceSourceRequestBus::Events::GetTerrainExistsFromList,
                    AZStd::span<const AZ::Vector3>(surfacePoints.data(), count),
                    AZStd::span<bool>(baseExists.data(), count));
            }
            for (size_t index = 0; index < count; ++index)
            {
                terrainExists[offset + index] = ComposeTerrainExists(surfacePoints[index], baseExists[index], contributors, &admitted);
            }
        }
    }

    void TerrainCompositionGradientComponent::GetSurfaceWeights(
        AZ::EntityId terrainRegionEntityId,
        const AZ::Vector3& position,
        AzFramework::SurfaceData::SurfaceTagWeightList& outSurfaceWeights) const
    {
        outSurfaceWeights.clear();
        const auto state = GetQueryState();
        if (terrainRegionEntityId != state->m_regionEntityId)
        {
            return;
        }
        AZ::Vector3 surfacePoint = position;
        surfacePoint.SetZ(
            static_cast<float>(
                state->m_regionMapping.m_minZ + double(GetNormalizedHeight(*state, position)) * state->m_regionMapping.m_range));
        if (!GetComposedTerrainExists(*state, surfacePoint, CaptureGapActivation(*state)))
        {
            return;
        }
        ComposeSurfaceStamps(
            position,
            state->m_surfacePalette,
            AZStd::span<const PreparedSurfaceStamp>(state->m_surfaceStamps.data(), state->m_surfaceStamps.size()),
            outSurfaceWeights);
    }

    void TerrainCompositionGradientComponent::GetSurfaceWeightsFromList(
        AZ::EntityId terrainRegionEntityId,
        AZStd::span<const AZ::Vector3> positions,
        AZStd::span<AzFramework::SurfaceData::SurfaceTagWeightList> outSurfaceWeights) const
    {
        if (!PrepareSurfaceWeightBatch(positions, outSurfaceWeights))
        {
            return;
        }
        if (positions.empty())
        {
            return;
        }
        const auto state = GetQueryState(); // One immutable publication for the complete batch.
        if (terrainRegionEntityId != state->m_regionEntityId)
        {
            return;
        }
        const AZStd::span<const PreparedSurfaceStamp> stamps(state->m_surfaceStamps.data(), state->m_surfaceStamps.size());
        const auto activation = CaptureGapActivation(*state);
        for (size_t index = 0; index < positions.size(); ++index)
        {
            AZ::Vector3 surfacePoint = positions[index];
            surfacePoint.SetZ(
                static_cast<float>(
                    state->m_regionMapping.m_minZ +
                    double(GetNormalizedHeight(*state, positions[index])) * state->m_regionMapping.m_range));
            if (GetComposedTerrainExists(*state, surfacePoint, activation))
            {
                ComposeSurfaceStamps(positions[index], state->m_surfacePalette, stamps, outSurfaceWeights[index]);
            }
        }
    }

    AZ::Uuid TerrainCompositionGradientComponent::GetCompositionSession() const
    {
        return m_controlThread.Check() && m_active ? m_session : AZ::Uuid{};
    }

    template<class Registration, class Classify>
    bool TerrainCompositionGradientComponent::RegisterAndPublish(const Registration& registration, Classify classify)
    {
        if (!m_controlThread.Check() || !m_active) return false;
        const auto admission = m_registrations.Register(registration, m_address, m_session, m_dirtyStamps, classify);
        if (admission == Internal::RegistrationAdmission::Apply && !m_dirtyStamps.empty())
        {
            PublishStamps();
        }
        return admission != Internal::RegistrationAdmission::Reject;
    }

    bool TerrainCompositionGradientComponent::RegisterStamp(const HeightmapStampRegistrationData& registration)
    {
        return RegisterAndPublish(registration, ClassifyStampChange);
    }

    bool TerrainCompositionGradientComponent::RegisterMeshCutout(const TerrainMeshCutoutRegistrationData& registration)
    {
        return RegisterAndPublish(registration, [](const auto* previous, const auto& current) -> AZ::u8
        {
            return !previous || !MeshCutoutRegistrationsEqual(*previous, current) ? DirtyExistence : AZ::u8{ 0 };
        });
    }

    bool TerrainCompositionGradientComponent::RegisterMeshHeightStamp(const TerrainMeshHeightStampRegistrationData& registration)
    {
        return RegisterAndPublish(registration, ClassifyMeshHeightChange);
    }

    template<class Registration>
    void TerrainCompositionGradientComponent::UnregisterAndPublish(
        AZ::EntityId entityId, const AZ::Uuid& registrationId, const AZ::Uuid& compositionSession)
    {
        if (!m_controlThread.Check() || !m_active) return;
        if (m_registrations.Remove<Registration>(entityId, registrationId, compositionSession, m_session, m_dirtyStamps))
        {
            PublishStamps();
        }
    }

    void TerrainCompositionGradientComponent::UnregisterStamp(
        AZ::EntityId stampEntityId, const AZ::Uuid& registrationId, const AZ::Uuid& compositionSession)
    {
        UnregisterAndPublish<HeightmapStampRegistrationData>(stampEntityId, registrationId, compositionSession);
    }

    void TerrainCompositionGradientComponent::UnregisterMeshCutout(
        AZ::EntityId cutoutEntityId, const AZ::Uuid& registrationId, const AZ::Uuid& compositionSession)
    {
        UnregisterAndPublish<TerrainMeshCutoutRegistrationData>(cutoutEntityId, registrationId, compositionSession);
    }

    void TerrainCompositionGradientComponent::UnregisterMeshHeightStamp(
        AZ::EntityId stampEntityId, const AZ::Uuid& registrationId, const AZ::Uuid& compositionSession)
    {
        UnregisterAndPublish<TerrainMeshHeightStampRegistrationData>(stampEntityId, registrationId, compositionSession);
    }

    void TerrainCompositionGradientComponent::PublishStamps()
    {
        if (!m_active)
        {
            return;
        }
        if (m_configurationUpdateDepth != 0)
        {
            return;
        }
        const auto previous = GetQueryState();
        auto replacement = std::make_shared<QueryState>();
        replacement->m_address = m_address;
        replacement->m_session = m_session;
        replacement->m_revision = ++m_revision;
        replacement->m_ownerEntityId = m_address.second;
        replacement->m_sourceEntityId = m_configuration.m_proceduralSourceEntityId;
        replacement->m_regionEntityId = m_configuration.m_targetTerrainRegionEntityId;
        replacement->m_regionBounds = m_regionBounds;
        replacement->m_regionMapping = PrepareHeightmapRegionMapping(m_regionBounds);
        replacement->m_surfacePalette = PrepareSurfacePalette(
            AZStd::span<const SurfacePaletteEntry>(m_configuration.m_surfacePalette.data(), m_configuration.m_surfacePalette.size()),
            AZStd::span<const SurfaceBaseWeight>(m_configuration.m_baseSurfaceWeights.data(), m_configuration.m_baseSurfaceWeights.size()));
        if (!replacement->m_surfacePalette.IsValid() && replacement->m_surfacePalette.m_error != previous->m_surfacePalette.m_error)
        {
            m_pendingDiagnostics.push_back(
                AZStd::string::format(
                    "Composition %s surface palette is invalid: %s",
                    m_address.second.ToString().c_str(),
                    replacement->m_surfacePalette.m_error.c_str()));
        }

        AZStd::unordered_map<AZStd::string, AZStd::vector<AZ::EntityId>> claims;
        const auto collectClaims = [&claims](const auto& registrations)
        {
            for (const auto& [id, registration] : registrations)
            {
                const auto key = registration.m_configuration.GetRuntimeOrderKey();
                if (!key.empty())
                {
                    claims[key].push_back(id);
                }
            }
        };
        collectClaims(m_registrations.Get<HeightmapStampRegistrationData>());
        collectClaims(m_registrations.Get<TerrainMeshCutoutRegistrationData>());
        collectClaims(m_registrations.Get<TerrainMeshHeightStampRegistrationData>());
        AZStd::unordered_map<AZStd::string, AZStd::string> collisions;
        for (const auto& [key, claimants] : claims)
        {
            if (claimants.size() > 1)
            {
                AZStd::vector<AZStd::string> displayIds;
                for (const auto id : claimants)
                {
                    displayIds.push_back(id.ToString());
                }
                AZStd::sort(displayIds.begin(),
                            displayIds.end()); // Diagnostic text only, never blend order.
                AZStd::string ids;
                for (const auto& id : displayIds)
                {
                    ids += id + " ";
                }
                collisions.emplace(key, ids);
                const auto prior = m_collisions.find(key);
                if (prior == m_collisions.end() || prior->second != ids)
                {
                    m_pendingDiagnostics.push_back(
                        AZStd::string::format(
                            "Ordering collision '%s' in composition %s, context %s; ALL "
                            "claimants suppressed: %s",
                            key.c_str(),
                            m_address.second.ToString().c_str(),
                            m_address.first.ToString<AZStd::string>().c_str(),
                            ids.c_str()));
                }
            }
        }
        m_collisions = AZStd::move(collisions);
        float collisionGridSpacing = 0.0f;
        AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
            collisionGridSpacing, &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightQueryResolution);
        for (const auto& [id, registration] : m_registrations.Get<HeightmapStampRegistrationData>())
        {
            PreparedHeightmapStamp prepared;
            const auto validation = PrepareHeightmapStamp(registration, AZ::NonUniformScaleRequestBus::HasHandlers(id), prepared);
            m_registrations.RecordDiagnostic(Internal::RegistrationDiagnostic::Height, id, registration,
                validation == HeightmapStampValidation::Valid ? "" : GetHeightmapStampValidationMessage(validation),
                "Stamp %s contributes nothing: %s", m_pendingDiagnostics);
            if (validation == HeightmapStampValidation::Valid && !prepared.m_placement.m_stableOrderKey.empty() &&
                !m_collisions.contains(prepared.m_placement.m_stableOrderKey) && prepared.m_image && prepared.m_strength > 0.0 &&
                prepared.m_placement.m_edgeInset < prepared.m_placement.m_halfWidth &&
                prepared.m_placement.m_edgeInset < prepared.m_placement.m_halfDepth)
            {
                prepared.m_reconstruction = AcquireHeightmapReconstruction(
                    prepared.m_image, registration.m_configuration.m_samplingMode, registration.m_configuration.m_reconstructionRadius);

                PreparedHeightContributor contributor;
                contributor.m_type = PreparedHeightContributor::Type::Image;
                contributor.m_image = AZStd::move(prepared);
                replacement->m_heightContributors.push_back(AZStd::move(contributor));
            }

            PreparedSurfaceStamp preparedSurface;
            HeightmapStampValidation surfacePlacement = HeightmapStampValidation::Valid;
            const auto surfaceValidation = PrepareSurfaceStamp(
                registration,
                AZ::NonUniformScaleRequestBus::HasHandlers(id),
                replacement->m_surfacePalette,
                preparedSurface,
                &surfacePlacement);
            const AZStd::string surfaceReason = surfaceValidation == SurfaceStampValidation::Placement
                ? GetHeightmapStampValidationMessage(surfacePlacement)
                : surfaceValidation != SurfaceStampValidation::Valid && surfaceValidation != SurfaceStampValidation::Absent
                    ? GetSurfaceStampValidationMessage(surfaceValidation) : "";
            m_registrations.RecordDiagnostic(Internal::RegistrationDiagnostic::Surface, id, registration,
                surfaceReason, "Stamp %s contributes no surface data: %s", m_pendingDiagnostics);
            if (surfaceValidation == SurfaceStampValidation::Valid && !preparedSurface.m_placement.m_stableOrderKey.empty() &&
                !m_collisions.contains(preparedSurface.m_placement.m_stableOrderKey) && preparedSurface.m_surfaceIdA &&
                preparedSurface.m_strength > 0.0 && preparedSurface.m_placement.m_edgeInset < preparedSurface.m_placement.m_halfWidth &&
                preparedSurface.m_placement.m_edgeInset < preparedSurface.m_placement.m_halfDepth)
            {
                replacement->m_surfaceStamps.push_back(AZStd::move(preparedSurface));
            }

            PreparedTerrainExistenceStamp preparedExistence;
            HeightmapStampValidation existencePlacement = HeightmapStampValidation::Valid;
            const auto existenceValidation = PrepareTerrainExistenceStamp(
                registration, AZ::NonUniformScaleRequestBus::HasHandlers(id), preparedExistence, &existencePlacement);
            const AZStd::string existenceReason = existenceValidation == TerrainExistenceStampValidation::Placement
                ? GetHeightmapStampValidationMessage(existencePlacement)
                : existenceValidation != TerrainExistenceStampValidation::Valid && existenceValidation != TerrainExistenceStampValidation::Absent
                    ? GetTerrainExistenceStampValidationMessage(existenceValidation) : "";
            m_registrations.RecordDiagnostic(Internal::RegistrationDiagnostic::Existence, id, registration,
                existenceReason, "Stamp %s contributes no terrain existence data: %s", m_pendingDiagnostics);
            if (existenceValidation == TerrainExistenceStampValidation::Valid && !preparedExistence.m_placement.m_stableOrderKey.empty() &&
                !m_collisions.contains(preparedExistence.m_placement.m_stableOrderKey) &&
                preparedExistence.m_placement.m_edgeInset < preparedExistence.m_placement.m_halfWidth &&
                preparedExistence.m_placement.m_edgeInset < preparedExistence.m_placement.m_halfDepth)
            {
                PreparedTerrainExistenceContributor contributor;
                contributor.m_type = PreparedTerrainExistenceContributor::Type::ImageMask;
                contributor.m_imageMask = AZStd::move(preparedExistence);
                replacement->m_existenceContributors.push_back(AZStd::move(contributor));
            }
        }
        for (const auto& [id, registration] : m_registrations.Get<TerrainMeshHeightStampRegistrationData>())
        {
            PreparedTerrainMeshHeightStamp prepared;
            const auto validation = PrepareTerrainMeshHeightStamp(registration, registration.m_hasNonUniformScale, prepared);
            const AZStd::string reason = validation == TerrainMeshHeightStampPlacementValidation::Valid ? ""
                : validation == TerrainMeshHeightStampPlacementValidation::DataUnavailable
                    ? GetTerrainMeshHeightDataDiagnostic(registration.m_mesh) : GetTerrainMeshHeightStampPlacementValidationMessage(validation);
            m_registrations.RecordDiagnostic(Internal::RegistrationDiagnostic::MeshHeight, id, registration,
                reason, "Terrain mesh height stamp %s contributes nothing: %s", m_pendingDiagnostics);
            if (validation == TerrainMeshHeightStampPlacementValidation::Valid && !prepared.m_stableOrderKey.empty() &&
                !m_collisions.contains(prepared.m_stableOrderKey) && prepared.m_data)
            {
                PreparedTerrainMeshHeightGap gap;
                if (PrepareTerrainMeshHeightGap(prepared, collisionGridSpacing, replacement->m_regionBounds, gap))
                {
                    gap.m_compositionSession = m_session;
                    if (gap.m_affectTerrainCollisionQueries)
                    {
                        PreparedTerrainExistenceContributor existenceContributor;
                        existenceContributor.m_type = PreparedTerrainExistenceContributor::Type::MeshHeightGap;
                        existenceContributor.m_meshHeightGap = gap;
                        replacement->m_existenceContributors.push_back(AZStd::move(existenceContributor));
                    }
                    replacement->m_meshHeightGaps.push_back(AZStd::move(gap));
                }
                if (prepared.m_strength > 0.0)
                {
                    PreparedHeightContributor contributor;
                    contributor.m_type = PreparedHeightContributor::Type::Mesh;
                    contributor.m_mesh = AZStd::move(prepared);
                    replacement->m_heightContributors.push_back(AZStd::move(contributor));
                }
            }
        }

        AZStd::vector<PreparedTerrainMeshCutout> renderCutouts;
        for (const auto& [id, registration] : m_registrations.Get<TerrainMeshCutoutRegistrationData>())
        {
            PreparedTerrainMeshCutout prepared;
            const auto validation = PrepareTerrainMeshCutout(registration, registration.m_hasNonUniformScale, prepared);
            const AZStd::string reason = validation == TerrainMeshCutoutPlacementValidation::Valid ? ""
                : validation == TerrainMeshCutoutPlacementValidation::DataUnavailable
                    ? GetMeshCutoutDataDiagnostic(registration.m_mesh) : GetTerrainMeshCutoutPlacementValidationMessage(validation);
            m_registrations.RecordDiagnostic(Internal::RegistrationDiagnostic::Cutout, id, registration,
                reason, "Terrain mesh cutout %s contributes nothing: %s", m_pendingDiagnostics);
            if (validation == TerrainMeshCutoutPlacementValidation::Valid && !m_collisions.contains(prepared.m_stableOrderKey))
            {
                if (prepared.m_affectTerrainRendering)
                {
                    renderCutouts.push_back(prepared);
                }
                if (prepared.m_affectTerrainCollisionQueries)
                {
                    ApplyTerrainMeshCutoutCollisionCellPadding(prepared, collisionGridSpacing);

                    PreparedTerrainExistenceContributor contributor;
                    contributor.m_type = PreparedTerrainExistenceContributor::Type::MeshCutout;
                    contributor.m_meshCutout = AZStd::move(prepared);
                    replacement->m_existenceContributors.push_back(AZStd::move(contributor));
                }
            }
        }

        // Candidate order preserves first-claim footprint selection before the blend streams are sorted.
        Internal::PublicationFootprints currentFootprints(*replacement);
        const auto sortContributors = [](auto& values, auto order)
        {
            AZStd::sort(values.begin(), values.end(), [order](const auto& a, const auto& b)
            {
                const auto [priorityA, keyA] = order(a);
                const auto [priorityB, keyB] = order(b);
                return priorityA != priorityB ? priorityA < priorityB : StampOrderKeyLess(keyA, keyB);
            });
        };
        const auto contributorOrder = [](const auto& value)
        {
            return AZStd::pair(value.GetPriority(), AZStd::string_view(value.GetStableOrderKey()));
        };
        sortContributors(replacement->m_heightContributors, contributorOrder);
        sortContributors(replacement->m_existenceContributors, contributorOrder);
        sortContributors(replacement->m_surfaceStamps, [](const auto& value)
        {
            return AZStd::pair(value.m_placement.m_priority, AZStd::string_view(value.m_placement.m_stableOrderKey));
        });
        sortContributors(replacement->m_meshHeightGaps, [](const auto& value)
        {
            return AZStd::pair(value.m_priority, AZStd::string_view(value.m_stableOrderKey));
        });

        AZStd::vector<PreparedTerrainMeshHeightGap> publishedMeshHeightGaps = replacement->m_meshHeightGaps;
        auto* renderRegistry = AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get();
        if (!renderRegistry && AZStd::any_of(
                publishedMeshHeightGaps.begin(), publishedMeshHeightGaps.end(),
                [](const auto& gap) { return gap.m_affectTerrainRendering; }))
        {
            AZStd::unordered_set<AZ::EntityId> suppressed;
            for (const auto& gap : publishedMeshHeightGaps)
            {
                if (!gap.m_affectTerrainRendering)
                {
                    continue;
                }
                suppressed.insert(gap.m_entityId);
                currentFootprints.m_gapRendering.erase(gap.m_entityId);
                currentFootprints.m_gapQueries.erase(gap.m_entityId);
                m_pendingDiagnostics.push_back(
                    AZStd::string::format(
                        "Terrain mesh height gap %s is neutral because the render "
                        "publication registry is unavailable.",
                        gap.m_entityId.ToString().c_str()));
            }
            AZStd::erase_if(
                replacement->m_existenceContributors,
                [&suppressed](const auto& contributor)
                {
                    return contributor.m_type == PreparedTerrainExistenceContributor::Type::MeshHeightGap &&
                        suppressed.contains(contributor.GetEntityId());
                });
            AZStd::erase_if(
                replacement->m_meshHeightGaps,
                [&suppressed](const auto& gap)
                {
                    return suppressed.contains(gap.m_entityId);
                });
            publishedMeshHeightGaps.clear();
        }

        if (renderRegistry)
        {
            replacement->m_renderChannel = renderRegistry->AcquireSceneChannel(AZ::RPI::Scene::GetSceneForEntityContextId(m_address.first));
            if (!renderRegistry->Publish(
                    AZ::RPI::Scene::GetSceneForEntityContextId(m_address.first),
                    m_session,
                    AZStd::move(renderCutouts),
                    CreateRenderGeometryQuery(replacement),
                    replacement->m_revision,
                    AZStd::move(publishedMeshHeightGaps)))
            {
                m_pendingDiagnostics.push_back(
                    AZStd::string::format(
                        "Composition %s render publication rejected stale generation %llu; "
                        "CPU publication was retained.",
                        m_address.second.ToString().c_str(),
                        static_cast<unsigned long long>(replacement->m_revision)));
                return;
            }
        }

        const Internal::PublicationFootprints oldFootprints(*previous);
        // Reentrant registration during dependency reconnection must not lose old/new
        // footprint metadata.
        auto affected = AZStd::move(m_dirtyStamps);
        m_dirtyStamps.clear();
        Internal::MarkMembershipChanges(oldFootprints.m_height, currentFootprints.m_height, DirtyHeight, affected);
        Internal::MarkMembershipChanges(oldFootprints.m_surface, currentFootprints.m_surface, DirtySurface, affected);
        Internal::MarkMembershipChanges(oldFootprints.m_existence, currentFootprints.m_existence, DirtyExistence, affected);
        Internal::MarkMembershipChanges(oldFootprints.m_gapQueries, currentFootprints.m_gapQueries, DirtyExistence, affected);
        Internal::MarkMembershipChanges(oldFootprints.m_gapRendering, currentFootprints.m_gapRendering, DirtyExistence, affected);
        // Atomic ownership exchange publishes references, mapping, keys, transforms
        // and pixels together. Standard shared_ptr atomics need not be lock-free, but
        // no application lock encloses external calls.
        m_queryState.exchange(QueryStatePtr(replacement), std::memory_order_acq_rel);
        m_publishedCollisionGridSpacing = std::isfinite(collisionGridSpacing) && collisionGridSpacing > 0.0f ? collisionGridSpacing : 0.0f;
        if (previous->m_sourceEntityId != replacement->m_sourceEntityId || previous->m_regionEntityId != replacement->m_regionEntityId ||
            previous->m_regionBounds != replacement->m_regionBounds)
        {
            // Mapping/reference changes affect the base and every stamp, including an
            // empty stamp set. Keep old/new region contexts separate; the old shape may
            // already be gone when we dispatch.
            QueueHeightRegionChange(*previous);
            QueueHeightRegionChange(*replacement);
        }
        if (previous->m_sourceEntityId != replacement->m_sourceEntityId || previous->m_regionEntityId != replacement->m_regionEntityId ||
            previous->m_regionBounds != replacement->m_regionBounds ||
            !SurfacePalettesEqual(previous->m_surfacePalette, replacement->m_surfacePalette))
        {
            QueueSurfaceRegionChange(*previous);
            QueueSurfaceRegionChange(*replacement);
        }
        const auto visitFootprints = [&](AZ::EntityId id, auto member, auto visit)
        {
            using Side = AZStd::pair<const QueryState*, const Internal::PublicationFootprints*>;
            for (const auto& [state, footprints] : { Side{ previous.get(), &oldFootprints }, Side{ replacement.get(), &currentFootprints } })
            {
                const auto& bounds = footprints->*member;
                if (const auto found = bounds.find(id); found != bounds.end())
                    visit(*state, found->second, *footprints);
            }
        };
        const auto queueSurface = [this](const auto& state, const auto& bounds, const auto&)
        {
            QueueSurfaceFootprintChange(state, bounds);
        };
        const auto queueHeight = [this](const auto& state, const auto& bounds, const auto&)
        {
            m_pendingHeightTerrain.AddFootprint(state.m_regionEntityId, state.m_regionBounds, bounds);
        };
        for (const auto& [id, dirty] : affected)
        {
            if ((dirty & DirtyHeight) != 0)
            {
                HeightmapStampFootprintChange change;
                change.m_stampEntityId = id;
                change.m_address = m_address;
                change.m_compositionSession = m_session;
                change.m_snapshotRevision = replacement->m_revision;
                if (const auto old = oldFootprints.m_height.find(id); old != oldFootprints.m_height.end())
                {
                    change.m_previousBounds = old->second;
                    change.m_previousRegionEntityId = previous->m_regionEntityId;
                    change.m_previousRegionBounds = previous->m_regionBounds;
                }
                if (const auto current = currentFootprints.m_height.find(id); current != currentFootprints.m_height.end())
                {
                    change.m_currentBounds = current->second;
                    change.m_currentRegionEntityId = replacement->m_regionEntityId;
                    change.m_currentRegionBounds = replacement->m_regionBounds;
                }
                if (change.m_previousBounds.IsValid() || change.m_currentBounds.IsValid())
                {
                    QueueHeightFootprintChange(change);
                }
                visitFootprints(id, &Internal::PublicationFootprints::m_height,
                    [&](const auto& state, const auto& heightBounds, const auto& footprints)
                    {
                        if (!heightBounds.IsValid())
                            return;
                        for (const auto& [cutoutId, cutoutBounds] : footprints.m_cutouts)
                        {
                            const AZ::Aabb overlap = IntersectFootprintsXY(heightBounds, cutoutBounds);
                            if (overlap.IsValid())
                                QueueSurfaceFootprintChange(state, overlap);
                        }
                    });
            }
            if ((dirty & DirtySurface) != 0)
                visitFootprints(id, &Internal::PublicationFootprints::m_surface, queueSurface);
            if ((dirty & DirtyExistence) != 0)
            {
                visitFootprints(id, &Internal::PublicationFootprints::m_existence,
                    [&](const auto& state, const auto& bounds, const auto& footprints)
                    {
                        queueHeight(state, bounds, footprints);
                        queueSurface(state, bounds, footprints);
                    });
                visitFootprints(id, &Internal::PublicationFootprints::m_gapQueries, queueHeight);
                visitFootprints(id, &Internal::PublicationFootprints::m_gapRendering, queueSurface);
            }
        }
    }

    HeightmapReconstructionDataPtr TerrainCompositionGradientComponent::AcquireHeightmapReconstruction(
        const HeightmapDataPtr& source, HeightmapSamplingMode mode, float radius)
    {
        if (!source || mode != HeightmapSamplingMode::SmoothCubic || radius <= 0.0f)
        {
            return {};
        }

        for (auto entry = m_reconstructionCache.begin(); entry != m_reconstructionCache.end();)
        {
            const auto cachedSource = entry->m_source.lock();
            const auto cachedReconstruction = entry->m_reconstruction.lock();
            if (!cachedSource || !cachedReconstruction)
            {
                entry = m_reconstructionCache.erase(entry);
                continue;
            }
            if (cachedSource == source && entry->m_sourceRevision == source->m_revision && entry->m_mode == mode &&
                entry->m_radius == radius)
            {
                return cachedReconstruction;
            }
            ++entry;
        }

        auto reconstruction = CreateHeightmapReconstruction(*source, radius);
        if (reconstruction)
        {
            m_reconstructionCache.push_back({ source, reconstruction, source->m_revision, mode, radius });
        }
        return reconstruction;
    }

    AZStd::vector<HeightmapStampRegistrationData> TerrainCompositionGradientComponent::GetRegisteredStamps() const
    {
        if (!m_controlThread.Check()) return {};
        return Internal::RegistrationValues(m_registrations.Get<HeightmapStampRegistrationData>());
    }

    AZStd::vector<TerrainMeshCutoutRegistrationData> TerrainCompositionGradientComponent::GetRegisteredMeshCutouts() const
    {
        if (!m_controlThread.Check()) return {};
        return Internal::RegistrationValues(m_registrations.Get<TerrainMeshCutoutRegistrationData>());
    }

    AZStd::vector<TerrainMeshHeightStampRegistrationData> TerrainCompositionGradientComponent::GetRegisteredMeshHeightStamps() const
    {
        if (!m_controlThread.Check()) return {};
        return Internal::RegistrationValues(m_registrations.Get<TerrainMeshHeightStampRegistrationData>());
    }

    size_t TerrainCompositionGradientComponent::GetOrderingClaimCount(AZStd::string_view stableOrderKey) const
    {
        if (!m_controlThread.Check() || stableOrderKey.empty())
            return 0;
        size_t count = 0;
        for (const auto& [id, registration] : m_registrations.Get<HeightmapStampRegistrationData>())
        {
            (void)id;
            count += registration.m_configuration.GetRuntimeOrderKey() == stableOrderKey;
        }
        for (const auto& [id, registration] : m_registrations.Get<TerrainMeshCutoutRegistrationData>())
        {
            (void)id;
            count += registration.m_configuration.GetRuntimeOrderKey() == stableOrderKey;
        }
        for (const auto& [id, registration] : m_registrations.Get<TerrainMeshHeightStampRegistrationData>())
        {
            (void)id;
            count += registration.m_configuration.GetRuntimeOrderKey() == stableOrderKey;
        }
        return count;
    }

    AZ::Aabb TerrainCompositionGradientComponent::GetTargetRegionBounds() const
    {
        return GetQueryState()->m_regionBounds;
    }
} // namespace TerrainCompositor
