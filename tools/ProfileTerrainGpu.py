"""Capture matched terrain GPU workloads in a dedicated O3DE Editor; never save.

Set TC_GPU_CONFIG to a JSON configuration and launch Editor --runpython this file.
Outputs use a fresh directory. Timestamp and pipeline-statistics captures are
separate, delayed diagnostic frames, not synchronized with CPU frame samples.
"""
import hashlib
import ctypes
import json
import os
from pathlib import Path
import shutil
import time
import traceback

import azlmbr.asset as asset
import azlmbr.atom as atom
import azlmbr.bus as bus
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as math
import azlmbr.paths as paths
import azlmbr.terrain_canvas as canvas

CONFIG = json.loads(Path(os.environ['TC_GPU_CONFIG']).read_text(encoding='utf-8-sig'))
OUTPUT = Path(CONFIG['output'])
OUTPUT.mkdir(parents=True, exist_ok=False)
RESULT = {'config': CONFIG, 'passed': False, 'cases': []}
RESTORE_CVARS = {}
RESTORE_PROPERTIES = []
RENDERER = None
RENDERER_ENABLED = True
ORIGINAL_VIEWPORT = None
ORIGINAL_POLICY = None
ORIGINAL_CAMERA = None
CREATED = None
CASE_RESTORES = []
CASE_DISABLED = []


def save():
    (OUTPUT / 'results.json').write_text(json.dumps(RESULT, indent=2) + '\n')


def wait(predicate, timeout=90):
    deadline = time.monotonic() + timeout
    while not predicate():
        if time.monotonic() >= deadline:
            raise TimeoutError('Capture or material did not complete')
        general.idle_wait_frames(1)


def cvar(name, value):
    if name not in RESTORE_CVARS:
        RESTORE_CVARS[name] = general.get_cvar(name)
    general.run_console(f'{name} {value}')
    return general.get_cvar(name)


def component(owner, name, level=False):
    types = [math.Uuid_CreateString(name)] if name.startswith('{') else editor.EditorComponentAPIBus(
        bus.Broadcast, 'FindComponentTypeIdsByEntityType', [name],
        entity.EntityType().Level if level else entity.EntityType().Game)
    assert len(types) == 1, name
    result = editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentOfType', owner, types[0])
    assert result.IsSuccess(), (name, result.GetError())
    return result.GetValue()


def set_property(comp, path, value):
    result = editor.EditorComponentAPIBus(bus.Broadcast, 'SetComponentProperty', comp, path, value)
    assert result.IsSuccess(), (path, result.GetError())


def property_path(comp, override):
    if 'path' in override:
        return override['path']
    matches = [p for p in editor.EditorComponentAPIBus(bus.Broadcast, 'BuildComponentPropertyList', comp)
               if p.endswith(override['path_suffix'])]
    assert len(matches) == 1, (override, matches)
    return matches[0]


def camera(view, offset=0):
    position = list(view['position'])
    position[0] += offset
    general.set_current_view_position(*(float(x) for x in position))
    general.set_current_view_rotation(*(float(x) for x in view['rotation']))


def settle(seconds):
    deadline = time.monotonic() + float(seconds)
    while time.monotonic() < deadline:
        general.idle_wait_frames(1)


def capture(kind, name, step=None):
    events = {
        'timestamp': ('CapturePassTimestamp', 'OnCaptureQueryTimestampFinished'),
        'statistics': ('CapturePassPipelineStatistics', 'OnCaptureQueryPipelineStatisticsFinished'),
        'metadata': ('CaptureBenchmarkMetadata', 'OnCaptureBenchmarkMetadataFinished'),
    }
    request, notification = events[kind]
    finished = {}
    handler = atom.ProfilingCaptureNotificationBusHandler()
    handler.connect()
    handler.add_callback(notification, lambda args: finished.update(success=bool(args[0]), info=str(args[1])))
    try:
        arguments = (name, str(OUTPUT / (name + '.json'))) if kind == 'metadata' else (str(OUTPUT / (name + '.json')),)
        assert atom.ProfilingCaptureRequestBus(bus.Broadcast, request, *arguments), request
        deadline = time.monotonic() + 90
        while not finished:
            assert time.monotonic() < deadline, request
            if step:
                step()
            general.idle_wait_frames(1)
        assert finished['success'], finished
    finally:
        handler.disconnect()


