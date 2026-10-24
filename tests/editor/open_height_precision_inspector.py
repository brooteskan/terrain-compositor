"""Open the comparison scene and select its compositor; never save or alter the view."""
import azlmbr.bus as bus
import azlmbr.editor as editor
import azlmbr.legacy.general as general

general.idle_enable(True)
try:
    if general.open_level_no_prompt('VueLiveLink'):
        # open_level returns before prefab entities are available on startup.
        general.idle_wait_frames(30)
        owner=general.find_editor_entity('Terrain Compositor')
        if owner.IsValid():
            editor.ToolsApplicationRequestBus(bus.Broadcast,'SetSelectedEntities',[owner])
finally:
    general.idle_enable(False)
