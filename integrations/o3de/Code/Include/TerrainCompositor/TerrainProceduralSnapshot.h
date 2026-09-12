#pragma once

#include <AzCore/EBus/EBus.h>
#include <AzCore/EBus/EBusSharedDispatchTraits.h>
#include <AzCore/std/function/function_template.h>
#include <TerrainCompositor/TerrainPreparationDependency.h>
#include <TerrainCompositor/TerrainRenderQuery.h>

namespace TerrainCompositor
{
    //! Opt-in, immutable values, not a retained provider. All callbacks must own
    //! their complete dependency chain and remain safe after provider destruction.
    //! Height is normalized/clamped; the composition maps it into world heights.
    //! Channels are independent: an unsupported existence channel must never be
    //! inferred from height support. Unsupported sampling leaves outputs untouched.
    struct TerrainProceduralSnapshot
    {
        // Identifies the value kernel covered by the composition's differential
        // render proof. Custom snapshots retain the ordinary-query fallback.
        enum class Kernel { Unspecified, ProceduralGround };
        Kernel m_kernel = Kernel::Unspecified;
        bool m_zeroProfilePruning = false; //!< Captured equivalent kernel policy; never read a cvar on workers.
        AZ::EntityId m_entityId{};
        AZ::Uuid m_session{};
        TerrainPreparationDependencyTicket m_ticket;
        TerrainRenderChannelCapability m_height, m_existence;
        TerrainSourceAcquisition m_heightResult = TerrainSourceAcquisition::Rejected;
        TerrainSourceAcquisition m_existenceResult = TerrainSourceAcquisition::Rejected;
        AZStd::function<float(const AZ::Vector3&)> m_heightValue;
        AZStd::function<bool(const AZ::Vector3&)> m_existenceValue;

        bool HasValidContract() const
        {
            const auto valid = [this](const TerrainRenderChannelCapability& channel, TerrainSourceAcquisition result, bool callback)
            {
                return result != TerrainSourceAcquisition::Acquired || (callback && channel.m_sampling.m_declared &&
                    channel.m_source == TerrainRenderSource::RetainedAvailable && channel.m_sourceEntityId == m_entityId);
            };
            return m_entityId.IsValid() && !m_session.IsNull() && m_ticket.m_dependency &&
                valid(m_height, m_heightResult, bool(m_heightValue)) && valid(m_existence, m_existenceResult, bool(m_existenceValue));
        }

        bool SampleHeights(AZStd::span<const AZ::Vector3> positions, AZStd::span<float> values) const
        {
            if (m_heightResult != TerrainSourceAcquisition::Acquired || positions.size() != values.size() ||
                !m_heightValue || !m_height.m_sampling.SupportsPositions(positions)) return false;
            for (size_t i = 0; i < positions.size(); ++i) values[i] = m_heightValue(positions[i]);
            return true;
        }
        bool SampleHeight(const AZ::Vector3& position, float& value) const
        {
            return SampleHeights({ &position, 1 }, { &value, 1 });
        }
        bool SampleExistence(AZStd::span<const AZ::Vector3> positions, AZStd::span<bool> values) const
        {
            if (m_existenceResult != TerrainSourceAcquisition::Acquired || positions.size() != values.size() ||
                !m_existenceValue || !m_existence.m_sampling.SupportsPositions(positions)) return false;
            for (size_t i = 0; i < positions.size(); ++i) values[i] = m_existenceValue(positions[i]);
            return true;
        }
        bool SampleExists(const AZ::Vector3& position, bool& value) const
        {
            return SampleExistence({ &position, 1 }, { &value, 1 });
        }
    };
    using TerrainProceduralSnapshotPtr = std::shared_ptr<const TerrainProceduralSnapshot>;

    //! Acquisition is call-scoped shared dispatch. Return null on failure. Capture
    //! values and ticket atomically with respect to configuration publication.
    //! Invalidate before publishing changes; retire before removal. Each activation
    //! has a fresh session/authority, even if the entity ID is reused. Do not call
    //! live dependencies from retained callbacks or recapture an old ticket later.
    class TerrainProceduralSnapshotRequests : public AZ::EBusSharedDispatchTraits<TerrainProceduralSnapshotRequests>
    {
    public:
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        using BusIdType = AZ::EntityId;
        virtual ~TerrainProceduralSnapshotRequests() = default;
        virtual TerrainProceduralSnapshotPtr AcquireTerrainSnapshot() const = 0;
    };
    using TerrainProceduralSnapshotRequestBus = AZ::EBus<TerrainProceduralSnapshotRequests>;
}