def screenshot(name):
    finished = {}
    outcome = atom.FrameCaptureRequestBus(bus.Broadcast, 'CaptureScreenshot', str(OUTPUT / (name + '.png')))
    assert outcome.IsSuccess(), outcome.GetError()
    handler = atom.FrameCaptureNotificationBusHandler()
    handler.connect(outcome.GetValue())
    handler.add_callback('OnFrameCaptureFinished', lambda args: finished.update(success=args[0] == atom.FrameCaptureResult_Success))
    try:
        wait(lambda: bool(finished))
        assert finished['success']
    finally:
        handler.disconnect()


def renderdoc_capture(name):
    """Use the installed RenderDoc app API after launch-time injection."""
    kernel = ctypes.WinDLL('kernel32')
    kernel.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
    kernel.GetModuleHandleW.restype = ctypes.c_void_p
    assert kernel.GetModuleHandleW('renderdoc.dll'), 'Launch Editor through renderdoccmd capture'
    dll = ctypes.CDLL('renderdoc.dll')
    api = ctypes.POINTER(ctypes.c_void_p)()
    dll.RENDERDOC_GetAPI.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))]
    assert dll.RENDERDOC_GetAPI(10600, ctypes.byref(api)) == 1
    # Stable fields in the installed renderdoc_app.h RENDERDOC_API_1_6_0.
    count = ctypes.CFUNCTYPE(ctypes.c_uint32)(api[13])
    set_path = ctypes.CFUNCTYPE(None, ctypes.c_char_p)(api[11])
    trigger = ctypes.CFUNCTYPE(None)(api[15])
    set_path(str(OUTPUT / name).encode('utf-8'))
    before = count()
    trigger()
    wait(lambda: count() > before, 180)
    return [str(path) for path in OUTPUT.glob(name + '*.rdc')]


def frame_metrics(name, step=None):
    """Use Atom's timestamp envelope, never the sum of nested pass durations."""
    folder = Path(paths.projectroot) / 'user'
    before = set(folder.glob('Performance_*.json'))
    for key, value in [('r_metricsDataLogType', 'all'), ('r_metricsWaitTimePerCaptureBatch', 0),
                       ('r_metricsFrameCountPerCaptureBatch', CONFIG.get('frame_samples', 180)),
                       ('r_metricsMeasureGpuTime', 'true'), ('r_metricsQuitUponCompletion', 'false')]:
        cvar(key, value)
    cvar('r_metricsNumberOfCaptureBatches', 1)
    # File enumeration / JSON parsing every frame can throttle the Editor and
    # alter GPU clocks. Let the engine collect the entire batch first.
    for _ in range(CONFIG.get('frame_samples', 180) + 10):
        if step:
            step()
        general.idle_wait_frames(1)
    deadline = time.monotonic() + 90
    files = set()
    while True:
        assert time.monotonic() < deadline, 'GPU frame metrics timeout'
        files = set(folder.glob('Performance_*.json')) - before
        if len(files) == 1:
            try:
                # The legacy console can retain a stale copy of the Atom CVar.
                # The collector closes the JSON array only on batch completion.
                raw = next(iter(files)).read_text()
                # This engine's reused JSON event logger leaves a leading comma
                # on later batches. Accept that exact defect; retain raw evidence.
                json.loads(raw.replace('[,', '[', 1) if raw.startswith('[,') else raw)
                break
            except (ValueError, OSError):
                pass
        if step:
            step()
        general.idle_wait_frames(1)
    assert len(files) == 1, str(files)
    destination = OUTPUT / (name + '-frame-metrics.json')
    shutil.copyfile(files.pop(), destination)
    # Trigger the callback with measurement disabled before individual pass captures.
    cvar('r_metricsMeasureGpuTime', 'false')
    cvar('r_metricsNumberOfCaptureBatches', 0)
    return destination.name


