#include "CompositionPreparation.h"

#include <TerrainCompositor/Components/TerrainCompositionGradientComponent.h>
#include <TerrainCompositor/HeightmapStampIdentity.h>
#include <AzCore/std/algorithm.h>

namespace TerrainCompositor::Internal
{
    namespace
    {
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

    }

    CompositionPreparation PrepareComposition(const CompositionRegistrations& registrations,
        const TerrainCompositionConfig& configuration, const AZ::Aabb& regionBounds, const AZ::Uuid& session,
        float collisionGridSpacing, const AZStd::unordered_set<AZ::EntityId>& nonUniformScaleEntities)
    {
        CompositionPreparation result;
        auto& query = result.m_query;
        query.m_regionBounds = regionBounds;
        query.m_regionMapping = PrepareHeightmapRegionMapping(regionBounds);
        query.m_surfacePalette = PrepareSurfacePalette(configuration.m_surfacePalette, configuration.m_baseSurfaceWeights);
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
        collectClaims(registrations.Get<HeightmapStampRegistrationData>());
        collectClaims(registrations.Get<TerrainMeshCutoutRegistrationData>());
        collectClaims(registrations.Get<TerrainMeshHeightStampRegistrationData>());
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
                result.m_collisions.emplace_back(key, AZStd::move(ids));
            }
        }
        const auto hasCollision = [&claims](const AZStd::string& key)
        {
            const auto found = claims.find(key);
            return found != claims.end() && found->second.size() > 1;
        };
        const auto imageCanContribute = [&hasCollision](const PreparedStampPlacement& placement)
        {
            return !placement.m_stableOrderKey.empty() && !hasCollision(placement.m_stableOrderKey) &&
                placement.m_edgeInset < placement.m_halfWidth && placement.m_edgeInset < placement.m_halfDepth;
        };
        for (const auto& [id, registration] : registrations.Get<HeightmapStampRegistrationData>())
        {
            const bool hasNonUniformScale = nonUniformScaleEntities.contains(id);
            PreparedHeightmapStamp prepared;
            const auto validation = PrepareHeightmapStamp(registration, hasNonUniformScale, prepared);
            result.m_diagnostics.emplace_back(RegistrationDiagnostic::Height, id, registration,
                validation == HeightmapStampValidation::Valid ? "" : GetHeightmapStampValidationMessage(validation),
                "Stamp %s contributes nothing: %s");
            if (validation == HeightmapStampValidation::Valid && imageCanContribute(prepared.m_placement) &&
                prepared.m_image && prepared.m_strength > 0.0)
            {
                PreparedHeightContributor contributor;
                contributor.m_type = PreparedHeightContributor::Type::Image;
                contributor.m_image = AZStd::move(prepared);
                query.m_heightContributors.push_back(AZStd::move(contributor));
            }

            PreparedSurfaceStamp preparedSurface;
            HeightmapStampValidation surfacePlacement = HeightmapStampValidation::Valid;
            const auto surfaceValidation = PrepareSurfaceStamp(
                registration,
                hasNonUniformScale,
                query.m_surfacePalette,
                preparedSurface,
                &surfacePlacement);
            const AZStd::string surfaceReason = surfaceValidation == SurfaceStampValidation::Placement
                ? GetHeightmapStampValidationMessage(surfacePlacement)
                : surfaceValidation != SurfaceStampValidation::Valid && surfaceValidation != SurfaceStampValidation::Absent
                    ? GetSurfaceStampValidationMessage(surfaceValidation) : "";
            result.m_diagnostics.emplace_back(RegistrationDiagnostic::Surface, id, registration,
                surfaceReason, "Stamp %s contributes no surface data: %s");
            if (surfaceValidation == SurfaceStampValidation::Valid && imageCanContribute(preparedSurface.m_placement) &&
                preparedSurface.m_surfaceIdA && preparedSurface.m_strength > 0.0)
            {
                query.m_surfaceStamps.push_back(AZStd::move(preparedSurface));
            }

            PreparedTerrainExistenceStamp preparedExistence;
            HeightmapStampValidation existencePlacement = HeightmapStampValidation::Valid;
            const auto existenceValidation = PrepareTerrainExistenceStamp(
                registration, hasNonUniformScale, preparedExistence, &existencePlacement);
            const AZStd::string existenceReason = existenceValidation == TerrainExistenceStampValidation::Placement
                ? GetHeightmapStampValidationMessage(existencePlacement)
                : existenceValidation != TerrainExistenceStampValidation::Valid && existenceValidation != TerrainExistenceStampValidation::Absent
                    ? GetTerrainExistenceStampValidationMessage(existenceValidation) : "";
            result.m_diagnostics.emplace_back(RegistrationDiagnostic::Existence, id, registration,
                existenceReason, "Stamp %s contributes no terrain existence data: %s");
            if (existenceValidation == TerrainExistenceStampValidation::Valid && imageCanContribute(preparedExistence.m_placement))
            {
                PreparedTerrainExistenceContributor contributor;
                contributor.m_type = PreparedTerrainExistenceContributor::Type::ImageMask;
                contributor.m_imageMask = AZStd::move(preparedExistence);
                query.m_existenceContributors.push_back(AZStd::move(contributor));
            }
        }
        for (const auto& [id, registration] : registrations.Get<TerrainMeshHeightStampRegistrationData>())
        {
            PreparedTerrainMeshHeightStamp prepared;
            const auto validation = PrepareTerrainMeshHeightStamp(registration, registration.m_hasNonUniformScale, prepared);
            const AZStd::string reason = validation == TerrainMeshHeightStampPlacementValidation::Valid ? ""
                : validation == TerrainMeshHeightStampPlacementValidation::DataUnavailable
                    ? GetTerrainMeshHeightDataDiagnostic(registration.m_mesh) : GetTerrainMeshHeightStampPlacementValidationMessage(validation);
            result.m_diagnostics.emplace_back(RegistrationDiagnostic::MeshHeight, id, registration,
                reason, "Terrain mesh height stamp %s contributes nothing: %s");
            if (validation == TerrainMeshHeightStampPlacementValidation::Valid && !prepared.m_stableOrderKey.empty() &&
                !hasCollision(prepared.m_stableOrderKey) && prepared.m_data)
            {
                PreparedTerrainMeshHeightGap gap;
                if (PrepareTerrainMeshHeightGap(prepared, collisionGridSpacing, query.m_regionBounds, gap))
                {
                    gap.m_compositionSession = session;
                    if (gap.m_affectTerrainCollisionQueries)
                    {
                        PreparedTerrainExistenceContributor existenceContributor;
                        existenceContributor.m_type = PreparedTerrainExistenceContributor::Type::MeshHeightGap;
                        existenceContributor.m_meshHeightGap = gap;
                        query.m_existenceContributors.push_back(AZStd::move(existenceContributor));
                    }
                    query.m_meshHeightGaps.push_back(AZStd::move(gap));
                }
                if (prepared.m_strength > 0.0)
                {
                    PreparedHeightContributor contributor;
                    contributor.m_type = PreparedHeightContributor::Type::Mesh;
                    contributor.m_mesh = AZStd::move(prepared);
                    query.m_heightContributors.push_back(AZStd::move(contributor));
                }
            }
        }

        for (const auto& [id, registration] : registrations.Get<TerrainMeshCutoutRegistrationData>())
        {
            PreparedTerrainMeshCutout prepared;
            const auto validation = PrepareTerrainMeshCutout(registration, registration.m_hasNonUniformScale, prepared);
            const AZStd::string reason = validation == TerrainMeshCutoutPlacementValidation::Valid ? ""
                : validation == TerrainMeshCutoutPlacementValidation::DataUnavailable
                    ? GetMeshCutoutDataDiagnostic(registration.m_mesh) : GetTerrainMeshCutoutPlacementValidationMessage(validation);
            result.m_diagnostics.emplace_back(RegistrationDiagnostic::Cutout, id, registration,
                reason, "Terrain mesh cutout %s contributes nothing: %s");
            if (validation == TerrainMeshCutoutPlacementValidation::Valid && !hasCollision(prepared.m_stableOrderKey))
            {
                if (prepared.m_affectTerrainRendering)
                {
                    result.m_renderCutouts.push_back(prepared);
                }
                if (prepared.m_affectTerrainCollisionQueries)
                {
                    ApplyTerrainMeshCutoutCollisionCellPadding(prepared, collisionGridSpacing);

                    PreparedTerrainExistenceContributor contributor;
                    contributor.m_type = PreparedTerrainExistenceContributor::Type::MeshCutout;
                    contributor.m_meshCutout = AZStd::move(prepared);
                    query.m_existenceContributors.push_back(AZStd::move(contributor));
                }
            }
        }

        // Candidate order preserves first-claim footprint selection before the blend streams are sorted.
        result.m_footprints = PublicationFootprints(query);
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
        sortContributors(query.m_heightContributors, contributorOrder);
        sortContributors(query.m_existenceContributors, contributorOrder);
        sortContributors(query.m_surfaceStamps, [](const auto& value)
        {
            return AZStd::pair(value.m_placement.m_priority, AZStd::string_view(value.m_placement.m_stableOrderKey));
        });
        sortContributors(query.m_meshHeightGaps, [](const auto& value)
        {
            return AZStd::pair(value.m_priority, AZStd::string_view(value.m_stableOrderKey));
        });

        return result;
    }
}
