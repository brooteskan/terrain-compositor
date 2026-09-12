#pragma once

#include <AzTest/AzTest.h>
#include <AzFramework/Scene/SceneSystemComponent.h>
#include <Atom/RPI.Reflect/Image/StreamingImageAsset.h>
#include <LmbrCentral/Shape/MockShapes.h>
#include <TerrainCompositor/Components/ProceduralGroundGradientComponent.h>
#include <TerrainCompositor/Components/TerrainCompositionGradientComponent.h>
#include "TerrainTestFixtures.h"

namespace TerrainCompositor::SnapshotTestSupport
{
    inline TerrainProceduralSnapshotPtr Acquire(AZ::EntityId id)
    {
        TerrainProceduralSnapshotPtr result;
        TerrainProceduralSnapshotRequestBus::EventResult(result, id, &TerrainProceduralSnapshotRequests::AcquireTerrainSnapshot);
        return result;
    }
    inline bool Current(const TerrainProceduralSnapshotPtr& snapshot)
    {
        return TerrainPreparationAdmission({ snapshot->m_ticket }).IsValid();
    }
    class Context : public AzFramework::EntityIdContextQueryBus::MultiHandler
    {
    public:
        explicit Context(AZ::EntityId id) { BusConnect(id); }
        ~Context() override { BusDisconnect(); }
        AzFramework::EntityContextId GetOwningContextId() override { return m_id; }
        AzFramework::EntityContextId m_id = AZ::Uuid::CreateRandom();
    };

    struct Composition
    {
        const AZ::EntityId m_owner{ 901001 }, m_sourceId{ 901002 }, m_region{ 901003 };
        TestSupport::ScopedNameDictionary m_names;
        AzFramework::SceneSystemComponent m_sceneSystem;
        TerrainMeshCutoutRenderRegistry m_registry;
        Context m_context{ m_owner };
        ::testing::NiceMock<UnitTest::MockShapeComponentRequests> m_shape{ m_region };
        ProceduralGroundGradientConfig m_config;
        std::unique_ptr<ProceduralGroundGradientComponent> m_source;
        std::unique_ptr<TerrainCompositionGradientComponent> m_composition;

