#pragma once

#include <AzCore/Component/TickBus.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzCore/std/string/string.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>

namespace TerrainCompositor
{
    inline constexpr float EditorPreviewStatusPollIntervalSeconds = 0.25f;

    // The editor owns serialized configuration; this owner creates its runtime preview only during activation.
    template<class Preview>
    class EditorPreview final
        : private AZ::TickBus::Handler
        , private AzToolsFramework::EntitySelectionEvents::Bus::Handler
    {
    public:
        EditorPreview(AzToolsFramework::Components::EditorComponentBase& editor, const char* inactiveStatus)
            : m_editor(editor), m_inactiveStatus(inactiveStatus), m_status(inactiveStatus) {}
        ~EditorPreview() { Deactivate(); }

        template<class Configuration>
        void Activate(const Configuration& configuration) { Activate(configuration, [](Preview&) {}); }

        template<class Configuration, class Prepare>
        void Activate(const Configuration& configuration, Prepare prepare)
        {
            m_preview = AZStd::make_unique<Preview>(configuration);
            prepare(*m_preview);
            m_preview->EditorActivate(m_editor.GetEntityId());
            m_status = m_preview->GetStatusMessage();
            m_statusElapsed = 0.0f;
            AzToolsFramework::EntitySelectionEvents::Bus::Handler::BusConnect(m_editor.GetEntityId());
            if (m_editor.IsSelected()) OnSelected();
        }

        void Deactivate()
        {
            AZ::TickBus::Handler::BusDisconnect();
            AzToolsFramework::EntitySelectionEvents::Bus::Handler::BusDisconnect();
            if (m_preview)
            {
                m_preview->EditorDeactivate(m_editor.GetEntityId());
                m_preview.reset();
            }
            m_status = m_inactiveStatus;
        }

        template<class Configuration>
        void Refresh(const Configuration& configuration)
        {
            if (m_preview)
            {
                m_preview->ReadInConfig(&configuration);
                m_status = m_preview->GetStatusMessage();
            }
        }

        Preview* get() const { return m_preview.get(); }
        Preview* operator->() const { return get(); }
        explicit operator bool() const { return bool(m_preview); }
        const AZStd::string& GetStatus() const { return m_status; }

    private:
        void OnSelected() override
        {
            m_statusElapsed = 0.0f;
            if (!AZ::TickBus::Handler::BusIsConnected()) AZ::TickBus::Handler::BusConnect();
        }

        void OnDeselected() override
        {
            AZ::TickBus::Handler::BusDisconnect();
            m_statusElapsed = 0.0f;
        }

        void OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time) override
        {
            if (!m_preview || !m_editor.IsSelected())
            {
                return;
            }
            m_statusElapsed += deltaTime;
            if (m_statusElapsed < EditorPreviewStatusPollIntervalSeconds)
            {
                return;
            }
            m_statusElapsed = 0.0f;
            const auto nextStatus = m_preview->GetStatusMessage();
            if (nextStatus == m_status)
            {
                return;
            }
            m_status = nextStatus;
            AzToolsFramework::ToolsApplicationNotificationBus::Broadcast(
                &AzToolsFramework::ToolsApplicationNotificationBus::Events::InvalidatePropertyDisplayForComponent,
                AZ::EntityComponentIdPair(m_editor.GetEntityId(), m_editor.GetId()),
                AzToolsFramework::Refresh_AttributesAndValues);
        }

        AzToolsFramework::Components::EditorComponentBase& m_editor;
        const char* m_inactiveStatus;
        AZStd::unique_ptr<Preview> m_preview;
        AZStd::string m_status;
        float m_statusElapsed = 0.0f;
    };
} // namespace TerrainCompositor
