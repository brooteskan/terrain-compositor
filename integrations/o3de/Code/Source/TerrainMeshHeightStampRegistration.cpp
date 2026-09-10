#include <TerrainCompositor/TerrainMeshHeightStampRegistration.h>

#include <Atom/RPI.Public/Scene.h>
#include <AzFramework/Terrain/TerrainDataRequestBus.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>
#include <TerrainCompositor/TerrainMeshCutoutFeatureProcessor.h>
#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>
#include <TerrainCompositor/TerrainMeshHeightStampSampling.h>

namespace TerrainCompositor
{
    namespace
    {
        AZStd::string FormatBounds(const AZ::Aabb& bounds)
        {
            if (!bounds.IsValid())
            {
                return "unavailable";
            }
            return AZStd::string::format(
                "[(%.3f, %.3f, %.3f) - (%.3f, %.3f, %.3f)]",
                bounds.GetMin().GetX(), bounds.GetMin().GetY(), bounds.GetMin().GetZ(),
                bounds.GetMax().GetX(), bounds.GetMax().GetY(), bounds.GetMax().GetZ());
        }
    } // namespace

    TerrainMeshHeightStampRegistration::~TerrainMeshHeightStampRegistration()
    {
        Deactivate();
    }

    void TerrainMeshHeightStampRegistration::Activate(
        AZ::EntityId entityId,
        const TerrainMeshHeightStampConfig& configuration,
        const AZ::Transform& worldTransform,
        bool transformAvailable,
        bool identityPending,
        bool hasNonUniformScale)
    {
        ActivateClient(entityId, configuration, worldTransform, transformAvailable, identityPending, hasNonUniformScale);
    }

    void TerrainMeshHeightStampRegistration::Update(
        const TerrainMeshHeightStampConfig& configuration,
        const AZ::Transform& worldTransform,
        bool transformAvailable,
        bool identityPending,
        bool hasNonUniformScale)
    {
        UpdateClient(configuration, worldTransform, transformAvailable, identityPending, hasNonUniformScale);
    }

    void TerrainMeshHeightStampRegistration::UpdateAssets()
    {
        RefreshAsset(m_meshAsset, m_registration.m_configuration.m_terrainMeshAsset.GetId(), m_registration.m_mesh);
    }

    void TerrainMeshHeightStampRegistration::Deactivate()
    {
        DeactivateClient();
    }

    bool TerrainMeshHeightStampRegistration::IsRegistered() const
    {
        return m_controlThread.Check() && m_registered;
    }

    TerrainMeshHeightStampRegistrationData TerrainMeshHeightStampRegistration::GetRegistrationData() const
    {
        return m_controlThread.Check() ? m_registration : TerrainMeshHeightStampRegistrationData{};
    }

    AZStd::string TerrainMeshHeightStampRegistration::GetStatusMessage() const
    {
        if (!m_controlThread.Check())
            return "Unavailable off the control thread.";
        if (!m_active)
            return "Inactive: no terrain mesh height contribution.";
        if (!m_registration.m_configuration.m_terrainMeshAsset.GetId().IsValid())
            return "Select a Terrain Mesh model asset.";
        if (!m_address.second.IsValid())
            return "Select a Target Composition entity.";
        if (m_address.first.IsNull())
            return "Waiting for entity context ownership.";
        if (!m_registered)
            return "Target Composition is unavailable in this entity context.";
        if (m_registration.m_identityPending)
            return "Waiting for prefab propagation/undo to resolve ordering identity.";
        const auto key = m_registration.m_configuration.GetRuntimeOrderKey();
        if (key.empty())
            return "Ordering identity is unresolved or malformed.";
        size_t claims = 0;
        TerrainCompositionRequestBus::EventResult(claims, m_address, &TerrainCompositionRequestBus::Events::GetOrderingClaimCount, key);
        if (claims > 1)
            return "Duplicate ordering identity: all conflicting contributors are suppressed.";
        if (m_registration.m_mesh.m_status != TerrainMeshHeightDataStatus::Ready)
        {
            const auto diagnostic = GetTerrainMeshHeightDataDiagnostic(m_registration.m_mesh);
            return diagnostic.empty() ? "Loading and preparing regular-grid terrain geometry asynchronously." : diagnostic;
        }
        PreparedTerrainMeshHeightStamp prepared;
        const auto validation = PrepareTerrainMeshHeightStamp(m_registration, m_registration.m_hasNonUniformScale, prepared);
        if (validation != TerrainMeshHeightStampPlacementValidation::Valid)
        {
            return GetTerrainMeshHeightStampPlacementValidationMessage(validation);
        }
        float collisionGridSpacing = 0.0f;
        AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
            collisionGridSpacing, &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightQueryResolution);
        AZ::Aabb regionBounds = AZ::Aabb::CreateNull();
        TerrainCompositionRequestBus::EventResult(
            regionBounds, m_address, &TerrainCompositionRequestBus::Events::GetTargetRegionBounds);

