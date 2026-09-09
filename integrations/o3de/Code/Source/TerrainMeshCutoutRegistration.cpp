#include <TerrainCompositor/TerrainMeshCutoutRegistration.h>

#include <TerrainCompositor/TerrainMeshCutoutSampling.h>

namespace TerrainCompositor
{
    TerrainMeshCutoutRegistration::~TerrainMeshCutoutRegistration()
    {
        Deactivate();
    }

    void TerrainMeshCutoutRegistration::Activate(
        AZ::EntityId entityId, const TerrainMeshCutoutConfig& configuration,
        const AZ::Transform& worldTransform, bool transformAvailable, bool identityPending,
        bool hasNonUniformScale)
    {
        if (!m_active) m_controlThread.BindForActivation();
        if (!m_controlThread.Check()) return;
        Deactivate();
        m_active = true;
        m_registration.m_cutoutEntityId = entityId;
        AZ::SystemTickBus::Handler::BusConnect();
        Update(configuration, worldTransform, transformAvailable, identityPending, hasNonUniformScale);
    }

    void TerrainMeshCutoutRegistration::Update(
        const TerrainMeshCutoutConfig& configuration, const AZ::Transform& worldTransform,
        bool transformAvailable, bool identityPending, bool hasNonUniformScale)
    {
        if (!m_controlThread.Check() || !m_active) return;
        AzFramework::EntityContextId context{};
        AzFramework::EntityIdContextQueryBus::EventResult(
            context, m_registration.m_cutoutEntityId, &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
        const TerrainCompositionAddress address{ context, configuration.m_targetCompositionEntityId };
        if (address != m_address)
        {
            DisconnectTarget();
            m_registration.m_registrationId = AZ::Uuid::CreateRandom();
            m_registration.m_updateRevision = 0;
        }
        m_registration.m_contextId = context;
        m_registration.m_configuration = configuration;
        m_registration.m_worldTransform = worldTransform;
        m_registration.m_transformAvailable = transformAvailable;
        m_registration.m_hasNonUniformScale = hasNonUniformScale;
        m_registration.m_identityPending = identityPending;
        m_address = address;
        UpdateMeshAsset();

        if (!context.IsNull() && address.second.IsValid() && m_registration.m_cutoutEntityId.IsValid())
        {
            if (!TerrainCompositionNotificationBus::Handler::BusIsConnected())
            {
                TerrainCompositionNotificationBus::Handler::BusConnect(address);
            }
            OnCompositionAvailable({});
        }
    }

    void TerrainMeshCutoutRegistration::UpdateMeshAsset()
    {
        const auto assetId = m_registration.m_configuration.m_cutoutMeshAsset.GetId();
        if (assetId == m_selectedAssetId && ((m_meshSource && m_meshChanged.IsConnected()) || !assetId.IsValid()))
        {
            return;
        }
        const AZ::u64 generation = ++m_assetGeneration;
        m_meshChanged.Disconnect();
        m_meshSource.reset();
        m_selectedAssetId = assetId;
        m_registration.m_mesh = {};
        if (!assetId.IsValid()) return;
        auto* cache = TerrainMeshCutoutDataCacheInterface::Get();
        if (!cache)
        {
            m_registration.m_mesh.m_status = TerrainMeshCutoutDataStatus::Error;
            return;
        }
        m_meshSource = cache->Acquire(assetId);
        m_meshChanged = TerrainMeshCutoutDataCache::ChangedEvent::Handler(
            [this, generation](const TerrainMeshCutoutDataSnapshot& snapshot)
            {
                if (!m_controlThread.Check() || !m_active || generation != m_assetGeneration) return;
                m_registration.m_mesh = snapshot;
                if (TerrainCompositionNotificationBus::Handler::BusIsConnected()) OnCompositionAvailable({});
            });
        TerrainMeshCutoutDataCache::ConnectChangedHandler(m_meshSource, m_meshChanged);
        m_registration.m_mesh = TerrainMeshCutoutDataCache::GetSnapshot(m_meshSource);
    }

    void TerrainMeshCutoutRegistration::DisconnectTarget()
    {
        TerrainCompositionNotificationBus::Handler::BusDisconnect();
        if (m_address.second.IsValid() && !m_address.first.IsNull())
        {
            TerrainCompositionRequestBus::Event(
                m_address, &TerrainCompositionRequestBus::Events::UnregisterMeshCutout,
                m_registration.m_cutoutEntityId, m_registration.m_registrationId,
                m_registration.m_compositionSession);
        }
        m_registered = false;
        m_address = TerrainCompositionAddress{};
        m_registration.m_compositionSession = {};
    }

    void TerrainMeshCutoutRegistration::Deactivate()
    {
        if (!m_active || !m_controlThread.Check()) return;
        m_active = false;
        AZ::SystemTickBus::Handler::BusDisconnect();
        ++m_assetGeneration;
        m_meshChanged.Disconnect();
        m_meshSource.reset();
        m_selectedAssetId = {};
        DisconnectTarget();
        m_registration = {};
    }

    bool TerrainMeshCutoutRegistration::IsRegistered() const
    {
        return m_controlThread.Check() && m_registered;
    }

    AZStd::string TerrainMeshCutoutRegistration::GetStatusMessage() const
    {
        if (!m_controlThread.Check()) return "Unavailable off the control thread.";
        if (!m_active) return "Inactive: no terrain cutout contribution.";
        if (!m_registration.m_configuration.m_cutoutMeshAsset.GetId().IsValid()) return "Select a closed Cutout Mesh model asset.";
        if (!m_address.second.IsValid()) return "Select a Target Composition entity.";
        if (m_address.first.IsNull()) return "Waiting for entity context ownership.";
        if (!m_registered) return "Target Composition is unavailable in this entity context.";
        if (m_registration.m_identityPending) return "Waiting for prefab propagation/undo to resolve ordering identity.";
        const auto key = m_registration.m_configuration.GetRuntimeOrderKey();
        if (key.empty()) return "Ordering identity is unresolved or malformed.";
        size_t claims = 0;
        TerrainCompositionRequestBus::EventResult(
            claims, m_address, &TerrainCompositionRequestBus::Events::GetOrderingClaimCount, key);
        if (claims > 1) return "Duplicate ordering identity: all conflicting contributors are suppressed.";
        switch (m_registration.m_mesh.m_status)
        {
        case TerrainMeshCutoutDataStatus::Unassigned: return "Select a closed Cutout Mesh model asset.";
        case TerrainMeshCutoutDataStatus::Loading: return "Loading and preparing cutter geometry asynchronously.";
        case TerrainMeshCutoutDataStatus::Missing: return "Cutout model product is missing from the asset catalog.";
        case TerrainMeshCutoutDataStatus::Error: return "Cutout model load or reload failed; terrain remains present.";
        case TerrainMeshCutoutDataStatus::Unsupported: return "Selected asset is not a supported Atom Model product.";
        case TerrainMeshCutoutDataStatus::InvalidGeometry:
            return GetTerrainMeshCutoutValidationMessage(m_registration.m_mesh.m_validation);
        case TerrainMeshCutoutDataStatus::Ready: break;
        }
        PreparedTerrainMeshCutout prepared;
        const auto placement = PrepareTerrainMeshCutout(
            m_registration, m_registration.m_hasNonUniformScale, prepared);
        if (placement != TerrainMeshCutoutPlacementValidation::Valid)
        {
            return GetTerrainMeshCutoutPlacementValidationMessage(placement);
        }
        return AZStd::string::format("Ready: %zu cutter triangles, revision %llu; %s terrain.",
            prepared.m_data->m_triangles.size(), static_cast<unsigned long long>(prepared.m_data->m_revision),
            prepared.m_operation == TerrainExistenceOperation::RemoveTerrain ? "removes" : "restores");
    }

    void TerrainMeshCutoutRegistration::OnCompositionAvailable(const AZ::Uuid& expectedSession)
    {
        if (!m_controlThread.Check() || !m_active) return;
        AZ::Uuid session{};
        TerrainCompositionRequestBus::EventResult(
            session, m_address, &TerrainCompositionRequestBus::Events::GetCompositionSession);
        if (session.IsNull())
        {
            m_registered = false;
            return;
        }
        if (!expectedSession.IsNull() && expectedSession != session) return;
        m_registration.m_compositionSession = session;
        ++m_registration.m_updateRevision;
        TerrainCompositionRequestBus::EventResult(
            m_registered, m_address, &TerrainCompositionRequestBus::Events::RegisterMeshCutout, m_registration);
    }

    void TerrainMeshCutoutRegistration::OnCompositionUnavailable(const AZ::Uuid& session)
    {
        if (!m_controlThread.Check() || session != m_registration.m_compositionSession) return;
        m_registered = false;
        m_registration.m_compositionSession = {};
    }

    void TerrainMeshCutoutRegistration::OnSystemTick()
    {
        if (!m_controlThread.Check() || !m_active) return;
        AzFramework::EntityContextId context{};
        AzFramework::EntityIdContextQueryBus::EventResult(
            context, m_registration.m_cutoutEntityId, &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
        if (context != m_address.first || (m_selectedAssetId.IsValid() && !m_meshChanged.IsConnected()))
        {
            const auto configuration = m_registration.m_configuration;
            Update(configuration, m_registration.m_worldTransform, m_registration.m_transformAvailable,
                m_registration.m_identityPending, m_registration.m_hasNonUniformScale);
        }
    }
} // namespace TerrainCompositor
