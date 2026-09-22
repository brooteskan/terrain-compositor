#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <AzToolsFramework/Prefab/PrefabPublicNotificationBus.h>
#include <TerrainCompositor/HeightmapStampIdentity.h>

namespace TerrainCompositor
{
    //! Shared prefab identity adapter for dedicated editor components and legacy generic wrappers.
    class HeightmapStampEditorSystemComponent final
        : public AZ::Component
        , public HeightmapStampIdentityRequests
        , private AzToolsFramework::Prefab::PrefabPublicNotificationBus::Handler
        , private AzToolsFramework::ToolsApplicationNotificationBus::Handler
        , private AZ::SystemTickBus::Handler
    {
    public:
        AZ_COMPONENT(HeightmapStampEditorSystemComponent, "{24A1BC86-E71F-4D2C-8439-F130889D1B5A}",
            HeightmapStampIdentityRequests);
        static void Reflect(AZ::ReflectContext* context);
        void Activate() override;
        void Deactivate() override;
        AZStd::string ResolveStampOrderKey(AZ::EntityId editorEntityId) const override;

    private:
        void OnPrefabInstancePropagationBegin() override;
        void OnPrefabInstancePropagationEnd() override;
        void OnRootPrefabInstanceLoaded() override;
        void OnAllTemplatesRemoved() override;
        void BeforeUndoRedo() override;
        void AfterUndoRedo() override;
        void OnSystemTick() override;
        void ScheduleRefresh();

        unsigned m_propagationDepth = 0;
        bool m_undoRedo = false;
        bool m_refreshPending = false;
        bool m_active = false;
    };
} // namespace TerrainCompositor
