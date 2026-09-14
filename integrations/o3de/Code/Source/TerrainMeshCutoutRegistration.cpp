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
        ActivateClient(entityId, configuration, worldTransform, transformAvailable, identityPending, hasNonUniformScale);
    }

    void TerrainMeshCutoutRegistration::Update(
        const TerrainMeshCutoutConfig& configuration, const AZ::Transform& worldTransform,
        bool transformAvailable, bool identityPending, bool hasNonUniformScale)
    {
        UpdateClient(configuration, worldTransform, transformAvailable, identityPending, hasNonUniformScale);
    }

    void TerrainMeshCutoutRegistration::UpdateAssets()
    {
        RefreshAsset(m_meshAsset, m_registration.m_configuration.m_cutoutMeshAsset.GetId(), m_registration.m_mesh);
    }

    void TerrainMeshCutoutRegistration::Deactivate()
    {
        DeactivateClient();
    }

    bool TerrainMeshCutoutRegistration::IsRegistered() const
    {
        return IsClientRegistered();
    }

    AZStd::string TerrainMeshCutoutRegistration::GetStatusMessage() const
    {
        if (!m_controlThread.Check()) return "Unavailable off the control thread.";
        if (!m_active) return "Inactive: no terrain cutout contribution.";
        if (!m_registration.m_configuration.m_cutoutMeshAsset.GetId().IsValid()) return "Select a closed Cutout Mesh model asset.";
        if (const char* status = GetOrderingStatus(
            "Select a Target Composition entity.",
            "Target Composition is unavailable in this entity context.",
            "Waiting for prefab propagation/undo to resolve ordering identity.",
            "Ordering identity is unresolved or malformed.",
            "Duplicate ordering identity: all conflicting contributors are suppressed."))
            return status;
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

} // namespace TerrainCompositor
