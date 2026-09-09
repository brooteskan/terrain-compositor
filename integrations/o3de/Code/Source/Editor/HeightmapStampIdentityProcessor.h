#pragma once

#include <AzToolsFramework/Prefab/Spawnable/PrefabProcessor.h>

namespace TerrainCompositor
{
    AZStd::string ResolvePrefabStampOrderKey(
        const AzToolsFramework::Prefab::Instance& root, AZ::EntityId entityId);

    class HeightmapStampIdentityProcessor final
        : public AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessor
    {
    public:
        AZ_CLASS_ALLOCATOR(HeightmapStampIdentityProcessor, AZ::SystemAllocator);
        AZ_RTTI(TerrainCompositor::HeightmapStampIdentityProcessor, "{B5FEC901-1FA7-4D0A-BB73-EF8E5F973456}",
            AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessor);
        static void Reflect(AZ::ReflectContext* context);
        void Process(AzToolsFramework::Prefab::PrefabConversionUtils::PrefabProcessorContext& context) override;
    };
} // namespace TerrainCompositor
