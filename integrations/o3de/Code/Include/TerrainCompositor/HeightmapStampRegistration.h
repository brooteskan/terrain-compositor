#pragma once

#include <TerrainCompositor/TerrainCompositionBus.h>
#include <TerrainCompositor/HeightmapDataCache.h>
#include <TerrainCompositor/HeightmapStampSampling.h>
#include <TerrainCompositor/HeightmapControlThread.h>
#include <AzCore/Component/TickBus.h>

namespace TerrainCompositor
{
    //! Lifecycle helper for the stamp component. Own one per stamp; call only on the main thread.
    //! Supports either activation order, retargeting, and composition removal/recreation without a global stamp list.
    class HeightmapStampRegistration final
        : private TerrainCompositionNotificationBus::Handler
        , private AZ::SystemTickBus::Handler
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
        void DisconnectTarget();
        void UpdateHeightmapAsset();
        void UpdateSurfaceAssets();
        void UpdateHoleAsset();
        void UpdateSurfaceAsset(const AZ::Data::AssetId& assetId, HeightmapDataCache::Handle& source,
            HeightmapDataCache::ChangedEvent::Handler& changed, AZ::Data::AssetId& selectedAssetId,
            AZ::u64* generation, HeightmapDataSnapshot HeightmapStampRegistrationData::* snapshotMember);
        void ValidateCurrentStamp();
        void OnCompositionAvailable(const AZ::Uuid& expectedSession) override;
        void OnCompositionUnavailable(const AZ::Uuid& session) override;
        void OnSystemTick() override;

        HeightmapStampRegistrationData m_registration;
        HeightmapControlThread m_controlThread;
        TerrainCompositionAddress m_address;
        HeightmapDataCache::Handle m_heightmapSource;
        HeightmapDataCache::ChangedEvent::Handler m_heightmapChanged;
        AZ::Data::AssetId m_selectedAssetId;
        AZ::u64 m_assetGeneration = 0;
        HeightmapDataCache::Handle m_surfaceIdASource;
        HeightmapDataCache::Handle m_surfaceIdBSource;
        HeightmapDataCache::Handle m_surfaceBlendSource;
        HeightmapDataCache::Handle m_holeMaskSource;
        HeightmapDataCache::ChangedEvent::Handler m_surfaceIdAChanged;
        HeightmapDataCache::ChangedEvent::Handler m_surfaceIdBChanged;
        HeightmapDataCache::ChangedEvent::Handler m_surfaceBlendChanged;
        HeightmapDataCache::ChangedEvent::Handler m_holeMaskChanged;
        AZ::Data::AssetId m_selectedSurfaceIdA;
        AZ::Data::AssetId m_selectedSurfaceIdB;
        AZ::Data::AssetId m_selectedSurfaceBlend;
        AZ::Data::AssetId m_selectedHoleMask;
        AZ::u64 m_surfaceIdAGeneration = 0;
        AZ::u64 m_surfaceIdBGeneration = 0;
        AZ::u64 m_surfaceBlendGeneration = 0;
        AZ::u64 m_holeMaskGeneration = 0;
        HeightmapStampValidation m_validation = HeightmapStampValidation::Valid;
        bool m_active = false;
        bool m_registered = false;
    };
} // namespace TerrainCompositor
