#pragma once

#include <AzCore/Component/EntityId.h>
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/RTTI/ReflectContext.h>
#include <AzCore/std/containers/vector.h>

namespace TerrainCompositor
{
    //! A binding is an instance identity, not a surface tag. Equal tags must not merge coordinate frames.
    struct TerrainOverlapSourceConfig
    {
        AZ_TYPE_INFO(TerrainOverlapSourceConfig, "{7FAD7265-8152-43A3-9F57-117157D88743}");
        static void Reflect(AZ::ReflectContext* context);
        AZ::EntityId m_stampEntityId;
        AZ::u32 m_materialSlot = 1;
    };

    struct TerrainOverlapConfig
    {
        AZ_TYPE_INFO(TerrainOverlapConfig, "{5ADB4F50-7E56-43B1-865B-525A6924D935}");
        static void Reflect(AZ::ReflectContext* context);
        bool m_enabled = false;
        float m_width = 1.0f;
        //! False preserves stamp precedence; true resolves the exposed envelope of retained surfaces.
        bool m_retainedSurfaces = false;
        bool m_flatGround = false;
        float m_groundHeight = 0.0f;
        AZ::u32 m_groundMaterialSlot = 1;
        AZStd::vector<TerrainOverlapSourceConfig> m_sources;
    };
}
