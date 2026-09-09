#include <TerrainCompositor/TerrainCompositionProviderBinding.h>

namespace TerrainCompositor
{
    bool TerrainCompositionProviderBinding::PrepareToStart()
    {
        if (!m_active)
        {
            m_controlThread.BindForActivation();
        }
        return m_controlThread.Check();
    }

    void TerrainCompositionProviderBinding::Activate(
        AZ::EntityId terrainRegionEntityId, AZ::EntityId compositionEntityId)
    {
        AZ_Assert(m_controlThread.Check() && !m_active,
            "Terrain composition provider binding must be inactive on its control thread before activation.");
        m_active = true;
        m_terrainRegionEntityId = terrainRegionEntityId;
        m_compositionAddress = { AzFramework::EntityContextId::CreateNull(), compositionEntityId };
        AzFramework::EntityIdContextQueryBus::EventResult(
            m_compositionAddress.first, terrainRegionEntityId,
            &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
    }

    bool TerrainCompositionProviderBinding::BeginStop()
    {
        if (!m_active || !m_controlThread.Check())
        {
            return false;
        }
        m_active = false;
        return true;
    }

    void TerrainCompositionProviderBinding::Clear()
    {
        m_compositionAddress = TerrainCompositionAddress{};
        m_terrainRegionEntityId = AZ::EntityId{};
    }

    bool TerrainCompositionProviderBinding::HasOwningContextChanged() const
    {
        if (!m_controlThread.Check() || !m_active)
        {
            return false;
        }
        AzFramework::EntityContextId context{};
        AzFramework::EntityIdContextQueryBus::EventResult(
            context, m_terrainRegionEntityId, &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
        return context != m_compositionAddress.first;
    }
} // namespace TerrainCompositor
