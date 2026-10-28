"""Inspector-triggered Vue collision refresh. Work runs only after a request.

Capture live transforms without saving the user's level; prepare immutable
assets in a worker, wait for Asset Processor, then update the two owned surface
entities. Keep prior bindings available for rollback and leave Save to the user.
"""
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess
import time
import uuid
from PySide6 import QtCore
import azlmbr.asset as asset
import azlmbr.bus as bus
import azlmbr.components as components
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as math
import azlmbr.paths as paths
import terrain_overlap

_bridge = None
PROJECT = Path(paths.projectroot)
OWNED = {
    'Vue Terrain Walkable Surface': {'Heightmap Stamp': [
        'Configuration|'+key for key in ['Target Composition', 'Heightmap Asset', 'Footprint Width',
        'Footprint Depth', 'Height Scale', 'Vertical Offset', 'Strength', 'Edge Feather Width', 'Priority']]},
    'Vue Terrain Render Mask': {
        'Mesh': ['Controller|Configuration|Model Asset'],
        'Terrain Mesh Cutout': ['Configuration|'+key for key in ['Target Composition', 'Cutout Mesh',
            'Affect Rendering', 'Affect Collision / Queries', 'Render Margin', 'Collision Margin']]}}


def vector(value): return [float(value.x), float(value.y), float(value.z)]


def snapshot(composition):
    import editor_link as api
    if general.is_in_game_mode(): raise RuntimeError('Exit play mode before refreshing the collision surface.')
    if general.get_current_level_name() != 'VueLiveLink': raise RuntimeError('Open VueLiveLink before refreshing its collision surface.')
    expected = general.find_editor_entity('Terrain Compositor')
    if not expected.IsValid() or expected.ToString() != composition.ToString():
        raise RuntimeError('Use the Terrain Compositor entity for VueLiveLink.')
    if api._SESSION and api._SESSION.phase in ('terrain processing', 'converting', 'processing', 'loading'):
        raise RuntimeError('Wait for the Vue terrain bake to finish, then refresh again.')
    root = general.find_editor_entity(api.ROOT)
    parents = [child for child in api._children(root) if api._name(child) == api.TERRAINS] if root.IsValid() else []
    if len(parents) != 1: raise RuntimeError('The Vue terrains group is missing or ambiguous.')
    rows = []
    for owner in api._children(parents[0]):
        try: source_component = api._component(owner, api.TERRAIN_SOURCE_COMPONENT)
        except RuntimeError: continue
        data = json.loads(api._get(source_component, 'Vue terrain data'))
        if not data.get('modelProduct') or not data.get('bakedSourceTransformation'):
            raise RuntimeError('Bake '+api._name(owner)+' before refreshing the collision surface.')
        source = (PROJECT/Path(data['modelProduct']).parent/'terrain.fbx').resolve()
        if not source.is_relative_to(PROJECT/'Assets'): raise RuntimeError('Vue terrain mesh is outside project Assets.')
        world = components.TransformBus(bus.Event, 'GetWorldTM', owner)
        columns = [vector(world.GetBasisX()), vector(world.GetBasisY()), vector(world.GetBasisZ())]
        has_scale = editor.EditorComponentAPIBus(bus.Broadcast, 'HasComponentOfType', owner, editor.EditorNonUniformScaleComponentTypeId)
        scale = vector(entity.NonUniformScaleRequestBus(bus.Event, 'GetScale', owner)) if has_scale else [1,1,1]
        placement = terrain_overlap.instance_mapping([[columns[c][r] for c in range(3)] for r in range(3)],
            vector(world.GetTranslation()), scale, data['bakedSourceTransformation'])
        rows.append(dict(name=api._name(owner), entity=data['uid'], source=source.relative_to(PROJECT).as_posix(),
            sha256=hashlib.sha256(source.read_bytes()).hexdigest(), matrix=placement['matrix'], translation=placement['translation']))
    if not rows: raise RuntimeError('No baked Vue terrains were found.')
    return {'version': 2, 'sources': sorted(rows, key=lambda row: row['entity'])}


def revision(manifest):
    return hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode()).hexdigest()[:24]


