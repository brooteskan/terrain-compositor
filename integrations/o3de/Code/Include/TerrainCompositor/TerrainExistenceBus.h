#pragma once

#include <AzCore/EBus/EBus.h>
#include <AzCore/EBus/EBusSharedDispatchTraits.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/span.h>

namespace TerrainCompositor
{
    //! Optional existence source implemented by procedural height sources.
    //! A height source without this bus is treated as existing everywhere.
    class TerrainExistenceSourceRequests
        : public AZ::EBusSharedDispatchTraits<TerrainExistenceSourceRequests>
    {
    public:
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        using BusIdType = AZ::EntityId;

        virtual ~TerrainExistenceSourceRequests() = default;
        virtual bool GetTerrainExists(const AZ::Vector3& position) const = 0;
        virtual void GetTerrainExistsFromList(
            AZStd::span<const AZ::Vector3> positions, AZStd::span<bool> terrainExists) const = 0;
    };
    using TerrainExistenceSourceRequestBus = AZ::EBus<TerrainExistenceSourceRequests>;
} // namespace TerrainCompositor
