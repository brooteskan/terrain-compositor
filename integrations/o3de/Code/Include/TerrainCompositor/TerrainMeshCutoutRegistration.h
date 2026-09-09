#pragma once

#include <AzCore/Component/TickBus.h>
#include <TerrainCompositor/HeightmapControlThread.h>
#include <TerrainCompositor/TerrainCompositionBus.h>
#include <TerrainCompositor/TerrainMeshCutoutDataCache.h>

namespace TerrainCompositor
{
    class TerrainMeshCutoutRegistration final
        : private TerrainCompositionNotificationBus::Handler
        , private AZ::SystemTickBus::Handler
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
        bool IsActive() const { return m_controlThread.Check() && m_active; }
        bool IsRegistered() const;
        AZStd::string GetStatusMessage() const;

    private:
        void DisconnectTarget();
        void UpdateMeshAsset();
        void OnCompositionAvailable(const AZ::Uuid& expectedSession) override;
        void OnCompositionUnavailable(const AZ::Uuid& session) override;
        void OnSystemTick() override;

        TerrainMeshCutoutRegistrationData m_registration;
        HeightmapControlThread m_controlThread;
        TerrainCompositionAddress m_address;
        TerrainMeshCutoutDataCache::Handle m_meshSource;
        TerrainMeshCutoutDataCache::ChangedEvent::Handler m_meshChanged;
        AZ::Data::AssetId m_selectedAssetId;
        AZ::u64 m_assetGeneration = 0;
        bool m_active = false;
        bool m_registered = false;
    };
} // namespace TerrainCompositor
