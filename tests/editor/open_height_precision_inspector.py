"""Open the comparison scene and select its compositor; never save or alter the view."""
import azlmbr.bus as bus
import azlmbr.editor as editor
import azlmbr.legacy.general as general

if general.open_level_no_prompt('VueLiveLink'):
    owner=general.find_editor_entity('Terrain Compositor')
    if owner.IsValid():
        editor.ToolsApplicationRequestBus(bus.Broadcast,'SetSelectedEntities',[owner])
