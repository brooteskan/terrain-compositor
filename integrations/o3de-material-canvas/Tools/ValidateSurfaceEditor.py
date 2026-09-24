"""Run in a dedicated Editor process against saved DefaultLevel; never save the level.

Exercises graph layout selection, rapid switching, cleared selection, near/far
rendering, pass timings and game mode. Native generation, AP processing and a
standalone packaged launch must be recorded separately. A screenshot is evidence
for inspection, not an automatic visual correctness claim.
"""
import json
import os
import time
import sqlite3
import traceback
from pathlib import Path
import azlmbr.asset as asset
import azlmbr.atom as atom
import azlmbr.bus as bus
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as math
import azlmbr.paths as paths
import azlmbr.terrain_canvas as canvas
OUTPUT = Path(os.environ.get('TC_CANVAS_REPORT_ROOT', str(Path(paths.projectroot) / 'user/TerrainGraphValidation')))
LEVEL = os.environ.get('TC_CANVAS_LEVEL', 'DefaultLevel')
OUTPUT.mkdir(parents=True, exist_ok=True)
RESULT = {'passed': False, 'selections': [], 'captures': [], 'gpu': [],
          'standalone_runtime': 'not tested by this Editor script'}
def wait(predicate, seconds=60):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        general.idle_wait_frames(1)
    raise RuntimeError('Timed out waiting for terrain surface')


def get_status(owner):
    return canvas.TerrainTintMaterialRequestBus(bus.Event, 'GetStatus', owner)


def capture(name):
    done = {}
    capture = atom.FrameCaptureRequestBus(bus.Broadcast, 'CaptureScreenshot', str(OUTPUT / (name + '.png')))
    assert capture.IsSuccess(), capture.GetError()
    handler = atom.FrameCaptureNotificationBusHandler()
    handler.connect(capture.GetValue())
    handler.add_callback('OnFrameCaptureFinished', lambda args: done.update(success=args[0] == atom.FrameCaptureResult_Success))
    try:
        wait(lambda: bool(done))
        assert done['success']
        RESULT['captures'].append(name)
    finally:
        handler.disconnect()


def gpu_capture(name):
    done = {}
    handler = atom.ProfilingCaptureNotificationBusHandler()
    handler.connect()
    handler.add_callback('OnCaptureQueryTimestampFinished', lambda args: done.update(success=bool(args[0])))
    try:
        accepted = atom.ProfilingCaptureRequestBus(bus.Broadcast, 'CapturePassTimestamp', str(OUTPUT / (name + '-gpu.json')))
        if accepted:
            wait(lambda: bool(done))
        RESULT['gpu'].append({'name': name, 'accepted': bool(accepted), **done})
    finally:
        handler.disconnect()


def component(owner, name):
    types = editor.EditorComponentAPIBus(bus.Broadcast, 'FindComponentTypeIdsByEntityType', [name], entity.EntityType().Game)
    assert types, name
    result = editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentOfType', owner, types[0])
    assert result.IsSuccess(), name
    return result.GetValue()


def change(component, path, value):
    result = editor.EditorComponentAPIBus(bus.Broadcast, 'SetComponentProperty', component, path, value)
    assert result.IsSuccess(), f'{path}: {result.GetError()}'



