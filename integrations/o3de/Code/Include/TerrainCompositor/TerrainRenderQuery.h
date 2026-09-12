#pragma once

#include <AzCore/Debug/Profiler.h>
#include <AzCore/Component/EntityId.h>
#include <Atom/RPI.Public/Base.h>
#include <AzCore/Math/Vector2.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Math/Uuid.h>
#include <AzCore/std/containers/array.h>
#include <AzCore/std/containers/span.h>
#include <AzCore/std/containers/vector.h>
#include <AzFramework/Terrain/TerrainDataRequestBus.h>
#include <chrono>
#include <cmath>
#include <limits>

namespace TerrainCompositor
{
    enum class TerrainRenderCoordinates { Unknown, WorldXYOrdinarySurfaceZ, WorldXY };
    enum class TerrainRenderGrid { ExplicitPositions, Regular };
    enum class TerrainRenderSource { Unknown, Live, RetainedAvailable, Unavailable };
    enum class TerrainRenderInputZ { Unknown, OrdinarySurface, Independent };
    enum class TerrainRenderDispatch { Height, Existence, Geometry };
    enum class TerrainSourceAcquisition : size_t
    {
        NotRequested, Acquired, UnsupportedProvider, MissingProvider, InvalidIdentity,
        Reentrant, Cyclic, Rejected, InvalidConfiguration, ExternalMask, Count
    };

    //! A channel's retained sampling guarantee. Samplers describe direct evaluation
    //! at supplied world XY, not reconstruction of the ordinary terrain grid.
    //! Undeclared channels continue to use the legacy query-level contract below.
    struct TerrainRenderChannelSampling
    {
        bool m_declared = false;
        bool m_explicitPositions = false, m_regularGrid = false;
        bool m_exact = false, m_clamp = false, m_bilinear = false;
        size_t m_minSamples = 0, m_maxSamples = std::numeric_limits<size_t>::max();
        float m_maxAbsXY = 0.0f;

        bool SupportsPositions(AZStd::span<const AZ::Vector3> positions) const
        {
            if (!m_declared || positions.size() < m_minSamples || positions.size() > m_maxSamples) return false;
            for (const auto& position : positions)
                if (!std::isfinite(position.GetX()) || !std::isfinite(position.GetY()) ||
                    std::abs(position.GetX()) > m_maxAbsXY || std::abs(position.GetY()) > m_maxAbsXY) return false;
            return true;
        }
    };
    enum class TerrainRenderFallback : size_t
    {
        UnownedHeight, UnownedExistence, SplitOwners, LegacyContract, UnsupportedRequest,
        UnknownSource, LiveSource, UnavailableSource, InputZ, OrdinaryDependency, PreservedPolicy,
        CoordinatesUnproven, SamplerEquivalenceUnproven, RenderValueUnproven, CollisionFallbackUnproven,
        ExternalMask, InvalidLayout, AreaDecisionMissing, AreaLifetimeUnproven,
        MissingInvalidation, StalePublication, StaleDependency, OrdinaryQueryLifetimeUnproven, Count
    };
    constexpr AZ::u32 TerrainRenderFallbackBit(TerrainRenderFallback reason)
    {
        return AZ::u32{1} << static_cast<size_t>(reason);
    }

    //! Separate from direct-XY sampler support. Opting in requires differential
    //! proof against ordinary-query-then-overlay for this channel, including holes,
    //! clamping and collision-only fallback. The composition adapter certifies
    //! CLAMP for its supported built-in source; source-level flags are not copied.
    struct TerrainRenderOrdinaryEquivalence
    {
        bool m_coordinates = false;
        bool m_exact = false, m_clamp = false, m_bilinear = false;
        bool m_renderValue = false;
        bool m_collisionFallback = false;
    };

    struct TerrainRenderChannelCapability
    {
        TerrainRenderSource m_source = TerrainRenderSource::Unknown;
        TerrainRenderInputZ m_inputZ = TerrainRenderInputZ::Unknown;
        bool m_requiresOrdinaryResult = true;
        AZ::EntityId m_sourceEntityId{}; //!< Informational identity; never a source-lifetime lease.
        TerrainRenderChannelSampling m_sampling;
        TerrainRenderOrdinaryEquivalence m_ordinaryEquivalence;
    };