def settings(owner, name, level=False):
    comp = component(owner, name, level)
    result = {}
    for path in editor.EditorComponentAPIBus(bus.Broadcast, 'BuildComponentPropertyList', comp):
        value = editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentProperty', comp, path)
        if value.IsSuccess():
            item = value.GetValue()
            if isinstance(item, (str, int, float, bool, type(None))):
                result[path] = item
    return result


def main():
    global RENDERER, RENDERER_ENABLED, ORIGINAL_VIEWPORT, ORIGINAL_POLICY, ORIGINAL_CAMERA, CREATED
    general.idle_enable(True)
    cvar('ed_keepEditorActive', 1)
    cvar('ed_backgroundUpdatePeriod', -1)
    cvar('vsync_interval', 0)
    cvar('sys_MaxFPS', 0)
    level = Path(paths.projectroot) / 'Levels' / CONFIG.get('level', 'DefaultLevel')
    level = level / (level.name + '.prefab')
    RESULT['level_file'] = str(level)
    RESULT['level_sha256_before'] = hashlib.sha256(level.read_bytes()).hexdigest()
    assert general.open_level(str(level)), level
    settle(12)
    position, rotation = general.get_current_view_position(), general.get_current_view_rotation()
    ORIGINAL_CAMERA = ([position.x, position.y, position.z], [rotation.x, rotation.y, rotation.z])
    size = general.get_viewport_size()
    ORIGINAL_VIEWPORT = [int(size.x), int(size.y)]
    ORIGINAL_POLICY = general.get_viewport_expansion_policy()
    general.set_viewport_expansion_policy('FixedSize')
    general.set_viewport_size(*CONFIG.get('resolution', [1920, 1080]))
    general.update_viewport()
    general.idle_wait_frames(90)
    size = general.get_viewport_size()
    RESULT['viewport'] = [int(size.x), int(size.y)]
    assert RESULT['viewport'] == CONFIG.get('resolution', [1920, 1080]), RESULT['viewport']
    level_owner = editor.ToolsApplicationRequestBus(bus.Broadcast, 'GetCurrentLevelEntityId')
    # Removing the renderer feature processor breaks frame capture in this
    # engine build. Unregister the terrain areas while retaining the pipeline.
    # Require an explicit scene owner so extra terrain areas are not overlooked.
    RENDERER = component(general.find_editor_entity(CONFIG['terrain_entity']), 'Terrain Layer Spawner')
    assert editor.EditorComponentAPIBus(bus.Broadcast, 'IsComponentEnabled', RENDERER), 'Terrain region must initially be enabled'
    RESULT['settings'] = {name: settings(level_owner, name, True) for name in ('Terrain World', 'Terrain World Renderer')}
    RESULT['composition_owners'] = []
    composition_types = editor.EditorComponentAPIBus(bus.Broadcast, 'FindComponentTypeIdsByEntityType', ['Terrain Composition Gradient'], entity.EntityType().Game)
    for candidate in entity.SearchBus(bus.Broadcast, 'SearchEntities', entity.SearchFilter()):
        if editor.EditorComponentAPIBus(bus.Broadcast, 'HasComponentOfType', candidate, composition_types[0]):
            RESULT['composition_owners'].append({'name': editor.EditorEntityInfoRequestBus(bus.Event, 'GetName', candidate),
                'settings': settings(candidate, 'Terrain Composition Gradient')})
    for item in CONFIG.get('inspect_components', []):
        owner = general.find_editor_entity(item['entity'])
        RESULT['settings'][item['entity'] + '/' + item['component']] = settings(owner, item['component'])
    RESULT['cvars'] = {}
    for name in ('r_width', 'r_height', 'r_resolutionMode', 'r_antiAliasing', 'r_multiSampleCount', 'r_terrainEnableRayTracing', 'q_graphics'):
        try:
            RESULT['cvars'][name] = general.get_cvar(name)
        except Exception as exc:
            RESULT['cvars'][name] = str(exc)
    types = editor.EditorComponentAPIBus(bus.Broadcast, 'FindComponentTypeIdsByEntityType', ['Terrain Material'], entity.EntityType().Game)
    owners = [o for o in entity.SearchBus(bus.Broadcast, 'SearchEntities', entity.SearchFilter())
              if editor.EditorComponentAPIBus(bus.Broadcast, 'HasComponentOfType', o, types[0])]
    assert len(owners) <= 1, 'Multiple terrain material selectors'
    if owners:
        owner = owners[0]
        selector = component(owner, 'Terrain Material')
        original = editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentProperty', selector, 'Material')
        assert original.IsSuccess()
        RESTORE_PROPERTIES.append((selector, 'Material', original.GetValue()))
    else:
        owner = editor.ToolsApplicationRequestBus(bus.Broadcast, 'CreateNewEntity', entity.EntityId())
        CREATED = owner
        added = editor.EditorComponentAPIBus(bus.Broadcast, 'AddComponentsOfType', owner, types)
        assert added.IsSuccess()
        selector = added.GetValue()[0]
    RESULT['original_material_status'] = canvas.TerrainTintMaterialRequestBus(bus.Event, 'GetStatus', owner)
    for override in CONFIG.get('properties', []):
        target_owner = level_owner if override['entity'] == 'Level' else general.find_editor_entity(override['entity'])
        comp = component(target_owner, override['component'], override['entity'] == 'Level')
        previous = editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentProperty', comp, override['path'])
        assert previous.IsSuccess()
        RESTORE_PROPERTIES.append((comp, override['path'], previous.GetValue()))
        set_property(comp, override['path'], override['value'])
    capture('metadata', 'metadata')
    for case in CONFIG['cases']:
        name = case['name']
        print('TERRAIN_GPU_CASE', name, flush=True)
        for comp in CASE_DISABLED:
            assert editor.EditorComponentAPIBus(bus.Broadcast, 'EnableComponents', [comp])
        CASE_DISABLED.clear()
        for comp, path, value in reversed(CASE_RESTORES):
            set_property(comp, path, value)
        CASE_RESTORES.clear()
        for override in case.get('properties', []):
            target = level_owner if override['entity'] == 'Level' else general.find_editor_entity(override['entity'])
            comp = component(target, override['component'], override['entity'] == 'Level')
            path = property_path(comp, override)
            previous = editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentProperty', comp, path)
            assert previous.IsSuccess(), override
            CASE_RESTORES.append((comp, path, previous.GetValue()))
            set_property(comp, path, override['value'])
        disabled_items = list(case.get('disable_components', []))
        if case.get('disable_all_meshes'):
            mesh_types = editor.EditorComponentAPIBus(bus.Broadcast, 'FindComponentTypeIdsByEntityType', ['Mesh'], entity.EntityType().Game)
            assert len(mesh_types) == 1
            for candidate in entity.SearchBus(bus.Broadcast, 'SearchEntities', entity.SearchFilter()):
                if editor.EditorComponentAPIBus(bus.Broadcast, 'HasComponentOfType', candidate, mesh_types[0]):
                    disabled_items.append({'entity': editor.EditorEntityInfoRequestBus(bus.Event, 'GetName', candidate), 'component': 'Mesh'})
            case['resolved_disabled_meshes'] = disabled_items
        for item in disabled_items:
            comp = component(general.find_editor_entity(item['entity']), item['component'])
            if editor.EditorComponentAPIBus(bus.Broadcast, 'IsComponentEnabled', comp):
                CASE_DISABLED.append(comp)
                assert editor.EditorComponentAPIBus(bus.Broadcast, 'DisableComponents', [comp])
        if RENDERER_ENABLED != case.get('terrain', True):
            action = 'EnableComponents' if case.get('terrain', True) else 'DisableComponents'
            assert editor.EditorComponentAPIBus(bus.Broadcast, action, [RENDERER]), action
            RENDERER_ENABLED = case.get('terrain', True)
        path = case.get('material', '')
        material = asset.AssetCatalogRequestBus(bus.Broadcast, 'GetAssetIdByPath', path, math.Uuid(), False) if path else asset.AssetId()
        assert not path or material.is_valid(), path
        set_property(selector, 'Material', material)
        if case.get('terrain', True):
            expected = 'Graph terrain material active' if path else 'Default graph terrain material'
            wait(lambda: canvas.TerrainTintMaterialRequestBus(bus.Event, 'GetStatus', owner) == expected)
        pacing = cvar('sys_MaxFPS', case.get('cap', -1))
        camera(case)
        settle(CONFIG.get('settle_seconds', 5))
        entry = {'case': case, 'cap_readback': pacing, 'material_status': canvas.TerrainTintMaterialRequestBus(bus.Event, 'GetStatus', owner),
                 'measurement_start_utc_epoch': time.time()}
        RESULT['cases'].append(entry)
        screenshot(name)
        if CONFIG.get('renderdoc'):
            entry['renderdoc_captures'] = renderdoc_capture(name)
            save()
            continue
        # CPU wall intervals include Editor/Python work and are never labeled GPU time.
        frames = []
        start = previous = time.perf_counter()
        while time.perf_counter() - start < CONFIG.get('wall_seconds', 3):
            elapsed = time.perf_counter() - start
            if case.get('moving'):
                camera(case, elapsed * CONFIG.get('speed_mps', 16))
            general.idle_wait_frames(1)
            now = time.perf_counter()
            frames.append((now - previous) * 1000)
            previous = now
        entry['editor_wall_intervals_ms'] = frames
        camera(case)
        origin = time.perf_counter()
        step = (lambda: camera(case, (time.perf_counter() - origin) * CONFIG.get('speed_mps', 16))) if case.get('moving') else None
        entry['frame_metrics'] = frame_metrics(name, step)
        entry['timestamps'] = []
        for sample in range(CONFIG.get('samples', 9)):
            camera(case)
            origin = time.perf_counter()
            step = (lambda: camera(case, (time.perf_counter() - origin) * CONFIG.get('speed_mps', 16))) if case.get('moving') else None
            filename = f'{name}-timestamp-{sample:02d}'
            capture('timestamp', filename, step)
            entry['timestamps'].append(filename + '.json')
        camera(case)
        general.idle_wait_frames(60)
        capture('statistics', name + '-statistics')
        entry['statistics'] = name + '-statistics.json'
        save()
    RESULT['level_sha256_after'] = hashlib.sha256(level.read_bytes()).hexdigest()
    assert RESULT['level_sha256_after'] == RESULT['level_sha256_before'], 'Authored level changed'
    RESULT['passed'] = True