def reload_material(owner):
    material = Path(__file__).resolve().parents[1] / 'Assets/MaterialCanvas/Terrain/Examples/textured_terrain.material'
    original = material.read_bytes()
    def job():
        with sqlite3.connect('file:' + str(Path(paths.projectroot) / 'Cache/assetdb.sqlite') + '?mode=ro', uri=True) as database:
            return database.execute(
                "SELECT j.LastLogTime,j.Status FROM Jobs j JOIN Sources s ON j.SourcePK=s.SourceID "
                "WHERE lower(s.SourceName) LIKE '%/textured_terrain.material' "
                "ORDER BY j.LastLogTime DESC LIMIT 1").fetchone()
    def publish(text, name, expect_failure=False):
        if material.read_text(encoding='utf-8') == text:
            return
        before = job()
        material.write_text(text, encoding='utf-8')
        wait(lambda: job() is not None and job() != before, 120)
        current = job()
        assert (current[1] != 4) if expect_failure else (current[1] == 4), current
        general.idle_wait_frames(180)
        RESULT.setdefault('reloads', []).append({'name': name, 'job': current, 'status': get_status(owner)})
        capture('reload-' + name)
    try:
        for name, values in [
            ('texture-a', {'surface.blend': 0.0, 'surface.color': [1.0, .8, .6, 1.0]}),
            ('texture-b', {'surface.blend': 1.0}),
            ('missing-image', {'surface.texture_a': '', 'surface.blend': 0.0}),
            ('incompatible-contract', {'terrain.contractVersion': 999})]:
            document = json.loads(original)
            document['propertyValues'].update(values)
            publish(json.dumps(document, indent=4), name)
            if name == 'incompatible-contract':
                assert 'Incompatible' in get_status(owner) or 'default' in get_status(owner).lower()
            else:
                assert get_status(owner) == 'Graph terrain material active'
        publish(original.decode('utf-8'), 'restored')
        wait(lambda: get_status(owner) == 'Graph terrain material active')
    finally:
        publish(original.decode('utf-8'), 'final-restore')
        wait(lambda: get_status(owner) == 'Graph terrain material active')


def reload_shader(owner):
    source = Path(__file__).resolve().parents[1] / 'Assets/MaterialCanvas/Terrain/Examples/textured_terrain_Tint.azsli'
    original = source.read_bytes()
    def job():
        with sqlite3.connect('file:' + str(Path(paths.projectroot) / 'Cache/assetdb.sqlite') + '?mode=ro', uri=True) as database:
            return database.execute(
                "SELECT j.LastLogTime,j.Status FROM Jobs j JOIN Sources s ON j.SourcePK=s.SourceID "
                "WHERE lower(s.SourceName) LIKE '%/textured_terrain_forward.shader' "
                "ORDER BY j.LastLogTime DESC LIMIT 1").fetchone()
    def publish(text, name, failure=False):
        before = job()
        source.write_text(text, encoding='utf-8')
        wait(lambda: job() is not None and job() != before, 180)
        current = job()
        assert (current[1] != 4) if failure else (current[1] == 4), current
        general.idle_wait_frames(180)
        RESULT.setdefault('shader_reloads', []).append({'name': name, 'job': current, 'status': get_status(owner)})
        assert 'active' in get_status(owner).lower() or 'retaining' in get_status(owner).lower(), get_status(owner)
        capture(name)
    try:
        publish(original.decode('utf-8').replace('result.baseColor = inBaseColor;', 'result.baseColor = inBaseColor * 0.5;'), 'formula-edit')
        publish(original.decode('utf-8') + '\n#error Intentional terrain validation compile failure\n', 'failed-shader-compile', True)
    finally:
        publish(original.decode('utf-8'), 'shader-restored')


