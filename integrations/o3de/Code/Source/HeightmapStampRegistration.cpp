#include <TerrainCompositor/HeightmapStampRegistration.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>
#include <AzCore/Component/NonUniformScaleBus.h>

namespace TerrainCompositor
{
    HeightmapStampRegistration::~HeightmapStampRegistration()
    {
        Deactivate();
    }

    void HeightmapStampRegistration::Activate(
        AZ::EntityId stampEntityId, const HeightmapStampConfig& configuration,
        const AZ::Transform& worldTransform, bool transformAvailable, bool identityPending)
    {
        ActivateClient(stampEntityId, configuration, worldTransform, transformAvailable, identityPending);
    }

    void HeightmapStampRegistration::Update(
        const HeightmapStampConfig& configuration, const AZ::Transform& worldTransform, bool transformAvailable, bool identityPending)
    {
        UpdateClient(configuration, worldTransform, transformAvailable, identityPending);
    }

    void HeightmapStampRegistration::UpdateAssets()
    {
        const auto& config = m_registration.m_configuration;
        const AZ::Data::AssetId assets[] = { config.m_heightmapAsset.GetId(),
            config.m_surfaceMaps.m_surfaceIdAAsset.GetId(), config.m_surfaceMaps.m_surfaceIdBAsset.GetId(),
            config.m_surfaceMaps.m_blendMaskAsset.GetId(), config.m_holeMask.m_maskAsset.GetId() };
        HeightmapDataSnapshot* snapshots[] = { &m_registration.m_heightmap, &m_registration.m_surfaceIdA,
            &m_registration.m_surfaceIdB, &m_registration.m_surfaceBlend, &m_registration.m_holeMask };
        for (size_t role = 0; role < AZ_ARRAY_SIZE(m_images); ++role)
        {
            const bool reportUnavailable = m_images[role].SelectedAssetId() != assets[role] ||
                snapshots[role]->m_status != HeightmapDataStatus::Error;
            if (!RefreshAsset(m_images[role], assets[role], *snapshots[role]))
            {
                if (role == 0)
                {
                    AZ_Warning("HeightmapData", !reportUnavailable,
                        "Cannot load heightmap %s: TerrainCompositorSystemComponent must be active before stamp activation.",
                        assets[role].ToFixedString().c_str());
                }
                else
                {
                    AZ_Warning("SurfaceMapData", false,
                        "Cannot load surface map %s: TerrainCompositorSystemComponent must be active before stamp activation.",
                        assets[role].ToFixedString().c_str());
                }
            }
        }
    }

    void HeightmapStampRegistration::Deactivate()
    {
        DeactivateClient();
    }

    void HeightmapStampRegistration::ResetAssets()
    {
        for (auto& image : m_images)
            image.Reset();
        m_validation = HeightmapStampValidation::Valid;
    }

    bool HeightmapStampRegistration::NeedsAssetRetry() const
    {
        if (HeightmapDataCacheInterface::Get())
        {
            for (const auto& image : m_images)
            {
                if (image.NeedsRetry())
                    return true;
            }
        }
        return false;
    }

    bool HeightmapStampRegistration::IsRegistered() const
    {
        return m_controlThread.Check() && m_registered;
    }

