#pragma once

#include <TerrainCompositor/HeightmapControlThread.h>
#include <TerrainCompositor/TerrainCompositionBus.h>

namespace TerrainCompositor
{
    //! Bus-neutral lifecycle and address state shared by terrain composition provider adapters.
    class TerrainCompositionProviderBinding
    {
    public:
        bool PrepareToStart();
        void Activate(AZ::EntityId terrainRegionEntityId, AZ::EntityId compositionEntityId);
        void Activate(AZ::EntityId terrainRegionEntityId, AZ::EntityId compositionEntityId,
            const AzFramework::EntityContextId& context);
        bool BeginStop();
        void Clear();

        bool CheckControlThread() const { return m_controlThread.Check(); }
        bool IsActive() const { return m_active; }
        AZ::EntityId GetTerrainRegionEntityId() const { return m_terrainRegionEntityId; }
        const TerrainCompositionAddress& GetCompositionAddress() const { return m_compositionAddress; }
        bool HasOwningContextChanged() const;

    private:
        HeightmapControlThread m_controlThread;
        TerrainCompositionAddress m_compositionAddress;
        AZ::EntityId m_terrainRegionEntityId;
        bool m_active = false;
    };
} // namespace TerrainCompositor
