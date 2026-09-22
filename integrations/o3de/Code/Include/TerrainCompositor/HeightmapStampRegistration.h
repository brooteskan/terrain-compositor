#pragma once

#include <TerrainCompositor/Internal/RegistrationClient.h>
#include <TerrainCompositor/HeightmapDataCache.h>
#include <TerrainCompositor/HeightmapStampSampling.h>

namespace TerrainCompositor
{
    //! Lifecycle helper for the stamp component. Own one per stamp; call only on the main thread.
    //! Supports either activation order, retargeting, and composition removal/recreation without a global stamp list.
    class HeightmapStampRegistration final
        : private Internal::RegistrationClient<HeightmapStampRegistrationData, &HeightmapStampRegistrationData::m_stampEntityId,
              &TerrainCompositionRequests::RegisterStamp, &TerrainCompositionRequests::UnregisterStamp>
    {
    public:
        HeightmapStampRegistration() = default;
        ~HeightmapStampRegistration();
        HeightmapStampRegistration(const HeightmapStampRegistration&) = delete;
        HeightmapStampRegistration& operator=(const HeightmapStampRegistration&) = delete;

        void Activate(AZ::EntityId stampEntityId, const HeightmapStampConfig& configuration,
            const AZ::Transform& worldTransform, bool transformAvailable = true, bool identityPending = false);
        void Update(const HeightmapStampConfig& configuration, const AZ::Transform& worldTransform,
            bool transformAvailable = true, bool identityPending = false);
        void Deactivate();
        bool IsRegistered() const;
        //! Read-only, control-thread diagnostic. Never call from a height query.
        AZStd::string GetStatusMessage() const;

    private:
        void UpdateAssets() override;
        void ResetAssets() override;
        void ValidateUnavailable() override;
        void BeforeRegister() override { m_registered = false; }

        Internal::AssetSubscription<HeightmapDataCache> m_images[5];
        HeightmapStampValidation m_validation = HeightmapStampValidation::Valid;
    };
} // namespace TerrainCompositor
