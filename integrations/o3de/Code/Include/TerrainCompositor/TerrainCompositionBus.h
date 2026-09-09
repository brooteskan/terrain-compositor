#pragma once

#include <AzCore/EBus/EBus.h>
#include <AzCore/EBus/EBusSharedDispatchTraits.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/hash.h>
#include <AzCore/std/string/string_view.h>
#include <AzFramework/Entity/EntityContextBus.h>
#include <AzFramework/SurfaceData/SurfaceData.h>
#include <TerrainCompositor/Components/HeightmapStampConfig.h>
#include <TerrainCompositor/Components/TerrainMeshCutoutConfig.h>
#include <TerrainCompositor/Components/TerrainMeshHeightStampConfig.h>
#include <TerrainCompositor/HeightmapData.h>
#include <TerrainCompositor/TerrainMeshCutoutData.h>
#include <TerrainCompositor/TerrainMeshHeightData.h>

namespace TerrainCompositor
{
    //! Both parts are required: an editor instance must never register into a game instance.
    using TerrainCompositionAddress = AZStd::pair<AzFramework::EntityContextId, AZ::EntityId>;

    //! Value-owned registration data; no component pointers or query callbacks outlive a stamp.
    //! Image snapshots own immutable CPU data. Sampling skips registrations whose data pointer is null.
    struct HeightmapStampRegistrationData
    {
        AZ::EntityId m_stampEntityId{};
        AzFramework::EntityContextId m_contextId{};
        AZ::Uuid m_compositionSession{}; //!< Ephemeral lease scope, never a blend-order key.
        AZ::Uuid m_registrationId{}; //!< New for each activation/retarget.
        AZ::u64 m_updateRevision = 0;
        HeightmapStampConfig m_configuration;
        AZ::Transform m_worldTransform = AZ::Transform::CreateIdentity();
        bool m_transformAvailable = true;
        bool m_identityPending = false; //!< Expected editor propagation/loading, not a malformed persistent key.
        HeightmapDataSnapshot m_heightmap;
        HeightmapDataSnapshot m_surfaceIdA;
        HeightmapDataSnapshot m_surfaceIdB;
        HeightmapDataSnapshot m_surfaceBlend;
        HeightmapDataSnapshot m_holeMask;
    };

    //! Value-owned mesh-cutout claim. Prepared geometry is immutable and shared by instances of one model asset.
    struct TerrainMeshCutoutRegistrationData
    {
        AZ::EntityId m_cutoutEntityId{};
        AzFramework::EntityContextId m_contextId{};
        AZ::Uuid m_compositionSession{};
        AZ::Uuid m_registrationId{};
        AZ::u64 m_updateRevision = 0;
        TerrainMeshCutoutConfig m_configuration;
        AZ::Transform m_worldTransform = AZ::Transform::CreateIdentity();
        bool m_transformAvailable = true;
        bool m_hasNonUniformScale = false;
        bool m_identityPending = false;
        TerrainMeshCutoutDataSnapshot m_mesh;
    };

    //! Value-owned regular-grid mesh height claim. The immutable prepared mesh is safe for terrain query workers.
    struct TerrainMeshHeightStampRegistrationData
    {
        AZ::EntityId m_stampEntityId{};
        AzFramework::EntityContextId m_contextId{};
        AZ::Uuid m_compositionSession{};
        AZ::Uuid m_registrationId{};
        AZ::u64 m_updateRevision = 0;
        TerrainMeshHeightStampConfig m_configuration;
        AZ::Transform m_worldTransform = AZ::Transform::CreateIdentity();
        bool m_transformAvailable = true;
        bool m_hasNonUniformScale = false;
        bool m_identityPending = false;
        TerrainMeshHeightDataSnapshot m_mesh;
    };

    //! Main-thread change metadata retained before replacement/removal. XY bounds have Z=0.
    //! Region context is captured with each side, including removal/retargeting; never resolve it at dispatch time.
    struct HeightmapStampFootprintChange
    {
        AZ::EntityId m_stampEntityId{};
        TerrainCompositionAddress m_address;
        AZ::Uuid m_compositionSession{};
        AZ::u64 m_snapshotRevision = 0;
        AZ::Aabb m_previousBounds = AZ::Aabb::CreateNull();
        AZ::Aabb m_currentBounds = AZ::Aabb::CreateNull();
        AZ::EntityId m_previousRegionEntityId{};
        AZ::EntityId m_currentRegionEntityId{};
        AZ::Aabb m_previousRegionBounds = AZ::Aabb::CreateNull();
        AZ::Aabb m_currentRegionBounds = AZ::Aabb::CreateNull();
    };

    //! Control-plane requests run on the main thread. Terrain queries use GradientRequestBus instead.
    class TerrainCompositionRequests : public AZ::EBusTraits
    {
    public:
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        using BusIdType = TerrainCompositionAddress;
        using MutexType = AZStd::recursive_mutex;

