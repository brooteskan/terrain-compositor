#pragma once

#include <AzCore/std/string/string.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>

namespace TerrainCompositor
{
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