def component(owner, name):
    kind = editor.EditorComponentAPIBus(bus.Broadcast, 'FindComponentTypeIdsByEntityType', [name], 0)[0]
    found = editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentOfType', owner, kind)
    return found.GetValue() if found.IsSuccess() else None


def capture_bindings():
    result = []
    for name, kinds in OWNED.items():
        owner = general.find_editor_entity(name)
        record = dict(name=name, exists=owner.IsValid(), values=[])
        if owner.IsValid():
            record['transform'] = components.TransformBus(bus.Event, 'GetWorldTM', owner)
            for kind, properties in kinds.items():
                comp = component(owner, kind)
                for path in properties if comp else []:
                    found = editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentProperty', comp, path)
                    if not found.IsSuccess(): raise RuntimeError('Could not preserve '+name+' / '+path)
                    record['values'].append((comp, path, found.GetValue()))
        result.append(record)
    return result


def restore_bindings(records):
    for record in records:
        owner = general.find_editor_entity(record['name'])
        if not record['exists']:
            if owner.IsValid(): editor.ToolsApplicationRequestBus(bus.Broadcast, 'DeleteEntityAndAllDescendants', owner)
        elif owner.IsValid():
            components.TransformBus(bus.Event, 'SetWorldTM', owner, record['transform'])
            for comp, path, value in record['values']:
                outcome = editor.EditorComponentAPIBus(bus.Broadcast, 'SetComponentProperty', comp, path, value)
                if not outcome.IsSuccess(): raise RuntimeError('Could not restore '+path)


