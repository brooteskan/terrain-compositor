#include "HeightmapStampEditorSystemComponent.h"
#include "HeightmapStampIdentityProcessor.h"

#include <AzCore/Serialization/SerializeContext.h>
#include <AzToolsFramework/Entity/PrefabEditorEntityOwnershipInterface.h>

namespace TerrainCompositor
{
    void HeightmapStampEditorSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        HeightmapStampIdentityProcessor::Reflect(context);
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<HeightmapStampEditorSystemComponent, AZ::Component>()->Version(1);
        }
    }

    void HeightmapStampEditorSystemComponent::Activate()
    {
        if (m_active) { return; }
        m_active = true;
        m_refreshPending = true;
        HeightmapStampIdentityInterface::Register(this);
        AzToolsFramework::Prefab::PrefabPublicNotificationBus::Handler::BusConnect();
        AzToolsFramework::ToolsApplicationNotificationBus::Handler::BusConnect();
        ScheduleRefresh();
    }

    void HeightmapStampEditorSystemComponent::Deactivate()
    {
        if (!m_active) { return; }
        m_active = false;
        AZ::SystemTickBus::Handler::BusDisconnect();
        AzToolsFramework::ToolsApplicationNotificationBus::Handler::BusDisconnect();
        AzToolsFramework::Prefab::PrefabPublicNotificationBus::Handler::BusDisconnect();
        HeightmapStampIdentityInterface::Unregister(this);
        m_refreshPending = false;
        m_undoRedo = false;
        m_propagationDepth = 0;
        HeightmapStampIdentityNotificationBus::Broadcast(&HeightmapStampIdentityNotificationBus::Events::OnStampIdentitiesChanged);
    }

    AZStd::string HeightmapStampEditorSystemComponent::ResolveStampOrderKey(AZ::EntityId editorEntityId) const
    {
        if (!m_active || m_propagationDepth != 0 || m_undoRedo)
        {
            return {};
        }
        auto* ownership = AZ::Interface<AzToolsFramework::PrefabEditorEntityOwnershipInterface>::Get();
        if (!ownership)
        {
            return {};
        }
        const auto root = ownership->GetRootPrefabInstance();
        return root ? ResolvePrefabStampOrderKey(root->get(), editorEntityId) : AZStd::string{};
    }

    void HeightmapStampEditorSystemComponent::OnPrefabInstancePropagationBegin()
    {
        ++m_propagationDepth;
        m_refreshPending = true;
        AZ::SystemTickBus::Handler::BusDisconnect();
        // Fail closed while aliases are being replaced; copies must not briefly reuse their parent's baked key.
        HeightmapStampIdentityNotificationBus::Broadcast(&HeightmapStampIdentityNotificationBus::Events::OnStampIdentitiesChanged);
    }

    void HeightmapStampEditorSystemComponent::OnPrefabInstancePropagationEnd()
    {
        if (m_propagationDepth != 0)
        {
            --m_propagationDepth;
        }
        ScheduleRefresh();
    }

    void HeightmapStampEditorSystemComponent::OnRootPrefabInstanceLoaded() { ScheduleRefresh(); }
    void HeightmapStampEditorSystemComponent::OnAllTemplatesRemoved() { ScheduleRefresh(); }

    void HeightmapStampEditorSystemComponent::BeforeUndoRedo()
    {
        m_undoRedo = true;
        m_refreshPending = true;
        AZ::SystemTickBus::Handler::BusDisconnect();
        HeightmapStampIdentityNotificationBus::Broadcast(&HeightmapStampIdentityNotificationBus::Events::OnStampIdentitiesChanged);
    }

    void HeightmapStampEditorSystemComponent::AfterUndoRedo()
    {
        m_undoRedo = false;
        ScheduleRefresh();
    }

    void HeightmapStampEditorSystemComponent::ScheduleRefresh()
    {
        m_refreshPending = true;
        if (m_active && !m_undoRedo && m_propagationDepth == 0 && !AZ::SystemTickBus::Handler::BusIsConnected())
            AZ::SystemTickBus::Handler::BusConnect();
    }

    void HeightmapStampEditorSystemComponent::OnSystemTick()
    {
        if (m_active && m_refreshPending && !m_undoRedo && m_propagationDepth == 0)
        {
            m_refreshPending = false;
            AZ::SystemTickBus::Handler::BusDisconnect();
            HeightmapStampIdentityNotificationBus::Broadcast(&HeightmapStampIdentityNotificationBus::Events::OnStampIdentitiesChanged);
        }
    }
} // namespace TerrainCompositor
