"""Register prepared Vue terrain in the open VueLiveLink level and save it.

Run prepare_vue_composition.py externally and process its assets first. This
installer is idempotent; rerun both scripts after moving or rebaking terrains.
"""
import hashlib
import json
from pathlib import Path
import azlmbr.asset as asset
import azlmbr.bus as bus
import azlmbr.components as components
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as math
import azlmbr.paths as paths
import azlmbr.terrain as terrain


def register(save=True, info_path=None, composition=None, update_character=True, wait_frames=60):
    if general.is_in_game_mode() or general.get_current_level_name() != 'VueLiveLink':
        raise RuntimeError('Open VueLiveLink in edit mode first')
    project = Path(paths.projectroot)
    info_path = Path(info_path) if info_path else project/'Assets/VueLiveLink/composition/VueTerrainSurface.json'
    info = json.loads(info_path.read_text())
    asset_base = info_path.parent.relative_to(project).as_posix().lower()+'/'
    for source in info['sources']:
        if hashlib.sha256((project/source['source']).read_bytes()).hexdigest() != source['sha256']:
            raise RuntimeError(source['name']+': mesh changed; rerun prepare_vue_composition.py first')
    def product(path):
        value = asset.AssetCatalogRequestBus(bus.Broadcast, 'GetAssetIdByPath', path, math.Uuid(), False)
        if value is None or not value.is_valid():
            raise RuntimeError('Wait for Asset Processor to finish: '+path)
        return value
    def set_property(comp, path, value):
        try:
            result = editor.EditorComponentAPIBus(bus.Broadcast, 'SetComponentProperty', comp, path, value)
            if not result.IsSuccess(): raise RuntimeError(str(result.GetError()))
        except Exception as error: raise RuntimeError('Could not set '+path+': '+str(error)) from error
    def ensure_component(owner, name):
        kinds = editor.EditorComponentAPIBus(bus.Broadcast, 'FindComponentTypeIdsByEntityType', [name], 0)
        if len(kinds) != 1 or kinds[0].IsNull(): raise RuntimeError('Missing component: '+name)
        kind = kinds[0]
        found = editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentOfType', owner, kind)
        if found.IsSuccess(): return found.GetValue()
        added = editor.EditorComponentAPIBus(bus.Broadcast, 'AddComponentsOfType', owner, [kind])
        if not added.IsSuccess(): raise RuntimeError(str(added.GetError()))
        return added.GetValue()[0]
    def ensure_entity(name):
        owner = general.find_editor_entity(name)
        if not owner.IsValid():
            owner = editor.ToolsApplicationRequestBus(bus.Broadcast, 'CreateNewEntity', entity.EntityId())
            editor.EditorEntityAPIBus(bus.Event, 'SetName', owner, name)
        return owner
    composition = composition if composition is not None else general.find_editor_entity('Terrain Compositor')
    if not composition.IsValid(): raise RuntimeError('Missing Terrain Compositor')
    height_asset = product(asset_base+'vueterrainsurface.tif.streamingimage')
    clip_asset = product(asset_base+'vuecompositionclip.fbx.azmodel')
    center = [(a+b)/2 for a,b in zip(info['lower'], info['upper'])]
    width, depth = [b-a for a,b in zip(info['lower'], info['upper'])]
    stamp_owner = ensure_entity('Vue Terrain Walkable Surface')
    components.TransformBus(bus.Event, 'SetWorldTranslation', stamp_owner, math.Vector3(*center, 0.0))
    stamp = ensure_component(stamp_owner, 'Heightmap Stamp')
    for key, value in [('Target Composition', composition), ('Heightmap Asset', height_asset),
                       ('Footprint Width', width), ('Footprint Depth', depth), ('Height Scale', info['heightScale']),
                       ('Vertical Offset', 0.0), ('Strength', 1.0), ('Edge Feather Width', 0.0), ('Priority', 100)]:
        set_property(stamp, 'Configuration|'+key, value)
    clip_owner = ensure_entity('Vue Terrain Render Mask')
    components.TransformBus(bus.Event, 'SetWorldTranslation', clip_owner,
                            math.Vector3(*center, (info['heightScale']+10)/2+0.002))
    mesh = ensure_component(clip_owner, 'Mesh')
    set_property(mesh, 'Controller|Configuration|Model Asset', clip_asset)
    editor.EditorEntityAPIBus(bus.Event, 'SetVisibilityState', clip_owner, False)
    cutout = ensure_component(clip_owner, 'Terrain Mesh Cutout')
    for key, value in [('Target Composition', composition), ('Cutout Mesh', clip_asset),
                       ('Affect Rendering', True), ('Affect Collision / Queries', False),
                       ('Render Margin', 0.0), ('Collision Margin', 0.0)]:
        set_property(cutout, 'Configuration|'+key, value)
    if wait_frames: general.idle_wait_frames(wait_frames)
    if not update_character:
        if save: general.save_level()
        return {'registered': [s['name'] for s in info['sources']], 'spacing': info['spacing'], 'saved': save}
    root = general.find_editor_entity('Falconry First Person')
    if not root.IsValid(): raise RuntimeError('Missing first person character')
    position = components.TransformBus(bus.Event, 'GetWorldTranslation', root)
    height, exists = terrain.TerrainDataRequestBus(bus.Broadcast, 'GetHeight', position, 0)
    if not exists: raise RuntimeError('Character is outside terrain')
    components.TransformBus(bus.Event, 'SetWorldTranslation', root, math.Vector3(position.x, position.y, height+0.05))
    # The following property edits notify the prefab system after placement,
    # so the saved spawn transform includes the small initial ground clearance.
    controller = ensure_component(root, 'PhysX Character Controller')
    for key, value in [('Configuration|Maximum Slope Angle', 45.0), ('Configuration|Step Height', 0.3),
                       ('Configuration|Contact Offset', 0.03), ('Configuration|Maximum Speed', 100.0),
                       ('Shape Configuration|Capsule|Height', 1.8), ('Shape Configuration|Capsule|Radius', 0.3)]:
        set_property(controller, key, value)
    gameplay = ensure_component(root, 'PhysX Character Gameplay')
    set_property(gameplay, 'Gameplay Configuration|Gravity Multiplier', 1.0)
    set_property(gameplay, 'Gameplay Configuration|Ground Detection Box Height', 0.1)
    for owner in [stamp_owner, clip_owner, root]:
        editor.ToolsApplicationRequestBus(bus.Broadcast, 'AddDirtyEntity', owner)
    general.idle_wait_frames(10)
    if save: general.save_level()
    return {'registered': [s['name'] for s in info['sources']], 'spacing': info['spacing'],
            'characterGroundHeight': height, 'saved': save}


if __name__ == '__main__': print(json.dumps(register(), indent=2))