    AZStd::string HeightmapStampRegistration::GetStatusMessage() const
    {
        if (!m_controlThread.Check()) { return "Unavailable off the control thread."; }
        if (!m_active) { return "Inactive: no stamp contribution."; }
        PreparedHeightmapStamp prepared;
        const auto validation = PrepareHeightmapStamp(m_registration,
            AZ::NonUniformScaleRequestBus::HasHandlers(m_registration.m_stampEntityId), prepared);
        if (validation != HeightmapStampValidation::Valid)
        {
            return GetHeightmapStampValidationMessage(validation);
        }
        if (!m_address.second.IsValid()) { return "Select a Target Composition entity with Terrain Composition Gradient."; }
        if (m_address.first.IsNull()) { return "Waiting for entity context ownership."; }
        if (!m_registered) { return "Target Composition is unavailable in this entity context. Check its component and enabled state."; }
        if (m_registration.m_identityPending) { return "Waiting for prefab propagation/undo to resolve the saved ordering identity."; }
        const auto key = m_registration.m_configuration.GetRuntimeOrderKey();
        if (key.empty()) { return "Unresolved ordering identity. Check Tools support; runtime clones need a persistent identity before activation."; }

        // This optional inspector diagnostic runs only on the control thread, not in publication or sampling.
        // Count all claims, including zero-strength/unready stamps, to match collision suppression exactly.
        size_t matches = 0;
        TerrainCompositionRequestBus::EventResult(
            matches, m_address, &TerrainCompositionRequestBus::Events::GetOrderingClaimCount, key);
        if (matches > 1) { return "Duplicate ordering identity: all conflicting stamps are suppressed. See TerrainComposition diagnostics."; }
        AZ::Aabb region = AZ::Aabb::CreateNull();
        TerrainCompositionRequestBus::EventResult(region, m_address, &TerrainCompositionRequestBus::Events::GetTargetRegionBounds);
        if (PrepareHeightmapRegionMapping(region).m_range <= 0.0)
        {
            return "Target region has no valid elevation range. Check Terrain Composition Gradient's region reference/shape.";
        }
        const bool heightConfigured = m_registration.m_configuration.m_heightmapAsset.GetId().IsValid();
        const bool holeConfigured = m_registration.m_configuration.m_holeMask.m_maskAsset.GetId().IsValid();
        PreparedTerrainExistenceStamp preparedExistence;
        HeightmapStampValidation existencePlacement = HeightmapStampValidation::Valid;
        const auto existenceValidation = PrepareTerrainExistenceStamp(
            m_registration, AZ::NonUniformScaleRequestBus::HasHandlers(m_registration.m_stampEntityId),
            preparedExistence, &existencePlacement);
        if (!heightConfigured)
        {
            if (!holeConfigured)
            {
                return "Select a heightmap and/or Terrain Hole Mask StreamingImage product (unsigned 16-bit TIFF, GSI16).";
            }
            if (existenceValidation == TerrainExistenceStampValidation::Placement)
            {
                return GetHeightmapStampValidationMessage(existencePlacement);
            }
            if (existenceValidation != TerrainExistenceStampValidation::Valid)
            {
                return GetTerrainExistenceStampValidationMessage(existenceValidation);
            }
            return AZStd::string::format("Ready: hole-only %u x %u, revision %llu; %s terrain in active mask samples.",
                preparedExistence.m_mask->m_width, preparedExistence.m_mask->m_height,
                static_cast<unsigned long long>(preparedExistence.m_maskRevision),
                preparedExistence.m_operation == TerrainExistenceOperation::RemoveTerrain ? "removes" : "restores");
        }
        switch (m_registration.m_heightmap.m_status)
        {
        case HeightmapDataStatus::Unassigned: return "Select a heightmap StreamingImage product (unsigned 16-bit TIFF, GSI16).";
        case HeightmapDataStatus::Loading: return "Loading image/mip 0 asynchronously; underlying terrain remains visible.";
        case HeightmapDataStatus::Missing: return "Image product is missing. Restore the source and let Asset Processor finish.";
        case HeightmapDataStatus::Error: return "Image load failed. Check Asset Processor and HeightmapData logs; verify GSI16 import settings.";
        case HeightmapDataStatus::Unsupported: return "Unsupported product. Use GSI16, Linear, R16, power-of-two dimensions, Res Limit 0 and Use Max Res.";
        case HeightmapDataStatus::Ready: break;
        }
        if (!m_registration.m_heightmap.m_data) { return "Image pixels unavailable; underlying terrain remains visible."; }
        if (m_registration.m_configuration.m_strength == 0.0f) { return "Strength is zero: no contribution; underlying terrain is unchanged."; }
        if (prepared.m_placement.m_edgeInset >= prepared.m_placement.m_halfWidth ||
            prepared.m_placement.m_edgeInset >= prepared.m_placement.m_halfDepth)
        {
            return "Edge Inset covers the entire footprint: no contribution; underlying terrain is unchanged.";
        }
        const auto& image = *m_registration.m_heightmap.m_data;
        const auto& maps = m_registration.m_configuration.m_surfaceMaps;
        const bool hasA = maps.m_surfaceIdAAsset.GetId().IsValid();
        const bool hasB = maps.m_surfaceIdBAsset.GetId().IsValid();
        const bool hasBlend = maps.m_blendMaskAsset.GetId().IsValid();
        if (hasA || hasB || hasBlend)
        {
            if (!hasA || hasB != hasBlend)
            {
                return "Height ready; surface maps incomplete. Assign ID A, and assign ID B together with Blend Mask.";
            }
            if (m_registration.m_surfaceIdA.m_status != HeightmapDataStatus::Ready || !m_registration.m_surfaceIdA.m_data ||
                (hasB && (m_registration.m_surfaceIdB.m_status != HeightmapDataStatus::Ready || !m_registration.m_surfaceIdB.m_data ||
                    m_registration.m_surfaceBlend.m_status != HeightmapDataStatus::Ready || !m_registration.m_surfaceBlend.m_data)))
            {
                return "Height ready; one or more surface maps are unavailable. Height remains active.";
            }
        }
        if (holeConfigured && existenceValidation != TerrainExistenceStampValidation::Valid)
        {
            return AZStd::string::format("Height ready; terrain hole mask unavailable: %s",
                existenceValidation == TerrainExistenceStampValidation::Placement
                    ? GetHeightmapStampValidationMessage(existencePlacement)
                    : GetTerrainExistenceStampValidationMessage(existenceValidation));
        }
        return AZStd::string::format("Ready: height %u x %u, mip 0, revision %llu%s Registered; see composition status for palette/source availability.",
            image.m_width, image.m_height, static_cast<unsigned long long>(image.m_revision),
            hasA ? "; surface maps loaded." : ".");
    }

    void HeightmapStampRegistration::ValidateUnavailable()
    {
        PreparedHeightmapStamp prepared;
        const auto validation = PrepareHeightmapStamp(m_registration,
            AZ::NonUniformScaleRequestBus::HasHandlers(m_registration.m_stampEntityId), prepared);
        if (validation != HeightmapStampValidation::Valid && validation != m_validation)
        {
            AZ_Warning("HeightmapStamp", false, "Stamp %s contributes nothing: %s",
                m_registration.m_stampEntityId.ToString().c_str(), GetHeightmapStampValidationMessage(validation));
        }
        m_validation = validation;
    }

} // namespace TerrainCompositor
