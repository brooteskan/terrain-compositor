"""Edit-driven Vue overlap publication. There is no periodic scene monitor.

The monitor does no work in game mode. Published material assets also work after
reopening a saved level without a Vue connection. Dynamic runtime mesh motion
requires a runtime publisher; this module follows Editor/Vue sync changes only.
"""
import json
import time
from pathlib import Path
from PySide6 import QtCore
import azlmbr.asset as asset
import azlmbr.bus as bus
import azlmbr.components as components
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as azmath
import terrain_overlap as model

_monitor = None


def modules():
    import editor_link
    import material_response_editor
    return editor_link, material_response_editor


def raw_path(value):
    return asset.AssetCatalogRequestBus(bus.Broadcast, 'GetAssetPathById', value).replace('\\', '/').lower() if value and value.is_valid() else ''


def vector(v):
    return [float(v.x), float(v.y), float(v.z)]


def has_nonuniform_scale(owner):
    return editor.EditorComponentAPIBus(bus.Broadcast, 'HasComponentOfType', owner,
                                        editor.EditorNonUniformScaleComponentTypeId)


class EditWatch:
    """Native edit and filesystem notifications; no reads while idle."""
    def __init__(self, request):
        self.request = request
        self.enabled = True
        self.owners = set()
        self.files = []
        self.baseline = []
        self.handler = editor.ToolsApplicationNotificationBusHandler()
        self.handler.connect()
        for event in ('EntityRegistered', 'EntityDeregistered'):
            self.handler.add_callback(event, self.entity_changed)
        self.edits = editor.VueTerrainEditNotificationBusHandler()
        self.edits.connect()
        self.edits.add_callback('OnTerrainEdited', self.terrain_changed)
        self.precision = editor.TerrainHeightPrecisionNotificationBusHandler()
        self.precision.connect()
        self.precision.add_callback('OnHeightPrecisionChanged', lambda *args: self.request())
        self.handler.add_callback('AfterUndoRedo', lambda *args: self.request())
        self.watcher = QtCore.QFileSystemWatcher()
        self.watcher.fileChanged.connect(self.file_changed)
        self.watcher.directoryChanged.connect(self.file_changed)

    def entity_changed(self, *args):
        if self.enabled:
            self.request()

    def terrain_changed(self, args):
        if self.enabled and (not self.owners or str(args[0]) in self.owners):
            self.request()

    def file_state(self):
        values = []
        for path in self.files:
            try:
                stat = path.stat(); values.append((stat.st_size, stat.st_mtime_ns))
            except FileNotFoundError:
                values.append(None)
        return values

    def file_changed(self, *args):
        # A parent directory may change for an unrelated asset. Check only known
        # dependencies, and rearm watches after deletion or atomic replacement.
        state = self.file_state()
        self.arm_files()
        if state != self.baseline or (args and Path(args[0]) in self.files):
            self.baseline = state
            self.request()

    def arm_files(self):
        paths = self.watcher.files() + self.watcher.directories()
        if paths:
            self.watcher.removePaths(paths)
        watched = set()
        for path in self.files:
            if path.is_file():
                watched.add(str(path))
            parent = path.parent
            while not parent.is_dir() and parent != parent.parent:
                parent = parent.parent
            if parent.is_dir():
                watched.add(str(parent))
        if watched:
            self.watcher.addPaths(sorted(watched))

    def capture(self, rows, enabled=True):
        api, _ = modules()
        self.enabled = enabled
        self.owners = {str(row[0]['owner']) for row in rows}
        files = {api.PROJECT / 'Assets/VueLiveLink/terrain_overlap.json'}
        for target, source, current, _ in rows:
            if not enabled:
                continue
            try:
                dependencies = [Path(path) for path, _, _ in model.dependency_state(api.PROJECT, source)]
            except (OSError, ValueError):
                # Missing/corrupt inputs must keep their recovery subscriptions.
                dependencies = self.files + [Path(source['material']), Path(source['fbx'])]
            for path in dependencies:
                if not path.resolve().is_relative_to(api.PROJECT.resolve() / 'Assets'):
                    continue
                files.add(Path(path))
                # Track sidecars even before they exist.
                files.add(Path(str(path) + '.assetinfo'))
            if current.startswith('assets/vuelivelink/overlap/'):
                files.add((api.PROJECT / current).with_suffix('.overlap.json'))
        self.files = sorted(files)
        self.baseline = self.file_state()
        self.arm_files()

    def close(self):
        self.handler.disconnect()
        self.edits.disconnect()
        self.precision.disconnect()
        paths = self.watcher.files() + self.watcher.directories()
        if paths:
            self.watcher.removePaths(paths)