        explicit Composition(ProceduralGroundGradientConfig config = {}, bool builtIn = true) : m_config(config)
        {
            ON_CALL(m_shape, GetEncompassingAabb()).WillByDefault(::testing::Return(
                AZ::Aabb::CreateFromMinMaxValues(-10000, -10000, -1024, 10000, 10000, 1024)));
            if (builtIn) StartSource();
            TerrainCompositionConfig composition;
            composition.m_proceduralSourceEntityId = m_sourceId;
            composition.m_targetTerrainRegionEntityId = m_region;
            m_composition = std::make_unique<TerrainCompositionGradientComponent>(composition);
            m_composition->EditorActivate(m_owner);
        }
        ~Composition()
        {
            m_composition->EditorDeactivate(m_owner);
            m_composition.reset();
            StopSource();
            // Match the application's event-loop teardown before unloading the
            // test DLL: deferred notifications own module-local function tables.
            AZ::TickBus::ExecuteQueuedEvents();
            AZ::SystemTickBus::ExecuteQueuedEvents();
        }
        void StartSource()
        {
            m_source = std::make_unique<ProceduralGroundGradientComponent>(m_config);
            m_source->EditorActivate(m_sourceId);
        }
        void StopSource()
        {
            if (m_source) m_source->EditorDeactivate(m_sourceId);
            m_source.reset();
        }
        TerrainMeshCutoutRenderChannelPtr Channel() { return m_registry.AcquireSceneChannel(nullptr); }
        TerrainMeshCutoutRenderSnapshotPtr Publication() { return Channel()->m_snapshot.load(); }
        TerrainRenderQuerySourcesPtr Capture() { return CaptureTerrainRenderQuerySources(Publication()); }
        bool AddImageHole(float footprint = 0.5f, bool withHeight = false)
        {
            HeightmapStampRegistrationData record;
            record.m_stampEntityId = AZ::EntityId(901004);
            m_context.BusConnect(record.m_stampEntityId);
            record.m_contextId = m_context.m_id;
            const TerrainCompositionAddress address{ m_context.m_id, m_owner };
            TerrainCompositionRequestBus::EventResult(record.m_compositionSession, address, &TerrainCompositionRequests::GetCompositionSession);
            record.m_registrationId = AZ::Uuid::CreateRandom();
            record.m_updateRevision = 1;
            record.m_configuration.m_targetCompositionEntityId = m_owner;
            record.m_configuration.AssignNewPersistentOrderingIdentity();
            record.m_configuration.m_footprintWidth = record.m_configuration.m_footprintDepth = footprint;
            record.m_configuration.m_holeMask.m_maskAsset = AZ::Data::Asset<AZ::RPI::StreamingImageAsset>(
                AZ::Data::AssetId(AZ::Uuid::CreateRandom()), azrtti_typeid<AZ::RPI::StreamingImageAsset>(), {});
            record.m_holeMask.m_status = HeightmapDataStatus::Ready;
            record.m_holeMask.m_data = TestSupport::MakeMask(1.0f);
            if (withHeight)
            {
                record.m_worldTransform = AZ::Transform::CreateRotationZ(0.37f);
                record.m_worldTransform.SetTranslation(AZ::Vector3(0, 0, -7));
                record.m_configuration.m_featherWidth = 0;
                record.m_configuration.m_heightScale = 40;
                record.m_configuration.m_heightmapAsset = record.m_configuration.m_holeMask.m_maskAsset;
                record.m_heightmap.m_status = HeightmapDataStatus::Ready;
                record.m_heightmap.m_data = AZStd::make_shared<HeightmapData>(TestSupport::MakeImage(2, 2, {0, 1, 0.25f, 0.5f}));
                record.m_holeMask.m_data = AZStd::make_shared<HeightmapData>(TestSupport::MakeImage(2, 2, {0, 1, 1, 0}));
            }
            bool accepted = false;
            TerrainCompositionRequestBus::EventResult(accepted, address, &TerrainCompositionRequests::RegisterStamp, record);
            return accepted;
        }
        bool AddMeshHeight()
        {
            TerrainMeshHeightStampRegistrationData record;
            record.m_stampEntityId = AZ::EntityId(901005);
            m_context.BusConnect(record.m_stampEntityId);
            record.m_contextId = m_context.m_id;
            const TerrainCompositionAddress address{ m_context.m_id, m_owner };
            TerrainCompositionRequestBus::EventResult(record.m_compositionSession, address, &TerrainCompositionRequests::GetCompositionSession);
            record.m_registrationId = AZ::Uuid::CreateRandom();
            record.m_updateRevision = 1;
            record.m_configuration.m_targetCompositionEntityId = m_owner;
            record.m_configuration.AssignNewPersistentOrderingIdentity();
            record.m_configuration.m_relativeEdgeBlend = false;
            record.m_configuration.m_priority = 2;
            record.m_worldTransform = AZ::Transform::CreateRotationZ(-0.23f);
            record.m_worldTransform.SetTranslation(AZ::Vector3(2, 1, 3));
            auto data = AZStd::make_shared<TerrainMeshHeightData>();
            const AZStd::array positions{ AZ::Vector3(-4,-4,-2), AZ::Vector3(4,-4,3), AZ::Vector3(-4,4,6), AZ::Vector3(4,4,-1) };
            const AZStd::array<AZ::u32, 6> indices{0, 1, 3, 0, 3, 2};
            if (BuildTerrainMeshHeightData(positions, indices, *data) != TerrainMeshHeightValidation::Valid) return false;
            record.m_mesh.m_status = TerrainMeshHeightDataStatus::Ready;
            record.m_mesh.m_data = data;
            bool accepted = false;
            TerrainCompositionRequestBus::EventResult(accepted, address, &TerrainCompositionRequests::RegisterMeshHeightStamp, record);
            return accepted;
        }
    };
}