        virtual ~TerrainCompositionRequests() = default;
        virtual AZ::Uuid GetCompositionSession() const = 0;
        //! Store the current lease's claim, including invalid/noncontributing settings.
        //! False rejects stale/wrong-context requests. True does not imply a contributing footprint.
        virtual bool RegisterStamp(const HeightmapStampRegistrationData& registration) = 0;
        virtual void UnregisterStamp(AZ::EntityId stampEntityId, const AZ::Uuid& registrationId, const AZ::Uuid& compositionSession) = 0;
        //! Copies, never references into mutable component state. Enumeration order is not blend order.
        virtual AZStd::vector<HeightmapStampRegistrationData> GetRegisteredStamps() const = 0;
        virtual bool RegisterMeshCutout(const TerrainMeshCutoutRegistrationData& registration) = 0;
        virtual void UnregisterMeshCutout(
            AZ::EntityId cutoutEntityId, const AZ::Uuid& registrationId, const AZ::Uuid& compositionSession) = 0;
        virtual AZStd::vector<TerrainMeshCutoutRegistrationData> GetRegisteredMeshCutouts() const = 0;
        virtual bool RegisterMeshHeightStamp(const TerrainMeshHeightStampRegistrationData& registration) = 0;
        virtual void UnregisterMeshHeightStamp(
            AZ::EntityId stampEntityId, const AZ::Uuid& registrationId, const AZ::Uuid& compositionSession) = 0;
        virtual AZStd::vector<TerrainMeshHeightStampRegistrationData> GetRegisteredMeshHeightStamps() const = 0;
        virtual size_t GetOrderingClaimCount(AZStd::string_view stableOrderKey) const = 0;
        //! Cached shape bounds used by Terrain Height Gradient List's normalized-height mapping.
        virtual AZ::Aabb GetTargetRegionBounds() const = 0;
    };
    using TerrainCompositionRequestBus = AZ::EBus<TerrainCompositionRequests>;

    //! Query-only world-height path consumed by the terrain-region height provider.
    //! The compositor publishes height and existence atomically from one immutable snapshot.
    class TerrainCompositionHeightRequests : public AZ::EBusSharedDispatchTraits<TerrainCompositionHeightRequests>
    {
    public:
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        using BusIdType = TerrainCompositionAddress;

        virtual ~TerrainCompositionHeightRequests() = default;
        virtual float GetHeight(AZ::EntityId terrainRegionEntityId, const AZ::Vector3& position, bool& terrainExists) const = 0;
        virtual void GetHeights(
            AZ::EntityId terrainRegionEntityId,
            AZStd::span<const AZ::Vector3> positions,
            AZStd::span<float> outHeights,
            AZStd::span<bool> terrainExists) const = 0;
    };
    using TerrainCompositionHeightRequestBus = AZ::EBus<TerrainCompositionHeightRequests>;

    //! Query-only surface path. The terrain-region provider forwards here instead of querying final terrain.
    //! Shared dispatch lets surface workers run concurrently while connection changes drain in-flight calls.
    class TerrainCompositionSurfaceRequests : public AZ::EBusSharedDispatchTraits<TerrainCompositionSurfaceRequests>
    {
    public:
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        using BusIdType = TerrainCompositionAddress;

        virtual ~TerrainCompositionSurfaceRequests() = default;
        virtual void GetSurfaceWeights(
            AZ::EntityId terrainRegionEntityId,
            const AZ::Vector3& position,
            AzFramework::SurfaceData::SurfaceTagWeightList& outSurfaceWeights) const = 0;
        virtual void GetSurfaceWeightsFromList(
            AZ::EntityId terrainRegionEntityId,
            AZStd::span<const AZ::Vector3> positions,
            AZStd::span<AzFramework::SurfaceData::SurfaceTagWeightList> outSurfaceWeights) const = 0;
    };
    using TerrainCompositionSurfaceRequestBus = AZ::EBus<TerrainCompositionSurfaceRequests>;

    class TerrainCompositionNotifications : public AZ::EBusTraits
    {
    public:
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Multiple;
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        using BusIdType = TerrainCompositionAddress;
        using MutexType = AZStd::recursive_mutex;

        virtual ~TerrainCompositionNotifications() = default;
        //! Sent after the request handler is available. Waiting stamps replay their current registration.
        virtual void OnCompositionAvailable(const AZ::Uuid& /*session*/)
        {
        }
        //! Sent after disconnecting requests and dropping registrations. Stamps keep waiting at this address.
        virtual void OnCompositionUnavailable(const AZ::Uuid& /*session*/)
        {
        }
        //! Sent after query publication, outside query locks; previous bounds survive invalidation/removal.
        virtual void OnStampFootprintChanged(const HeightmapStampFootprintChange& /*change*/)
        {
        }
    };
    using TerrainCompositionNotificationBus = AZ::EBus<TerrainCompositionNotifications>;
} // namespace TerrainCompositor