def main():
    general.idle_enable(True)
    general.run_console('ed_keepEditorActive 1')
    (OUTPUT / 'results.json').unlink(missing_ok=True)
    assert general.open_level(LEVEL)
    general.idle_wait_frames(180)
    restores = []
    owner = entity.EntityId()
    created = False
    try:
        renderer_types = editor.EditorComponentAPIBus(bus.Broadcast, 'FindComponentTypeIdsByEntityType',
            ['Terrain World Renderer'], entity.EntityType().Level)
        renderer = editor.EditorLevelComponentAPIBus(bus.Broadcast, 'GetComponentOfType', renderer_types[0]).GetValue()
        properties = list(editor.EditorComponentAPIBus(bus.Broadcast, 'BuildComponentPropertyList', renderer))
        assert not any('clipmap' in p.lower() or 'detail material' in p.lower() for p in properties), properties
        RESULT['renderer_properties'] = properties
        types = editor.EditorComponentAPIBus(bus.Broadcast, 'FindComponentTypeIdsByEntityType', ['Terrain Material'], entity.EntityType().Game)
        existing = [candidate for candidate in entity.SearchBus(bus.Broadcast, 'SearchEntities', entity.SearchFilter())
                    if editor.EditorComponentAPIBus(bus.Broadcast, 'HasComponentOfType', candidate, types[0])]
        assert len(existing) <= 1, 'Validation requires at most one scene material owner'
        if existing:
            owner = existing[0]
            selector = component(owner, 'Terrain Material')
            restores.append((selector, 'Material', editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentProperty', selector, 'Material').GetValue()))
        else:
            owner = editor.ToolsApplicationRequestBus(bus.Broadcast, 'CreateNewEntity', entity.EntityId())
            created = True
            editor.EditorEntityAPIBus(bus.Event, 'SetName', owner, 'Terrain Graph Validation')
            added = editor.EditorComponentAPIBus(bus.Broadcast, 'AddComponentsOfType', owner, types)
            assert added.IsSuccess()
            selector = added.GetValue()[0]
        owner_name = editor.EditorEntityInfoRequestBus(bus.Event, 'GetName', owner)
        materials = {}
        for name in ('default_terrain', 'procedural_terrain', 'textured_terrain'):
            materials[name] = asset.AssetCatalogRequestBus(bus.Broadcast, 'GetAssetIdByPath',
                'materialcanvas/terrain/examples/' + name + '.azmaterial', math.Uuid(), False)
            assert materials[name].is_valid(), name
            change(selector, 'Material', materials[name])
            wait(lambda: get_status(owner) == 'Graph terrain material active')
            RESULT['selections'].append({'name': name, 'status': get_status(owner)})
            for distance, position in json.loads(os.environ.get('TC_CANVAS_CAMERAS', '[ ["near", [16,16,32]], ["far", [16,-180,180]] ]')):
                general.set_current_view_position(*(float(value) for value in position))
                general.set_current_view_rotation(-35.0, 0.0, 0.0)
                general.idle_wait_frames(180)
                capture(name + '-' + distance)
            for sample in range(3): gpu_capture(f'{name}-{sample}')
        for _ in range(4):
            for material in materials.values(): change(selector, 'Material', material)
        wait(lambda: get_status(owner) == 'Graph terrain material active')
        change(selector, 'Material', asset.AssetId())
        wait(lambda: get_status(owner) == 'Default graph terrain material')
        capture('cleared-selection')
        change(selector, 'Material', materials['textured_terrain'])
        wait(lambda: get_status(owner) == 'Graph terrain material active')
        reload_material(owner)
        reload_shader(owner)
        general.enter_game_mode()
        def game_material_active():
            game_owner = general.find_game_entity(owner_name)
            return game_owner.IsValid() and get_status(game_owner) == 'Graph terrain material active'
        wait(game_material_active)
        RESULT['game_mode'] = 'passed'
        general.exit_game_mode()
        general.idle_wait_frames(60)
        RESULT['passed'] = True
    finally:
        if general.is_in_game_mode(): general.exit_game_mode(); general.idle_wait_frames(60)
        if created and owner.IsValid(): editor.ToolsApplicationRequestBus(bus.Broadcast, 'DeleteEntityById', owner)
        for comp, path, value in reversed(restores): change(comp, path, value)

try:
    main()
except Exception:
    RESULT['error'] = traceback.format_exc()
finally:
    (OUTPUT / 'results.json').write_text(json.dumps(RESULT, indent=2) + '\n', encoding='utf-8')
    print('TERRAIN_GRAPH_VALIDATION ' + str(RESULT))
    if os.environ.get('TC_CANVAS_EXIT') == '1': general.exit_no_prompt()