def height_precision():
    api, _ = modules()
    owner = general.find_editor_entity('Terrain Compositor')
    if not owner.IsValid():
        return 32
    # Use the editor adapter identity, whose property layout owns Configuration.
    found = editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentOfType', owner,
        azmath.Uuid_CreateString('{43588E85-B704-461F-AC94-8C9E1D916A9A}'))
    if not found.IsSuccess():
        raise ValueError('Terrain Compositor needs the Terrain Composition Gradient editor component')
    component = found.GetValue()
    bits = int(api._get(component, 'Configuration|Height Precision'))
    if bits not in (16, 32):
        raise ValueError('Height Precision must be 16 or 32')
    return bits


def snapshot():
    api, response = modules()
    rows = []
    for target in response.targets():
        if target['ground']:
            continue
        source = target['sourceSnapshot']
        if not source.get('modelProduct') or not source.get('bakedSourceTransformation'):
            continue
        owner = target['owner']
        current = raw_path(response.binding(target))
        base = model.original_material(api.PROJECT, current)
        if not base:
            raise ValueError(target['name'] + ': no material asset')
        # Component overrides and extra slot assignments are not represented by
        # the imported parent. Refuse rather than evaluating a different look.
        for entry in response.assignment_evidence(target).values():
            if entry['properties'] or (not entry['default'] and entry['overridden']):
                raise ValueError(target['name'] + ': material overrides need baking before overlap')
        world = components.TransformBus(bus.Event, 'GetWorldTM', owner)
        columns = [vector(world.GetBasisX()), vector(world.GetBasisY()), vector(world.GetBasisZ())]
        scale = vector(entity.NonUniformScaleRequestBus(bus.Event, 'GetScale', owner)) if has_nonuniform_scale(owner) else [1, 1, 1]
        placement = model.instance_mapping([[columns[c][r] for c in range(3)] for r in range(3)],
            vector(world.GetTranslation()), scale, source['bakedSourceTransformation'])
        row = dict(key=target['key'], name=target['name'], material=str(response.source_path(base)),
                   fbx=str(api.PROJECT / Path(source['modelProduct']).parent / 'terrain.fbx'), **placement)
        rows.append((target, row, current, base))
    return sorted(rows, key=lambda item: item[1]['key'])


def restore_owned(rows):
    api, response = modules()
    for target, _, current, base in rows:
        if current != base and raw_path(response.binding(target)) == current:
            api._set(target['component'], target['property'], response.product(base))


