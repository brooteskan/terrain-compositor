#include <TerrainCompositor/Components/TerrainCompositionHeightProviderComponent.h>
#include "../ComponentConfiguration.h"

#include <AzCore/Math/MathUtils.h>
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
            if (!serialize->IsRemovingReflection() &&
                serialize->FindClassData(azrtti_typeid<TerrainCompositionHeightProviderConfig>()))
            {
                return;
            }
            serialize->Class<TerrainCompositionHeightProviderConfig, AZ::ComponentConfig>()
                ->Version(1)
                ->Field("CompositionEntityId", &TerrainCompositionHeightProviderConfig::m_compositionEntityId);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<TerrainCompositionHeightProviderConfig>(
                    "Terrain Composition Height Provider Configuration", "Composition reference for this terrain region.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                    ->DataElement(AZ::Edit::UIHandlers::Default,
                        &TerrainCompositionHeightProviderConfig::m_compositionEntityId,
                        "Terrain Composition", "Composition whose height and terrain-existence snapshot provides this region.")
                    ->Attribute(AZ::Edit::Attributes::RequiredService, AZ_CRC_CE("TerrainCompositionService"));
            }
        }
    }

    void TerrainCompositionHeightProviderComponent::Reflect(AZ::ReflectContext* context)
    {
        TerrainCompositionHeightProviderConfig::Reflect(context);
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<TerrainCompositionHeightProviderComponent, AZ::Component>()
                ->Version(1)
                ->Field("Configuration", &TerrainCompositionHeightProviderComponent::m_configuration);
        }
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

    void TerrainCompositionHeightProviderComponent::Activate() { StartProvider(GetEntityId()); }
    void TerrainCompositionHeightProviderComponent::Deactivate() { StopProvider(); }
    void TerrainCompositionHeightProviderComponent::EditorActivate(AZ::EntityId entityId) { StartProvider(entityId); }
    void TerrainCompositionHeightProviderComponent::EditorDeactivate([[maybe_unused]] AZ::EntityId entityId) { StopProvider(); }

    void TerrainCompositionHeightProviderComponent::StartProvider(AZ::EntityId terrainRegionEntityId)
    {
        if (!m_binding.PrepareToStart()) { return; }
        StopProvider();
        m_binding.Activate(terrainRegionEntityId, m_configuration.m_compositionEntityId);
        RefreshHeightBounds();
        if (m_configuration.m_compositionEntityId.IsValid())
        {
            LmbrCentral::DependencyNotificationBus::Handler::BusConnect(m_configuration.m_compositionEntityId);
        }
        AzFramework::Terrain::TerrainDataNotificationBus::Handler::BusConnect();
        Terrain::TerrainAreaHeightRequestBus::Handler::BusConnect(terrainRegionEntityId);
        AZ::SystemTickBus::Handler::BusConnect();
        RefreshArea();
    }

    void TerrainCompositionHeightProviderComponent::StopProvider()
    {
        if (!m_binding.BeginStop()) { return; }
        Terrain::TerrainAreaHeightRequestBus::Handler::BusDisconnect();
        LmbrCentral::DependencyNotificationBus::Handler::BusDisconnect();
        AzFramework::Terrain::TerrainDataNotificationBus::Handler::BusDisconnect();
        AZ::SystemTickBus::Handler::BusDisconnect();
        RefreshArea();
        m_binding.Clear();
        AZStd::unique_lock lock(m_heightBoundsMutex);
        m_heightBounds = AzFramework::Terrain::FloatRange::CreateNull();
    }

    void TerrainCompositionHeightProviderComponent::RestartProvider()
    {
        if (m_binding.IsActive())
        {
            const auto region = m_binding.GetTerrainRegionEntityId();
            StopProvider();
            StartProvider(region);
        }
    }

    void TerrainCompositionHeightProviderComponent::RefreshArea() const
    {
        const AZ::EntityId terrainRegionEntityId = m_binding.GetTerrainRegionEntityId();
        if (terrainRegionEntityId.IsValid())
        {
            Terrain::TerrainSystemServiceRequestBus::Broadcast(
                &Terrain::TerrainSystemServiceRequests::RefreshArea, terrainRegionEntityId,
                AzFramework::Terrain::TerrainDataNotifications::TerrainDataChangedMask::HeightData |
                AzFramework::Terrain::TerrainDataNotifications::TerrainDataChangedMask::SurfaceData);
        }
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
        if (!m_binding.CheckControlThread()) { return false; }
        return Internal::ReadConfiguration<TerrainCompositionHeightProviderConfig>(baseConfig, [this](const auto& value)
        {
            const bool changed = value.m_compositionEntityId != m_configuration.m_compositionEntityId;
            m_configuration = value;
            if (changed) { RestartProvider(); }
        });
    }

    bool TerrainCompositionHeightProviderComponent::WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const
    {
        if (!m_binding.CheckControlThread()) { return false; }
        return Internal::WriteConfiguration(outBaseConfig, m_configuration);
    }

    AZStd::string TerrainCompositionHeightProviderComponent::GetStatusMessage() const
    {
        if (!m_binding.CheckControlThread()) { return "Unavailable off the control thread."; }
        if (!m_binding.IsActive()) { return "Inactive: no terrain height provider."; }
        if (!m_configuration.m_compositionEntityId.IsValid()) { return "Select a Terrain Composition entity."; }
        const auto& compositionAddress = m_binding.GetCompositionAddress();
        if (compositionAddress.first.IsNull()) { return "Waiting for entity context ownership."; }
        if (!TerrainCompositionHeightRequestBus::HasHandlers(compositionAddress))
        {
            return "Terrain Composition is unavailable in this entity context.";
        }
        return "Ready: composition height and terrain existence are provided to this terrain region.";
    }

    void TerrainCompositionHeightProviderComponent::OnSystemTick()
    {
        if (m_binding.HasOwningContextChanged()) { RestartProvider(); }
    }

    void TerrainCompositionHeightProviderComponent::OnCompositionChanged()
    {
        RefreshArea();
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
            RefreshArea();
        }
    }

    void TerrainCompositionHeightProviderComponent::OnTerrainDataChanged(
        [[maybe_unused]] const AZ::Aabb& dirtyRegion, TerrainDataChangedMask dataChangedMask)
    {
        if ((dataChangedMask & TerrainDataChangedMask::Settings) == TerrainDataChangedMask::Settings)
        {
            RefreshHeightBounds();
            RefreshArea();
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