        PreparedTerrainMeshHeightGap gap;
        const bool hasPreparedGap = PrepareTerrainMeshHeightGap(prepared, collisionGridSpacing, regionBounds, gap);
        const bool cutOut = prepared.m_uncoveredAreaPolicy == TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain;
        const char* removalState = !cutOut ? "neutral (Preserve Lower Terrain)"
            : prepared.m_data->m_uncoveredCellCount == 0 ? "none (no uncovered cells)"
            : hasPreparedGap ? "prepared" : "rejected";

        AZ::u64 scenePublicationRevision = 0;
        AZ::u64 activePublicationRevision = 0;
        bool renderPublished = false;
        bool renderAdmitted = !prepared.m_affectTerrainRendering;
        AZ::u32 resourceRejects = 0;
        AZ::u64 resourceFailures = 0;
        AZ::u64 staleResults = 0;
        if (hasPreparedGap)
        {
            gap.m_compositionSession = m_registration.m_compositionSession;
        }
        if (AZ::RPI::Scene* scene = AZ::RPI::Scene::GetSceneForEntityContextId(m_address.first))
        {
            if (auto* registry = AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get())
            {
                if (const auto channel = registry->FindSceneChannel(scene))
                {
                    const auto snapshot = channel->m_snapshot.load(std::memory_order_acquire);
                    const auto activation = channel->m_activation.load(std::memory_order_acquire);
                    if (snapshot)
                    {
                        scenePublicationRevision = snapshot->m_revision;
                        renderPublished = AZStd::any_of(
                            snapshot->m_meshHeightGaps.begin(), snapshot->m_meshHeightGaps.end(),
                            [this](const auto& candidate)
                            {
                                return candidate.m_entityId == m_registration.m_stampEntityId &&
                                    candidate.m_compositionSession == m_registration.m_compositionSession;
                            });
                    }
                    if (activation)
                    {
                        activePublicationRevision = activation->m_revision;
                        renderAdmitted = hasPreparedGap && IsTerrainMeshHeightGapAdmitted(gap, activation->m_gaps);
                    }
                }
            }
            if (auto* featureProcessor = scene->GetFeatureProcessor<TerrainMeshCutoutFeatureProcessor>())
            {
                const auto& statistics = featureProcessor->GetStatistics();
                resourceRejects = statistics.m_gapRejectedContributions;
                resourceFailures = statistics.m_gapResourceFailures;
                staleResults = statistics.m_gapStaleResults;
            }
        }

        const size_t conservativeCells = hasPreparedGap && gap.m_collisionCells ? gap.m_collisionCells->m_cells.size() : 0;
        return AZStd::string::format(
            "Ready: asset revision %llu; registration update %llu; %u x %u vertices / %u x %u cells at (%.3f, %.3f) m; "
            "%u covered, %u accepted authored gaps. Policy: %s; consumers: rendering %s, collision/queries %s; removal %s. "
            "Local bounds %s; world height bounds %s; exact removal bounds %s; conservative collision bounds %s; "
            "%zu conservative cells at live %.3f m spacing; terrain region %s. Render publication %llu (%s), "
            "activation %llu (%s); resource rejects %u, failures %llu, stale results %llu.",
            static_cast<unsigned long long>(prepared.m_data->m_revision),
            static_cast<unsigned long long>(m_registration.m_updateRevision),
            prepared.m_data->m_width, prepared.m_data->m_height,
            prepared.m_data->m_width - 1, prepared.m_data->m_height - 1,
            prepared.m_data->m_gridSpacing.GetX(), prepared.m_data->m_gridSpacing.GetY(),
            prepared.m_data->m_coveredCellCount, prepared.m_data->m_uncoveredCellCount,
            cutOut ? "Cut Out Terrain" : "Preserve Lower Terrain",
            prepared.m_affectTerrainRendering ? "on" : "off",
            prepared.m_affectTerrainCollisionQueries ? "on" : "off", removalState,
            FormatBounds(prepared.m_data->m_localBounds).c_str(), FormatBounds(prepared.m_worldBounds).c_str(),
            FormatBounds(hasPreparedGap ? gap.m_worldBounds : AZ::Aabb::CreateNull()).c_str(),
            FormatBounds(hasPreparedGap ? gap.m_collisionWorldBounds : AZ::Aabb::CreateNull()).c_str(),
            conservativeCells, collisionGridSpacing, FormatBounds(regionBounds).c_str(),
            static_cast<unsigned long long>(scenePublicationRevision), renderPublished ? "published" : "not published",
            static_cast<unsigned long long>(activePublicationRevision), renderAdmitted ? "admitted" : "not admitted",
            resourceRejects, static_cast<unsigned long long>(resourceFailures), static_cast<unsigned long long>(staleResults));
    }

} // namespace TerrainCompositor