class Monitor:
    def __init__(self):
        self.key = None
        self.pending = None
        self.needs_snapshot = True
        self.busy = False
        self.last_status = None
        self.wait_since = None
        self.last_tick_ms = 0.0
        self.max_tick_ms = 0.0
        self.full_scans = 0
        self.timer = QtCore.QTimer()
        self.timer.setSingleShot(True)
        self.timer.timeout.connect(self.tick)
        self.watch = EditWatch(self.request_update)
        self.request_update()

    def request_update(self, *args):
        if not self.busy:
            self.needs_snapshot = True
            self.wait_since = None
            self.timer.start(150)  # Coalesce actual edits, never poll for them.

    def wait_for_work(self, detail):
        if self.wait_since is None:
            self.wait_since = time.monotonic()
        if time.monotonic() - self.wait_since < 240:
            self.timer.start(500)
        else:
            self.pending = None
            self.key = None
            self.status('waiting', detail)

    def status(self, phase, detail='', **values):
        api, _ = modules()
        result = dict(phase=phase, detail=detail, **values)
        if result == self.last_status:
            return
        model.write_json(api.STATE / 'terrain_overlap_status.json', result)
        self.last_status = result
        if phase == 'unsupported':
            print('Vue terrain overlap: ' + detail)

    def tick(self):
        if self.busy:
            return
        self.timer.stop()
        self.busy = True
        tick_started = time.perf_counter()
        rows = []
        try:
            api, response = modules()
            if str(general.get_current_level_name()) != api.LEVEL or general.is_in_game_mode():
                self.key = None; self.pending = None
                return
            if response._applying or (api._SESSION and api._SESSION.phase in ('terrain processing', 'converting', 'processing', 'loading')):
                self.wait_for_work('Sync is still busy; another edit or Sync will retry overlap')
                return
            if self.needs_snapshot or not self.pending:
                self.full_scans += 1
                config_path = api.PROJECT / 'Assets/VueLiveLink/terrain_overlap.json'
                config = json.loads(config_path.read_text()) if config_path.is_file() else {'enabled': False}
                rows = snapshot()
                sources = [r[1] for r in rows]
                width = float(config.get('width', 10)) if config.get('enabled', False) else 0
                bits = height_precision()
                key = model.fingerprint(api.PROJECT, sources, width, bits)
                self.needs_snapshot = False
                if key != self.key:
                    restore_owned(rows)  # Never leave an old footprint active while rebuilding.
                    self.key = key; self.pending = None
                    if width == 0 or len(rows) < 2:
                        self.status('disabled', 'Need at least two baked terrains and a positive transition width')
                        self.watch.capture(rows, enabled=width > 0)
                        return
                    outputs = model.prepare(api.PROJECT, sources, width, bits)
                    self.pending = (outputs, time.monotonic(), rows, width)
                    self.watch.capture(rows)
                    self.status('processing', contributors=len(rows), width=width)
                if not self.pending:
                    self.watch.capture(rows, enabled=width > 0)
                    return
            outputs, started, rows, width = self.pending
            products = {}
            for name, path in outputs.items():
                product_path = path.relative_to(api.PROJECT).with_suffix('.azmaterial').as_posix().lower()
                candidate = asset.AssetCatalogRequestBus(bus.Broadcast, 'GetAssetIdByPath', product_path, azmath.Uuid(), False)
                if not candidate or not candidate.is_valid() or not (api.PROJECT / 'Cache/pc' / product_path).is_file():
                    if time.monotonic() - started >= 240:
                        self.pending = None
                        self.key = None
                        self.status('waiting', 'Asset Processor timed out; another edit or refresh will retry', contributors=len(rows))
                    else:
                        self.timer.start(500)  # Only while this publication is pending.
                    return
                products[name] = (product_path, candidate)
            applied = []
            try:
                for target, source, _, base in rows:
                    old = response.binding(target)
                    if model.original_material(api.PROJECT, raw_path(old)) != base:
                        raise RuntimeError('Material changed while publishing overlap')
                    wanted, candidate = products[source['key']]
                    if raw_path(old) != wanted:
                        applied.append((target, old))
                        api._set(target['component'], target['property'], candidate)
                        if raw_path(response.binding(target)) != wanted:
                            raise RuntimeError('Overlap material binding readback failed')
            except Exception:
                for target, old in reversed(applied):
                    api._set(target['component'], target['property'], old)
                raise
            self.pending = None
            self.status('ready', contributors=len(rows), width=width, revision=self.key,
                        materials={name: p[0] for name, p in products.items()})
            self.watch.capture([(target, source, products[source['key']][0], base)
                                for target, source, _, base in rows])
        except Exception as error:
            # snapshot may fail before all sources are returned. Still detach
            # our children from every managed terrain; never overwrite a user assignment.
            try:
                api, response = modules()
                for target in response.targets():
                    if target['ground']: continue
                    current = raw_path(response.binding(target))
                    base = model.original_material(api.PROJECT, current)
                    if base != current:
                        api._set(target['component'], target['property'], response.product(base))
            except Exception as restore_error:
                error = RuntimeError(str(error) + '; restore: ' + str(restore_error))
            self.pending = None
            self.key = None
            self.status('unsupported', str(error))
            # Keep edit/file subscriptions after a rejected override or source.
            if rows or not self.watch.files:
                self.watch.capture(rows)
        finally:
            self.busy = False
            self.last_tick_ms = (time.perf_counter() - tick_started) * 1000
            self.max_tick_ms = max(self.max_tick_ms, self.last_tick_ms)


def start():
    global _monitor
    if _monitor is None:
        if not hasattr(editor, 'VueTerrainEditNotificationBusHandler') or not hasattr(editor, 'TerrainHeightPrecisionNotificationBusHandler'):
            api, _ = modules()
            model.write_json(api.STATE / 'terrain_overlap_status.json', dict(
                phase='restart-required', detail='Rebuild TGProject.Editor and restart to load terrain edit notifications'))
            raise RuntimeError('Terrain overlap needs the updated TGProject.Editor edit notifications; rebuild and restart Editor')
        _monitor = Monitor()
    return _monitor


def invalidate():
    monitor = start()
    monitor.request_update()


def stop():
    global _monitor
    if _monitor is not None:
        _monitor.timer.stop()
        _monitor.watch.close()
        _monitor = None