try:
    main()
except Exception:
    RESULT['error'] = traceback.format_exc()
    print(RESULT['error'], flush=True)
finally:
    try:
        for comp in CASE_DISABLED:
            editor.EditorComponentAPIBus(bus.Broadcast, 'EnableComponents', [comp])
        if RENDERER and not RENDERER_ENABLED:
            editor.EditorComponentAPIBus(bus.Broadcast, 'EnableComponents', [RENDERER])
        for comp, path, value in reversed(RESTORE_PROPERTIES + CASE_RESTORES):
            set_property(comp, path, value)
        if CREATED:
            editor.ToolsApplicationRequestBus(bus.Broadcast, 'DeleteEntityById', CREATED)
        if ORIGINAL_CAMERA:
            general.set_current_view_position(*ORIGINAL_CAMERA[0])
            general.set_current_view_rotation(*ORIGINAL_CAMERA[1])
        if ORIGINAL_VIEWPORT:
            general.set_viewport_size(*ORIGINAL_VIEWPORT)
            general.set_viewport_expansion_policy(ORIGINAL_POLICY)
        for name, value in RESTORE_CVARS.items():
            general.run_console(f'{name} {value}')
        RESULT['restored_cvars'] = {name: general.get_cvar(name) for name in RESTORE_CVARS}
    except Exception:
        RESULT['restore_error'] = traceback.format_exc()
        RESULT['passed'] = False
    save()
    print('TERRAIN_GPU_RESULT', str(OUTPUT), RESULT['passed'], flush=True)
    general.exit_no_prompt()
