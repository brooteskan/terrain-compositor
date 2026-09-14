#pragma once

#include <TerrainCompositor/Internal/RegistrationClient.h>
#include <TerrainCompositor/TerrainMeshHeightDataCache.h>

namespace TerrainCompositor
{
    class TerrainMeshHeightStampRegistration final
        : private Internal::RegistrationClient<TerrainMeshHeightStampRegistrationData, &TerrainMeshHeightStampRegistrationData::m_stampEntityId,
              &TerrainCompositionRequests::RegisterMeshHeightStamp, &TerrainCompositionRequests::UnregisterMeshHeightStamp>
    {
    public:
        TerrainMeshHeightStampRegistration() = default;
        ~TerrainMeshHeightStampRegistration();
        TerrainMeshHeightStampRegistration(const TerrainMeshHeightStampRegistration&) = delete;
        TerrainMeshHeightStampRegistration& operator=(const TerrainMeshHeightStampRegistration&) = delete;

        void Activate(
            AZ::EntityId entityId,
            const TerrainMeshHeightStampConfig& configuration,
            const AZ::Transform& worldTransform,
            bool transformAvailable = true,
            bool identityPending = false,
            bool hasNonUniformScale = false);
        void Update(
            const TerrainMeshHeightStampConfig& configuration,
            const AZ::Transform& worldTransform,
            bool transformAvailable = true,
            bool identityPending = false,
            bool hasNonUniformScale = false);
        void Deactivate();
        bool IsActive() const
        {
            return IsClientActive();
        }
        bool IsRegistered() const;
        AZStd::string GetStatusMessage() const;
        //! Value-owned control-thread snapshot used by editor diagnostics. It
        //! contains immutable prepared mesh data and no component references.
        TerrainMeshHeightStampRegistrationData GetRegistrationData() const;

    private:
        void UpdateAssets() override;
        void ResetAssets() override { m_meshAsset.Reset(); }
        bool NeedsAssetRetry() const override { return m_meshAsset.NeedsRetry(); }

        Internal::AssetSubscription<TerrainMeshHeightDataCache> m_meshAsset;
    };
} // namespace TerrainCompositor
