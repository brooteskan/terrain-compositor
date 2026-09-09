#pragma once

#include <AzCore/Component/TickBus.h>
#include <TerrainCompositor/HeightmapControlThread.h>
#include <TerrainCompositor/TerrainCompositionBus.h>
#include <TerrainCompositor/TerrainMeshHeightDataCache.h>

namespace TerrainCompositor
{
    class TerrainMeshHeightStampRegistration final
        : private TerrainCompositionNotificationBus::Handler
        , private AZ::SystemTickBus::Handler
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
            return m_controlThread.Check() && m_active;
        }
        bool IsRegistered() const;
        AZStd::string GetStatusMessage() const;
        //! Value-owned control-thread snapshot used by editor diagnostics. It
        //! contains immutable prepared mesh data and no component references.
        TerrainMeshHeightStampRegistrationData GetRegistrationData() const;

    private:
        void DisconnectTarget();
        void UpdateMeshAsset();
        void OnCompositionAvailable(const AZ::Uuid& expectedSession) override;
        void OnCompositionUnavailable(const AZ::Uuid& session) override;
        void OnSystemTick() override;

        TerrainMeshHeightStampRegistrationData m_registration;
        HeightmapControlThread m_controlThread;
        TerrainCompositionAddress m_address;
        TerrainMeshHeightDataCache::Handle m_meshSource;
        TerrainMeshHeightDataCache::ChangedEvent::Handler m_meshChanged;
        AZ::Data::AssetId m_selectedAssetId;
        AZ::u64 m_assetGeneration = 0;
        bool m_active = false;
        bool m_registered = false;
    };
} // namespace TerrainCompositor
