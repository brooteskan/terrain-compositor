#pragma once

#include <AzCore/std/string/string.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>

namespace TerrainCompositor
{
    // Previews are constructed by Activate, never on the asynchronous deserialization thread.
    template<class Preview>
    void ActivateEditorPreview(Preview& preview, AZ::EntityId entityId, AZStd::string& status, float& statusElapsed)
    {
        preview.EditorActivate(entityId);
        status = preview.GetStatusMessage();
        statusElapsed = 0.0f;
    }

    template<class Preview>
    void StopEditorPreview(AZStd::unique_ptr<Preview>& preview, AZ::EntityId entityId)
    {
        if (preview)
        {
            preview->EditorDeactivate(entityId);
            preview.reset();
        }
    }

    template<class Preview, class Configuration>
    void RefreshEditorPreview(Preview* preview, const Configuration& configuration, AZStd::string& status)
    {
        if (preview)
        {
            preview->ReadInConfig(&configuration);
            status = preview->GetStatusMessage();
        }
    }

    inline constexpr float EditorPreviewStatusPollIntervalSeconds = 0.25f;

    template<class EditorComponent, class PreviewComponent>
    void PollEditorPreviewStatus(
        EditorComponent& editor, const PreviewComponent* preview, float deltaTime,
        float& statusElapsed, AZStd::string& status)
    {
        if (!preview || !editor.IsSelected())
        {
            return;
        }
        statusElapsed += deltaTime;
        if (statusElapsed < EditorPreviewStatusPollIntervalSeconds)
        {
            return;
        }
        statusElapsed = 0.0f;
        const auto nextStatus = preview->GetStatusMessage();
        if (nextStatus == status)
        {
            return;
        }
        status = nextStatus;
        AzToolsFramework::ToolsApplicationNotificationBus::Broadcast(
            &AzToolsFramework::ToolsApplicationNotificationBus::Events::InvalidatePropertyDisplayForComponent,
            AZ::EntityComponentIdPair(editor.GetEntityId(), editor.GetId()),
            AzToolsFramework::Refresh_AttributesAndValues);
    }
} // namespace TerrainCompositor