    //! Guarantees for the ownership decision, never an availability probe or a
    //! restriction on the legacy overlay callbacks. Unsupported requests still use
    //! the ordinary-query-then-overlay contract. Retaining a composition does not
    //! retain the handlers on its external procedural buses.
    struct TerrainRenderQueryCapability
    {
        bool m_declared = false;
        TerrainRenderCoordinates m_coordinates = TerrainRenderCoordinates::Unknown;
        bool m_acceptsExplicitPositions = false;
        bool m_acceptsRegularGrid = false;
        bool m_exact = false;
        bool m_clamp = false;
        bool m_bilinear = false;
        size_t m_minSamples = 1;
        size_t m_maxSamples = std::numeric_limits<size_t>::max();
        TerrainRenderChannelCapability m_height;
        TerrainRenderChannelCapability m_existence;
    };

    //! Synchronous borrowed samples. The caller keeps these spans alive and does not
    //! change XY between resolution and execution. Z is the ordinary QueryRegion
    //! surface value (including collision fallback), never a recomposed height.
    struct TerrainRenderQueryRequest
    {
        AZStd::span<const AZ::Vector3> m_positions;
        TerrainRenderCoordinates m_coordinates = TerrainRenderCoordinates::WorldXYOrdinarySurfaceZ;
        TerrainRenderGrid m_grid = TerrainRenderGrid::ExplicitPositions;
        AZ::Vector2 m_gridStart = AZ::Vector2::CreateZero();
        AZ::Vector2 m_gridSpacing = AZ::Vector2::CreateZero();
        size_t m_gridWidth = 0;
        size_t m_gridHeight = 0;
        AzFramework::Terrain::TerrainDataRequests::Sampler m_sampler =
            AzFramework::Terrain::TerrainDataRequests::Sampler::EXACT;
        bool m_allowBatch = true;
    };

    //! Optional caller-owned, worker-local accumulation. Sample counts are points,
    //! callback/source counts are invocations. Fallback buckets may overlap.
    struct TerrainRenderQueryStatistics
    {
        size_t m_heightOwned = 0, m_existenceOwned = 0, m_bothOwned = 0;
        size_t m_independentSamples = 0, m_ordinarySamples = 0, m_retainedSamples = 0;
        size_t m_skippedOrdinarySamples = 0;
        size_t m_scalarSamples = 0, m_batchSamples = 0;
        size_t m_scalarCallbacks = 0, m_batchCallbacks = 0;
        size_t m_heightSourceCalls = 0, m_existenceSourceCalls = 0, m_hierarchySourceCalls = 0;
        size_t m_heightSourceFallbackSamples = 0, m_existenceSourceFallbackSamples = 0;
        size_t m_heightSnapshotSamples = 0, m_existenceSnapshotSamples = 0;
        AZStd::array<size_t, static_cast<size_t>(TerrainSourceAcquisition::Count)> m_sourceAcquisitions{};
        struct SourceProvenance
        {
            AZ::EntityId m_entityId{};
            AZ::Uuid m_session{};
            AZ::u64 m_generation = 0;
            TerrainSourceAcquisition m_height = TerrainSourceAcquisition::NotRequested;
            TerrainSourceAcquisition m_existence = TerrainSourceAcquisition::NotRequested;
        };
        AZStd::vector<SourceProvenance> m_sources;
        AZStd::array<size_t, static_cast<size_t>(TerrainRenderFallback::Count)> m_fallbackSamples{};
        // Complete-request assessment, distinct from executed/partially owned samples.
        size_t m_requiredSectorSamples = 0, m_requiredClodSamples = 0, m_sectorBothOwned = 0;
        bool m_sectorAvoidOrdinaryEligible = false, m_sectorAcrossFramesEligible = false;
        AZ::u32 m_sectorOrdinaryFallbacks = 0, m_sectorAcrossFramesFallbacks = 0;
        double m_resolutionMicroseconds = 0, m_executionMicroseconds = 0, m_preparationMicroseconds = 0;
    };

    class TerrainRenderQueryTimer
    {
    public:
        explicit TerrainRenderQueryTimer(double* output) : m_output(output)
        {
            if (m_output) m_start = Clock::now();
        }
        ~TerrainRenderQueryTimer()
        {
            if (m_output) *m_output += std::chrono::duration<double, std::micro>(Clock::now() - m_start).count();
        }
    private:
        using Clock = std::chrono::steady_clock;
        double* m_output;
        Clock::time_point m_start;
    };
}
