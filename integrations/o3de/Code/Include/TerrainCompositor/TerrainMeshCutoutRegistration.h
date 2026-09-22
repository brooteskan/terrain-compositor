#pragma once

#include <TerrainCompositor/Internal/RegistrationClient.h>
#include <TerrainCompositor/TerrainMeshCutoutDataCache.h>

namespace TerrainCompositor
{
    class TerrainMeshCutoutRegistration final
        : private Internal::RegistrationClient<TerrainMeshCutoutRegistrationData, &TerrainMeshCutoutRegistrationData::m_cutoutEntityId,
              &TerrainCompositionRequests::RegisterMeshCutout, &TerrainCompositionRequests::UnregisterMeshCutout>
    {
    public:
        TerrainMeshCutoutRegistration() = default;
        ~TerrainMeshCutoutRegistration();
        TerrainMeshCutoutRegistration(const TerrainMeshCutoutRegistration&) = delete;
        TerrainMeshCutoutRegistration& operator=(const TerrainMeshCutoutRegistration&) = delete;

        void Activate(AZ::EntityId entityId, const TerrainMeshCutoutConfig& configuration,
            const AZ::Transform& worldTransform, bool transformAvailable = true, bool identityPending = false,
            bool hasNonUniformScale = false);
        void Update(const TerrainMeshCutoutConfig& configuration, const AZ::Transform& worldTransform,
            bool transformAvailable = true, bool identityPending = false, bool hasNonUniformScale = false);
        void Deactivate();
        bool IsActive() const { return IsClientActive(); }
        bool IsRegistered() const;
        AZStd::string GetStatusMessage() const;

    private:
        void UpdateAssets() override;
        void ResetAssets() override { m_meshAsset.Reset(); }

        Internal::AssetSubscription<TerrainMeshCutoutDataCache> m_meshAsset;
    };
} // namespace TerrainCompositor
