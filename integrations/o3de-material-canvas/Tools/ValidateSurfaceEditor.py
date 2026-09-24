"""Run inside Editor: capture terrain surfaces in direct/clipmap modes without saving.

Run on the saved TG DefaultLevel with no unsaved work. Restores edited properties
and the original graph, and deletes a selector only if the script created it.
Results are under project/user; TC_CANVAS_SURFACE_PARITY_ONLY=1 runs static comparisons.
"""
import json
import os
import sqlite3
import subprocess
import time
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

OUTPUT = Path(paths.projectroot) / 'user/TerrainSurfaceValidation'
PARITY_ONLY = os.environ.get('TC_CANVAS_SURFACE_PARITY_ONLY') == '1'
if PARITY_ONLY:
    OUTPUT /= 'parity'
OUTPUT.mkdir(parents=True, exist_ok=True)
RESULT = {'passed': False, 'selections': [], 'captures': [], 'gpu': []}
(OUTPUT / 'results.json').write_text(json.dumps({'state': 'running'}), encoding='utf-8')


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


def graph_reload(owner, selector):
    """Change a new example, require a fresh AP shader job, and restore it."""
    gem = Path(__file__).resolve().parents[1]
    graph = gem / 'Assets/MaterialCanvas/Terrain/Examples/wet_terrain.materialgraph'
    original = graph.read_bytes()
    material = asset.AssetCatalogRequestBus(bus.Broadcast, 'GetAssetIdByPath',
        'materialcanvas/terrain/examples/wet_terrain.azmaterial', math.Uuid(), False)
    change(selector, 'Material', material)
    wait(lambda: get_status(owner) == 'Canvas terrain surface active')
    general.idle_wait_frames(180)
    capture('reload-before')

    def shader_job():
        with sqlite3.connect('file:' + str(Path(paths.projectroot) / 'Cache/assetdb.sqlite') + '?mode=ro', uri=True) as database:
            return database.execute("SELECT j.JobRunKey,j.Status,j.ErrorCount FROM Jobs j JOIN Sources s ON j.SourcePK=s.SourceID "
                "WHERE s.SourceName='MaterialCanvas/Terrain/Examples/wet_terrain_Forward.shader' AND j.JobKey='Shader Asset'").fetchone()

    def compile_graph(previous):
        report = gem / 'compile-results.json'
        env = dict(os.environ, TC_CANVAS_GRAPH=graph.name, TC_CANVAS_GRAPH_ROOT=str(graph.parent))
        process = subprocess.Popen([str(Path(paths.executableFolder) / 'MaterialCanvas.exe'),
            '--project-path=' + str(paths.projectroot), '--rhi=null', '--allowMultipleInstances', '--autotest_mode',
            '--timeout=180000', '--regset=/O3DE/TerrainCanvas/SourceGenerationOnly=true',
            '--regset=/O3DE/AtomToolsFramework/AtomToolsDocumentSystem/DisplayErrorMessageDialogs=false',
            '--runpython=' + str(Path(__file__).with_name('CompileExamples.py'))],
            env=env, creationflags=subprocess.CREATE_NO_WINDOW)
        started = time.time_ns()
        try:
            wait(lambda: process.poll() is not None, 180)
            RESULT.setdefault('reload_compiler_exits', []).append(process.returncode)
            assert report.exists() and report.stat().st_mtime_ns > started
            generated = json.loads(report.read_text())
            assert len(generated) == 1 and Path(generated[0]['graph']) == graph
            wait(lambda: (lambda job: job and job[0] > previous and job[1:] == (4, 0))(shader_job()), 180)
            general.idle_wait_frames(180)
            assert get_status(owner) == 'Canvas terrain surface active'
        finally:
            if process.poll() is None:
                process.terminate()

    try:
        previous = shader_job()[0]
        edited = json.loads(original)
        node = next(n['Value'] for n in edited['ClassData']['m_nodes'] if n['Key'] == 9)
        for slot in node['m_inputDataSlots']:
            if slot['Key']['m_name'] in ('a', 'b'):
                slot['Value']['m_value']['Value'] = [0.05, 0.2, 0.8]
        graph.write_text(json.dumps(edited, indent=4) + '\n', encoding='utf-8')
        compile_graph(previous)
        capture('reload-blue')
        RESULT['graph_reload'] = get_status(owner)
    finally:
        previous = shader_job()[0]
        graph.write_bytes(original)
        compile_graph(previous)
        capture('reload-restored')


