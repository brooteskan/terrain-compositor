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
        if (!m_active) { m_controlThread.BindForActivation(); }
        if (!m_controlThread.Check()) { return; }
        Deactivate();
        m_active = true;
        m_registration.m_stampEntityId = stampEntityId;
        AZ::SystemTickBus::Handler::BusConnect();
        Update(configuration, worldTransform, transformAvailable, identityPending);
    }

    void HeightmapStampRegistration::Update(
        const HeightmapStampConfig& configuration, const AZ::Transform& worldTransform, bool transformAvailable, bool identityPending)
    {
        if (!m_controlThread.Check() || !m_active)
        {
            return;
        }
        AzFramework::EntityContextId contextId = AzFramework::EntityContextId::CreateNull();
        if (m_registration.m_stampEntityId.IsValid())
        {
            AzFramework::EntityIdContextQueryBus::EventResult(
                contextId, m_registration.m_stampEntityId, &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
        }
        const TerrainCompositionAddress address{ contextId, configuration.m_targetCompositionEntityId };
        if (address != m_address)
        {
            DisconnectTarget();
            m_registration.m_registrationId = AZ::Uuid::CreateRandom();
            m_registration.m_updateRevision = 0;
        }
        m_registration.m_contextId = contextId;
        m_registration.m_configuration = configuration;
        m_registration.m_worldTransform = worldTransform;
        m_registration.m_transformAvailable = transformAvailable;
        m_registration.m_identityPending = identityPending;
        m_address = address;
        UpdateHeightmapAsset();
        UpdateSurfaceAssets();
        UpdateHoleAsset();

        // Unknown contexts fail closed. The component can call Update once entity-context ownership is established.
        if (!contextId.IsNull() && address.second.IsValid() && m_registration.m_stampEntityId.IsValid())
        {
            if (!TerrainCompositionNotificationBus::Handler::BusIsConnected())
            {
                TerrainCompositionNotificationBus::Handler::BusConnect(address);
            }
            // Connect notifications before trying the existing provider: neither activation order loses a registration.
            OnCompositionAvailable({});
        }
        else
        {
            // Diagnose placement even while the author has not selected a valid composition target.
            ValidateCurrentStamp();
        }
    }

    void HeightmapStampRegistration::UpdateHeightmapAsset()
    {
        const auto assetId = m_registration.m_configuration.m_heightmapAsset.GetId();
        if (assetId == m_selectedAssetId && ((m_heightmapSource && m_heightmapChanged.IsConnected()) || !assetId.IsValid()))
        {
            return; // Transform, blend, or target edits never reload or copy shared image samples.
        }
        const bool reportUnavailable = assetId != m_selectedAssetId ||
            m_registration.m_heightmap.m_status != HeightmapDataStatus::Error;
        const AZ::u64 generation = ++m_assetGeneration;
        m_heightmapChanged.Disconnect();
        m_heightmapSource.reset();
        m_selectedAssetId = assetId;
        m_registration.m_heightmap = {};
        if (!assetId.IsValid())
        {
            return;
        }
        auto* cache = HeightmapDataCacheInterface::Get();
        if (!cache)
        {
            m_registration.m_heightmap.m_status = HeightmapDataStatus::Error;
            AZ_Warning("HeightmapData", !reportUnavailable,
                "Cannot load heightmap %s: TerrainCompositorSystemComponent must be active before stamp activation.",
                assetId.ToFixedString().c_str());
            return;
        }
        m_heightmapSource = cache->Acquire(assetId);
        m_heightmapChanged = HeightmapDataCache::ChangedEvent::Handler(
            [this, generation](const HeightmapDataSnapshot& snapshot)
            {
                if (!m_controlThread.Check() || !m_active || generation != m_assetGeneration)
                {
                    return;
                }
                m_registration.m_heightmap = snapshot;
                if (TerrainCompositionNotificationBus::Handler::BusIsConnected())
                {
                    // Replacing the registration publishes the revision to every matching claim in that
                    // composition, then queues their old/new footprints for regional terrain invalidation.
                    OnCompositionAvailable({});
                }
            });
        HeightmapDataCache::ConnectChangedHandler(m_heightmapSource, m_heightmapChanged);
        m_registration.m_heightmap = HeightmapDataCache::GetSnapshot(m_heightmapSource);
    }

    void HeightmapStampRegistration::UpdateSurfaceAssets()
    {
        const auto& maps = m_registration.m_configuration.m_surfaceMaps;
        UpdateSurfaceAsset(maps.m_surfaceIdAAsset.GetId(), m_surfaceIdASource, m_surfaceIdAChanged,
            m_selectedSurfaceIdA, &m_surfaceIdAGeneration, &HeightmapStampRegistrationData::m_surfaceIdA);
        UpdateSurfaceAsset(maps.m_surfaceIdBAsset.GetId(), m_surfaceIdBSource, m_surfaceIdBChanged,
            m_selectedSurfaceIdB, &m_surfaceIdBGeneration, &HeightmapStampRegistrationData::m_surfaceIdB);
        UpdateSurfaceAsset(maps.m_blendMaskAsset.GetId(), m_surfaceBlendSource, m_surfaceBlendChanged,
            m_selectedSurfaceBlend, &m_surfaceBlendGeneration, &HeightmapStampRegistrationData::m_surfaceBlend);
    }

    void HeightmapStampRegistration::UpdateHoleAsset()
    {
        UpdateSurfaceAsset(m_registration.m_configuration.m_holeMask.m_maskAsset.GetId(),
            m_holeMaskSource, m_holeMaskChanged, m_selectedHoleMask, &m_holeMaskGeneration,
            &HeightmapStampRegistrationData::m_holeMask);
    }

    void HeightmapStampRegistration::UpdateSurfaceAsset(const AZ::Data::AssetId& assetId,
        HeightmapDataCache::Handle& source, HeightmapDataCache::ChangedEvent::Handler& changed,
        AZ::Data::AssetId& selectedAssetId, AZ::u64* generation,
        HeightmapDataSnapshot HeightmapStampRegistrationData::* snapshotMember)
    {
        AZ_Assert(generation, "Surface asset generation pointer is required.");
        if (!generation)
        {
            return;
        }
        if (assetId == selectedAssetId && ((source && changed.IsConnected()) || !assetId.IsValid()))
        {
            return;
        }
        const AZ::u64 expectedGeneration = ++(*generation);
        changed.Disconnect();
        source.reset();
        selectedAssetId = assetId;
        (m_registration.*snapshotMember) = {};
        if (!assetId.IsValid())
        {
            return;
        }
        auto* cache = HeightmapDataCacheInterface::Get();
        if (!cache)
        {
            (m_registration.*snapshotMember).m_status = HeightmapDataStatus::Error;
            AZ_Warning("SurfaceMapData", false,
                "Cannot load surface map %s: TerrainCompositorSystemComponent must be active before stamp activation.",
                assetId.ToFixedString().c_str());
            return;
        }
        source = cache->Acquire(assetId);
        changed = HeightmapDataCache::ChangedEvent::Handler(
            [this, expectedGeneration, generation, snapshotMember](const HeightmapDataSnapshot& snapshot)
            {
                if (!m_controlThread.Check() || !m_active || expectedGeneration != *generation)
                {
                    return;
                }
                m_registration.*snapshotMember = snapshot;
                if (TerrainCompositionNotificationBus::Handler::BusIsConnected())
                {
                    OnCompositionAvailable({});
                }
            });
        HeightmapDataCache::ConnectChangedHandler(source, changed);
        m_registration.*snapshotMember = HeightmapDataCache::GetSnapshot(source);
    }

    void HeightmapStampRegistration::DisconnectTarget()
    {
        TerrainCompositionNotificationBus::Handler::BusDisconnect();
        if (m_address.second.IsValid() && !m_address.first.IsNull())
        {
            TerrainCompositionRequestBus::Event(
                m_address, &TerrainCompositionRequestBus::Events::UnregisterStamp, m_registration.m_stampEntityId,
                m_registration.m_registrationId, m_registration.m_compositionSession);
        }
        m_registered = false;
        m_address = TerrainCompositionAddress{};
        m_registration.m_compositionSession = {};
    }

    void HeightmapStampRegistration::Deactivate()
    {
        if (!m_active) { return; }
        if (!m_controlThread.Check()) { return; }
        m_active = false;
        AZ::SystemTickBus::Handler::BusDisconnect();
        ++m_assetGeneration;
        ++m_surfaceIdAGeneration;
        ++m_surfaceIdBGeneration;
        ++m_surfaceBlendGeneration;
        ++m_holeMaskGeneration;
        m_heightmapChanged.Disconnect();
        m_surfaceIdAChanged.Disconnect();
        m_surfaceIdBChanged.Disconnect();
        m_surfaceBlendChanged.Disconnect();
        m_holeMaskChanged.Disconnect();
        m_heightmapSource.reset();
        m_surfaceIdASource.reset();
        m_surfaceIdBSource.reset();
        m_surfaceBlendSource.reset();
        m_holeMaskSource.reset();
        m_selectedAssetId = {};
        m_selectedSurfaceIdA = {};
        m_selectedSurfaceIdB = {};
        m_selectedSurfaceBlend = {};
        m_selectedHoleMask = {};
        DisconnectTarget();
        m_registration = HeightmapStampRegistrationData{};
        m_validation = HeightmapStampValidation::Valid;
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

    void HeightmapStampRegistration::ValidateCurrentStamp()
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

    void HeightmapStampRegistration::OnCompositionAvailable(const AZ::Uuid& expectedSession)
    {
        if (!m_controlThread.Check() || !m_active)
        {
            return;
        }
        // Invalid claims remain registered but never contribute to query snapshots.
        AZ::Uuid session{};
        TerrainCompositionRequestBus::EventResult(
            session, m_address, &TerrainCompositionRequestBus::Events::GetCompositionSession);
        if (session.IsNull())
        {
            m_registered = false;
            ValidateCurrentStamp();
            return;
        }
        if (!expectedSession.IsNull() && expectedSession != session) { return; }
        m_registered = false;
        m_registration.m_compositionSession = session;
        ++m_registration.m_updateRevision;
        TerrainCompositionRequestBus::EventResult(
            m_registered, m_address, &TerrainCompositionRequestBus::Events::RegisterStamp, m_registration);
    }

    void HeightmapStampRegistration::OnCompositionUnavailable(const AZ::Uuid& session)
    {
        if (!m_controlThread.Check() || session != m_registration.m_compositionSession) { return; }
        m_registered = false;
        m_registration.m_compositionSession = {};
    }

    void HeightmapStampRegistration::OnSystemTick()
    {
        if (!m_controlThread.Check() || !m_active) { return; }
        AzFramework::EntityContextId context{};
        AzFramework::EntityIdContextQueryBus::EventResult(
            context, m_registration.m_stampEntityId, &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
        const bool missingCacheSubscription = HeightmapDataCacheInterface::Get() &&
            ((m_selectedAssetId.IsValid() && !m_heightmapChanged.IsConnected()) ||
             (m_selectedSurfaceIdA.IsValid() && !m_surfaceIdAChanged.IsConnected()) ||
             (m_selectedSurfaceIdB.IsValid() && !m_surfaceIdBChanged.IsConnected()) ||
             (m_selectedSurfaceBlend.IsValid() && !m_surfaceBlendChanged.IsConnected()) ||
             (m_selectedHoleMask.IsValid() && !m_holeMaskChanged.IsConnected()));
        if (context != m_address.first || missingCacheSubscription)
        {
            // Retry late context ownership/cache restart using current values, never a captured old target.
            const auto configuration = m_registration.m_configuration;
            Update(configuration, m_registration.m_worldTransform, m_registration.m_transformAvailable,
                m_registration.m_identityPending);
        }
    }
} // namespace TerrainCompositor