class Refresh:
    def __init__(self, composition, finished):
        self.composition, self.finished = composition, finished
        self.process = self.log = None
        self.backup = None
        self.phase = 'preparing'
        self.timer = QtCore.QTimer()
        self.timer.setInterval(250)
        self.timer.timeout.connect(self.tick)
        self.deadline = time.monotonic()+300
        self.manifest = snapshot(composition)
        self.key = revision(self.manifest)
        self.destination = PROJECT/'Assets/VueLiveLink/composition/revisions'/self.key
        self.info_path = self.destination/'VueTerrainSurface.json'
        self.folder = PROJECT/'user/VueLiveLink/collision-refresh'/uuid.uuid4().hex
        self.folder.mkdir(parents=True)
        self.prepared = self.folder/'prepared'
        (self.folder/'manifest.json').write_text(json.dumps(self.manifest, indent=2))

    def status(self, text, busy=True):
        editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event, 'SetRefreshStatus', self.composition, text, busy)
        (self.folder/'status.json').write_text(json.dumps(dict(phase=self.phase, detail=text, busy=busy)))

    def start(self):
        import editor_link
        required = ['VueTerrainSurface.tif', 'VueTerrainSurface.tif.assetinfo',
                    'VueCompositionClip.fbx', 'VueCompositionClip.fbx.assetinfo', 'VueTerrainSurface.json']
        if all((self.destination/name).is_file() for name in required):
            self.phase = 'processing'
            self.status('Waiting for the refreshed terrain assets...')
        else:
            self.log = (self.folder/'worker.log').open('w')
            environment = dict(os.environ)
            environment.pop('PYTHONHOME', None); environment.pop('PYTHONPATH', None)
            self.process = subprocess.Popen([editor_link._python_worker(),
                str(PROJECT/'Editor/Scripts/prepare_vue_composition.py'), '--manifest', str(self.folder/'manifest.json'),
                '--output', str(self.prepared)], stdout=self.log, stderr=subprocess.STDOUT, env=environment,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.status('Rebuilding the collision surface in the background...')
        self.timer.start()

    def finish(self, message, error=False):
        self.timer.stop()
        if self.process and self.process.poll() is None: self.process.terminate()
        if self.log: self.log.close(); self.log = None
        if error and self.backup:
            try: restore_bindings(self.backup)
            except Exception as rollback: message += ' Restore failed: '+str(rollback)
        self.phase = 'error' if error else 'ready'
        self.status(message, False)
        self.finished(self)

    def tick(self):
        try:
            if general.is_in_game_mode() or general.get_current_level_name() != 'VueLiveLink':
                # Never apply or roll back Editor changes into a different level.
                self.backup = None
                self.finish('Refresh cancelled because the level or play mode changed.', True)
                return
            current = general.find_editor_entity('Terrain Compositor')
            if not current.IsValid() or current.ToString() != self.composition.ToString():
                self.backup = None
                self.finish('Refresh cancelled because the compositor changed.', True)
                return
            if time.monotonic() > self.deadline: raise RuntimeError('Refresh timed out. Check Asset Processor, then try again.')
            if self.phase == 'preparing':
                code = self.process.poll()
                if code is None: return
                self.log.close(); self.log = None
                if code != 0:
                    detail = (self.folder/'worker.log').read_text(errors='replace').strip().splitlines()
                    raise RuntimeError('Surface preparation failed: '+(detail[-1] if detail else str(code)))
                if revision(snapshot(self.composition)) != self.key: raise RuntimeError('Terrains changed during refresh. Refresh again when edits are finished.')
                self.destination.mkdir(parents=True, exist_ok=True)
                for source in self.prepared.iterdir():
                    os.replace(source, self.destination/source.name)
                self.phase = 'processing'
                self.status('Processing the refreshed terrain assets...')
            if self.phase == 'processing':
                relative = self.destination.relative_to(PROJECT).as_posix().lower()+'/'
                for path in ['vueterrainsurface.tif.streamingimage', 'vuecompositionclip.fbx.azmodel']:
                    product = asset.AssetCatalogRequestBus(bus.Broadcast, 'GetAssetIdByPath', relative+path, math.Uuid(), False)
                    if product is None or not product.is_valid(): return
                if revision(snapshot(self.composition)) != self.key: raise RuntimeError('Terrains changed during refresh. Refresh again when edits are finished.')
                self.backup = capture_bindings()
                api = runpy.run_path(str(PROJECT/'Editor/Scripts/register_vue_composition.py'))
                editor.ToolsApplicationRequestBus(bus.Broadcast, 'BeginUndoBatch', 'Refresh Vue collision surface')
                try:
                    api['register'](save=False, info_path=self.info_path, composition=self.composition,
                                    update_character=False, wait_frames=0)
                finally: editor.ToolsApplicationRequestBus(bus.Broadcast, 'EndUndoBatch')
                self.info = json.loads(self.info_path.read_text())
                self.phase = 'loading'
                self.status('Checking the refreshed collision surface...')
            if self.phase == 'loading':
                import azlmbr.terrain as terrain
                for x,y,height in self.info['samples']:
                    actual, exists = terrain.TerrainDataRequestBus(bus.Broadcast, 'GetHeight', math.Vector3(x,y,0.0), 0)
                    if not exists or abs(actual-height) > 0.12: return
                if revision(snapshot(self.composition)) != self.key: raise RuntimeError('Terrains changed during refresh. Refresh again when edits are finished.')
                self.backup = None
                self.finish('Collision surface refreshed for %d Vue terrains. Save the level to keep it.' % len(self.info['sources']))
        except Exception as error:
            self.finish(str(error), True)


class Bridge:
    def __init__(self):
        self.job = None
        self.handler = bus.NotificationHandler('TerrainCollisionSurfaceRefreshNotificationBus')
        self.handler.connect()
        self.handler.add_callback('OnRefreshCollisionSurface', self.request)

    def request(self, args):
        composition = args[0]
        if self.job:
            editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event, 'SetRefreshStatus', composition,
                'A collision surface refresh is already running.', composition.ToString() == self.job.composition.ToString())
            return
        try:
            self.job = Refresh(composition, self.finished)
            self.job.start()
        except Exception as error:
            if self.job:
                self.job.finish(str(error), True)
            else:
                editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event, 'SetRefreshStatus', composition, str(error), False)

    def finished(self, job):
        if self.job is job: self.job = None


def start():
    global _bridge
    if _bridge is None and hasattr(editor, 'TerrainCollisionSurfaceEditorRequestBus'):
        _bridge = Bridge()
    return _bridge
