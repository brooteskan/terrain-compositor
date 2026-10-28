#include <TerrainCompositor/Components/TerrainCompositionHeightProviderComponent.h>

#include <AzCore/Math/MathUtils.h>
#include "../ProviderReflection.h"
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/containers/array.h>

namespace TerrainCompositor
{
    AZ_COMPONENT_IMPL(TerrainCompositionHeightProviderComponent, "TerrainCompositionHeightProviderComponent",
        TerrainCompositionHeightProviderComponentTypeId, AzFramework::EditorEntityEvents);

    void TerrainCompositionHeightProviderConfig::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection() && serialize->FindClassData(azrtti_typeid<TerrainCompositionHeightProviderConfig>())) return;
            serialize->Class<TerrainCompositionHeightProviderConfig, AZ::ComponentConfig>()->Version(2)
                ->Field("CompositionEntityId", &TerrainCompositionHeightProviderConfig::m_compositionEntityId)
                ->Field("EnableCollisions", &TerrainCompositionHeightProviderConfig::m_enableCollisions)
                ->Field("CollisionRange", &TerrainCompositionHeightProviderConfig::m_collisionRange)
                ->Field("CollisionGridSpacing", &TerrainCompositionHeightProviderConfig::m_collisionGridSpacing);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<TerrainCompositionHeightProviderConfig>("Terrain Composition Height Provider Configuration", "Composition and physics for this terrain region.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainCompositionHeightProviderConfig::m_compositionEntityId,
                        "Terrain Composition", "Composition whose final height and holes provide this region.")
                    ->Attribute(AZ::Edit::Attributes::RequiredService, AZ_CRC_CE("TerrainCompositionService"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainCompositionHeightProviderConfig::m_enableCollisions,
                        "Enable Collisions", "Automatically create PhysX collision for all composed terrain surfaces, respecting holes.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainCompositionHeightProviderConfig::m_collisionRange,
                        "Collision Range", "Coverage radius around each character, rigid body, and active viewport camera, in metres.")
                    ->Attribute(AZ::Edit::Attributes::Min, 16.0f)->Attribute(AZ::Edit::Attributes::Max, 512.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainCompositionHeightProviderConfig::m_collisionGridSpacing,
                        "Collision Grid Spacing", "Physics sample spacing in metres. Smaller values preserve finer features.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.25f)->Attribute(AZ::Edit::Attributes::Max, 4.0f);
            }
        }
    }

    void TerrainCompositionHeightProviderComponent::Reflect(AZ::ReflectContext* context)
    {
        Internal::ReflectConfiguredComponent(context, &TerrainCompositionHeightProviderComponent::m_configuration);
    }

    void TerrainCompositionHeightProviderComponent::GetProvidedServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        services.push_back(AZ_CRC_CE("TerrainHeightProviderService"));
    }

    void TerrainCompositionHeightProviderComponent::GetIncompatibleServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        services.push_back(AZ_CRC_CE("TerrainHeightProviderService"));
        services.push_back(AZ_CRC_CE("GradientService"));
    }

    void TerrainCompositionHeightProviderComponent::GetRequiredServices(
        AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        services.push_back(AZ_CRC_CE("TerrainAreaService"));
        services.push_back(AZ_CRC_CE("AxisAlignedBoxShapeService"));
    }

    void TerrainCompositionHeightProviderComponent::GetDependentServices(
        [[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& services)
    {
    }

    TerrainCompositionHeightProviderComponent::TerrainCompositionHeightProviderComponent(
        const TerrainCompositionHeightProviderConfig& configuration)
        : m_configuration(configuration)
    {
    }

    void TerrainCompositionHeightProviderComponent::Activate()
    {
        if (!m_binding.CheckControlThread()) return;
        m_runtimeActive = true;
        m_binding.Start(GetEntityId());
    }
    void TerrainCompositionHeightProviderComponent::Deactivate()
    {
        if (!m_binding.CheckControlThread()) return;
        m_binding.Stop();
        m_runtimeActive = false;
    }
    void TerrainCompositionHeightProviderComponent::EditorActivate(AZ::EntityId entityId) { m_binding.Start(entityId); }
    void TerrainCompositionHeightProviderComponent::EditorDeactivate([[maybe_unused]] AZ::EntityId entityId) { m_binding.Stop(); }

    void TerrainCompositionHeightProviderComponent::ConnectProvider()
    {
        RefreshHeightBounds();
        if (m_configuration.m_compositionEntityId.IsValid())
        {
            LmbrCentral::DependencyNotificationBus::Handler::BusConnect(m_configuration.m_compositionEntityId);
        }
        AzFramework::Terrain::TerrainDataNotificationBus::Handler::BusConnect();
        Terrain::TerrainAreaHeightRequestBus::Handler::BusConnect(m_binding.GetTerrainRegionEntityId());
        if (m_runtimeActive && m_configuration.m_enableCollisions)
            m_collision.Start(m_binding.GetTerrainRegionEntityId(), m_configuration.m_collisionRange, m_configuration.m_collisionGridSpacing);
    }

    void TerrainCompositionHeightProviderComponent::DisconnectProvider()
    {
        m_collision.Stop();
        Terrain::TerrainAreaHeightRequestBus::Handler::BusDisconnect();
        LmbrCentral::DependencyNotificationBus::Handler::BusDisconnect();
        AzFramework::Terrain::TerrainDataNotificationBus::Handler::BusDisconnect();
    }

    void TerrainCompositionHeightProviderComponent::ClearProvider()
    {
        AZStd::unique_lock lock(m_heightBoundsMutex);
        m_heightBounds = AzFramework::Terrain::FloatRange::CreateNull();
    }

    void TerrainCompositionHeightProviderComponent::RefreshHeightBounds()
    {
        auto bounds = AzFramework::Terrain::FloatRange::CreateNull();
        AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
            bounds, &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightBounds);
        AZStd::unique_lock lock(m_heightBoundsMutex);
        m_heightBounds = bounds;
    }

    bool TerrainCompositionHeightProviderComponent::ReadInConfig(const AZ::ComponentConfig* baseConfig)
    {
        if (!m_binding.CheckControlThread()) return false;
        if (m_runtimeActive && azrtti_cast<const TerrainCompositionHeightProviderConfig*>(baseConfig))
            m_collision.Stop();
        const bool accepted = m_binding.ReadConfiguration(baseConfig, m_configuration);
        if (accepted && m_runtimeActive)
        {
            m_collision.Stop();
            if (m_configuration.m_enableCollisions)
                m_collision.Start(GetEntityId(), m_configuration.m_collisionRange, m_configuration.m_collisionGridSpacing);
        }
        return accepted;
    }

    bool TerrainCompositionHeightProviderComponent::WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const
    {
        return m_binding.WriteConfiguration(outBaseConfig, m_configuration);
    }

    AZStd::string TerrainCompositionHeightProviderComponent::GetStatusMessage() const
    {
        return m_binding.GetStatusMessage<TerrainCompositionHeightRequestBus>(
            "Inactive: no terrain height provider.",
            "Ready: composition height and terrain existence are provided to this terrain region.");
    }

    void TerrainCompositionHeightProviderComponent::OnCompositionChanged()
    {
        m_binding.RefreshArea();
    }

    void TerrainCompositionHeightProviderComponent::OnCompositionRegionChanged(const AZ::Aabb& dirtyRegion)
    {
        if (dirtyRegion.IsValid())
        {
            Terrain::TerrainSystemServiceRequestBus::Broadcast(
                &Terrain::TerrainSystemServiceRequests::RefreshRegion, dirtyRegion,
                AzFramework::Terrain::TerrainDataNotifications::TerrainDataChangedMask::HeightData);
        }
        else
        {
            m_binding.RefreshArea();
        }
    }

    void TerrainCompositionHeightProviderComponent::OnTerrainDataChanged(
        [[maybe_unused]] const AZ::Aabb& dirtyRegion, TerrainDataChangedMask dataChangedMask)
    {
        if ((dataChangedMask & TerrainDataChangedMask::Settings) == TerrainDataChangedMask::Settings)
        {
            RefreshHeightBounds();
            m_binding.RefreshArea();
        }
    }

    void TerrainCompositionHeightProviderComponent::GetHeight(
        const AZ::Vector3& inPosition, AZ::Vector3& outPosition, bool& terrainExists)
    {
        float height = 0.0f;
        terrainExists = false;
        TerrainCompositionHeightRequestBus::EventResult(
            height, m_binding.GetCompositionAddress(), &TerrainCompositionHeightRequestBus::Events::GetHeight,
            m_binding.GetTerrainRegionEntityId(), inPosition, terrainExists);
        AZStd::shared_lock lock(m_heightBoundsMutex);
        if (m_heightBounds.IsValid()) { height = AZ::GetClamp(height, m_heightBounds.m_min, m_heightBounds.m_max); }
        outPosition.Set(inPosition.GetX(), inPosition.GetY(), height);
    }

    void TerrainCompositionHeightProviderComponent::GetHeights(
        AZStd::span<AZ::Vector3> inOutPositionList, AZStd::span<bool> terrainExistsList)
    {
        if (inOutPositionList.size() != terrainExistsList.size())
        {
            AZ_Assert(false, "The position list size doesn't match the terrain-exists list size.");
            return;
        }
        constexpr size_t BatchSize = 256;
        AZStd::array<float, BatchSize> heights{};
        AzFramework::Terrain::FloatRange heightBounds;
        {
            AZStd::shared_lock lock(m_heightBoundsMutex);
            heightBounds = m_heightBounds;
        }
        for (size_t offset = 0; offset < inOutPositionList.size(); offset += BatchSize)
        {
            const size_t count = AZStd::min(BatchSize, inOutPositionList.size() - offset);
            AZStd::fill_n(heights.begin(), count, 0.0f);
            auto exists = terrainExistsList.subspan(offset, count);
            AZStd::fill(exists.begin(), exists.end(), false);
            TerrainCompositionHeightRequestBus::Event(
                m_binding.GetCompositionAddress(), &TerrainCompositionHeightRequestBus::Events::GetHeights,
                m_binding.GetTerrainRegionEntityId(),
                AZStd::span<const AZ::Vector3>(inOutPositionList.data() + offset, count),
                AZStd::span<float>(heights.data(), count), exists);
            for (size_t index = 0; index < count; ++index)
            {
                inOutPositionList[offset + index].SetZ(heightBounds.IsValid()
                    ? AZ::GetClamp(heights[index], heightBounds.m_min, heightBounds.m_max) : heights[index]);
            }
        }
    }
} // namespace TerrainCompositor