def main():
    general.idle_enable(True)
    assert general.open_level('DefaultLevel')
    general.idle_wait_frames(180)
    restores = []
    owner = entity.EntityId()
    created_owner = False
    try:
        ground = component(general.find_editor_entity('Procedural Ground Source'), 'Procedural Ground Gradient')
        properties = editor.EditorComponentAPIBus(bus.Broadcast, 'BuildComponentPropertyList', ground)
        strength = next(p for p in properties if p.endswith('Noise Tint Strength'))
        restores.append((ground, strength, editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentProperty', ground, strength).GetValue()))
        change(ground, strength, 0.0)
        renderer_types = editor.EditorComponentAPIBus(bus.Broadcast, 'FindComponentTypeIdsByEntityType',
            ['Terrain World Renderer'], entity.EntityType().Level)
        renderer_result = editor.EditorLevelComponentAPIBus(bus.Broadcast, 'GetComponentOfType', renderer_types[0])
        assert renderer_result.IsSuccess(), 'Terrain World Renderer'
        renderer = renderer_result.GetValue()
        properties = list(editor.EditorComponentAPIBus(bus.Broadcast, 'BuildComponentPropertyList', renderer))
        RESULT['renderer_properties'] = properties
        clipmap_path = next(p for p in properties if p.endswith('|Clipmap Enabled') or p.endswith('ClipmapEnabled'))
        restores.append((renderer, clipmap_path, editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentProperty', renderer, clipmap_path).GetValue()))
        types = editor.EditorComponentAPIBus(bus.Broadcast, 'FindComponentTypeIdsByEntityType', ['Terrain Material'], entity.EntityType().Game)
        existing = []
        for candidate in entity.SearchBus(bus.Broadcast, 'SearchEntities', entity.SearchFilter()):
            if editor.EditorComponentAPIBus(bus.Broadcast, 'HasComponentOfType', candidate, types[0]):
                existing.append(candidate)
        assert len(existing) <= 1, 'Run validation in a scene with at most one terrain selector'
        if existing:
            owner = existing[0]
            selector = component(owner, 'Terrain Material')
            restores.append((selector, 'Material', editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentProperty', selector, 'Material').GetValue()))
        else:
            owner = editor.ToolsApplicationRequestBus(bus.Broadcast, 'CreateNewEntity', entity.EntityId())
            created_owner = True
            editor.EditorEntityAPIBus(bus.Event, 'SetName', owner, 'Terrain Surface Validation')
            added = editor.EditorComponentAPIBus(bus.Broadcast, 'AddComponentsOfType', owner, types)
            assert added.IsSuccess()
            selector = added.GetValue()[0]
        owner_name = editor.EditorEntityInfoRequestBus(bus.Event, 'GetName', owner)
        editor.ToolsApplicationRequestBus(bus.Broadcast, 'SetSelectedEntities', [])
        if PARITY_ONLY:
            passthrough = asset.AssetCatalogRequestBus(bus.Broadcast, 'GetAssetIdByPath',
                'materialcanvas/terrain/examples/surface_passthrough.azmaterial', math.Uuid(), False)
            for clipmaps in (False, True):
                change(renderer, clipmap_path, clipmaps)
                mode = 'clipmap' if clipmaps else 'direct'
                for distance, position in [('near', (16.0, 16.0, 32.0)), ('far', (16.0, -180.0, 180.0))]:
                    general.set_current_view_position(*position)
                    general.set_current_view_rotation(-35.0, 0.0, 0.0)
                    general.idle_wait_frames(600)
                    for name, selected, expected in [('baseline', asset.AssetId(), 'Legacy terrain tint'),
                            ('passthrough', passthrough, 'Canvas terrain surface active'),
                            ('baseline-restored', asset.AssetId(), 'Legacy terrain tint')]:
                        change(selector, 'Material', selected)
                        wait(lambda: get_status(owner) == expected)
                        general.idle_wait_frames(360)
                        capture(f'{mode}-{distance}-{name}')
            RESULT['passed'] = True
            return
        for clipmaps in (False, True):
            change(renderer, clipmap_path, clipmaps)
            mode = 'clipmap' if clipmaps else 'direct'
            for name in ('baseline', 'surface_passthrough', 'wet_terrain', 'procedural_ground', 'slope_elevation', 'procedural_normal'):
                if name == 'baseline':
                    material = asset.AssetId()
                    change(selector, 'Material', material)
                    wait(lambda: get_status(owner) == 'Legacy terrain tint')
                else:
                    material = asset.AssetCatalogRequestBus(bus.Broadcast, 'GetAssetIdByPath', 'materialcanvas/terrain/examples/' + name + '.azmaterial', math.Uuid(), False)
                    assert material.is_valid(), name
                    change(selector, 'Material', material)
                    wait(lambda: get_status(owner) == 'Canvas terrain surface active')
                RESULT['selections'].append({'mode': mode, 'graph': name, 'status': get_status(owner)})
                for distance, position in [('near', (16.0, 16.0, 32.0)), ('far', (16.0, -180.0, 180.0))]:
                    general.set_current_view_position(*position)
                    general.set_current_view_rotation(-35.0, 0.0, 0.0)
                    general.idle_wait_frames(180)
                    capture(mode + '-' + name + '-' + distance)
                for sample in range(3):
                    gpu_capture(f'{mode}-{name}-{sample}')
            invalid = asset.AssetCatalogRequestBus(bus.Broadcast, 'GetAssetIdByPath', 'materials/terrain/defaultpbrterrain.azmaterial', math.Uuid(), False)
            change(selector, 'Material', invalid)
            wait(lambda: 'Incompatible tint material' in get_status(owner))
            RESULT[mode + '_invalid_candidate'] = get_status(owner)
            capture(mode + '-invalid-retains-surface')
            change(selector, 'Material', material)
            wait(lambda: get_status(owner) == 'Canvas terrain surface active')
        graph_reload(owner, selector)
        general.enter_game_mode()
        runtime = {}
        def find_runtime():
            game_owner = general.find_game_entity(owner_name)
            if game_owner.IsValid():
                runtime['status'] = get_status(game_owner)
                return runtime['status'] == 'Canvas terrain surface active'
            return False
        wait(find_runtime)
        RESULT['runtime'] = runtime
        general.exit_game_mode()
        general.idle_wait_frames(60)
        RESULT['passed'] = True
    finally:
        if general.is_in_game_mode():
            general.exit_game_mode()
            general.idle_wait_frames(60)
        if created_owner and owner.IsValid():
            editor.ToolsApplicationRequestBus(bus.Broadcast, 'DeleteEntityById', owner)
        for comp, path, value in reversed(restores):
            change(comp, path, value)


try:
    main()
except Exception:
    RESULT['error'] = traceback.format_exc()
finally:
    (OUTPUT / 'results.json').write_text(json.dumps(RESULT, indent=2) + '\n', encoding='utf-8')
    print('TERRAIN_SURFACE_VALIDATION ' + str(RESULT))
