"""Exercise the inspector's exact native callback with unsaved Vue placement.

Uses an isolated Editor, never saves the level, and leaves source terrain edits
temporary. Reports beside the isolated log directory; no visual inspection.
"""
import hashlib
import json
import os
from pathlib import Path
import time
import traceback
import azlmbr.bus as bus
import azlmbr.components as components
import azlmbr.editor as editor
import azlmbr.legacy.general as general
import azlmbr.math as math
import azlmbr.paths as paths

report = {}
old_background = general.get_cvar('ed_backgroundUpdatePeriod')
try:
    general.run_console('ed_backgroundUpdatePeriod -1'); general.idle_enable(True)
    assert general.open_level('VueLiveLink'); general.idle_wait_frames(20)
    import collision_surface_editor as surface
    bridge = surface.start()
    assert bridge is not None
    project = Path(paths.projectroot)
    level = project/'Levels/VueLiveLink/VueLiveLink.prefab'
    original_hash = hashlib.sha256(level.read_bytes()).hexdigest()
    composition = general.find_editor_entity('Terrain Compositor')
    player = general.find_editor_entity('Falconry First Person')
    player_before = surface.vector(components.TransformBus(bus.Event,'GetWorldTranslation',player))
    terrain = general.find_editor_entity('Terrain7')
    position = components.TransformBus(bus.Event,'GetWorldTranslation',terrain)
    moved = math.Vector3(position.x+13.0,position.y+7.0,position.z+3.0)
    components.TransformBus(bus.Event,'SetWorldTranslation',terrain,moved)
    expected = surface.snapshot(composition)
    expected_row = next(row for row in expected['sources'] if row['name']=='Terrain7')
    assert all(abs(a-b)<0.0001 for a,b in zip(expected_row['translation'],surface.vector(moved)))
    editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event,'RefreshCollisionSurface',composition)
    job = bridge.job
    assert job is not None
    report['workerFolder'] = str(job.folder)
    editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event,'RefreshCollisionSurface',composition)
    assert bridge.job is job, 'Duplicate request started another worker'
    report['duplicateRequestIgnored'] = True
    statuses = []
    deadline = time.monotonic()+300
    while bridge.job is not None and time.monotonic()<deadline:
        general.idle_wait_frames(1)
        text = editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event,'GetRefreshStatus',composition)
        if not statuses or statuses[-1]!=text: statuses.append(text)
    report['statuses'] = statuses
    assert bridge.job is None, 'Refresh did not finish'
    status = editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event,'GetRefreshStatus',composition)
    assert status.startswith('Collision surface refreshed'), status
    assert not editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event,'IsRefreshInProgress',composition)
    info = json.loads(job.info_path.read_text())
    actual_row = next(row for row in info['sources'] if row['name']=='Terrain7')
    assert actual_row['translation']==expected_row['translation'], 'Worker used the saved transform'
    report['liveUnsavedTransformUsed'] = True
    # Reapply a captured binding after a deliberately incorrect height scale.
    backup = surface.capture_bindings()
    stamp = surface.component(general.find_editor_entity('Vue Terrain Walkable Surface'),'Heightmap Stamp')
    previous = editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentProperty',stamp,'Configuration|Height Scale').GetValue()
    changed = editor.EditorComponentAPIBus(bus.Broadcast,'SetComponentProperty',stamp,'Configuration|Height Scale',1.0)
    assert changed.IsSuccess()
    surface.restore_bindings(backup)
    restored = editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentProperty',stamp,'Configuration|Height Scale').GetValue()
    assert abs(previous-restored)<0.0001
    report['bindingRollbackPassed'] = True
    before_asset = editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentProperty',stamp,'Configuration|Heightmap Asset').GetValue()
    # A changed source prevents the cache shortcut; invalid worker selection
    # must leave the last successful surface bound and clear the busy state.
    components.TransformBus(bus.Event,'SetWorldTranslation',terrain,math.Vector3(moved.x+1.0,moved.y,moved.z))
    old_python = os.environ.get('TG_VUE_LINK_PYTHON')
    os.environ['TG_VUE_LINK_PYTHON'] = str(project/'user/missing-collision-worker.exe')
    try: editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event,'RefreshCollisionSurface',composition)
    finally:
        if old_python is None: os.environ.pop('TG_VUE_LINK_PYTHON',None)
        else: os.environ['TG_VUE_LINK_PYTHON'] = old_python
    status = editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event,'GetRefreshStatus',composition)
    assert 'does not exist' in status, status
    assert bridge.job is None and not editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event,'IsRefreshInProgress',composition)
    assert editor.EditorComponentAPIBus(bus.Broadcast,'CompareComponentProperty',stamp,'Configuration|Heightmap Asset',before_asset)
    report['failedRefreshKeptPriorSurface'] = True
    assert surface.vector(components.TransformBus(bus.Event,'GetWorldTranslation',player))==player_before
    report['playerUnchanged'] = True
    assert hashlib.sha256(level.read_bytes()).hexdigest()==original_hash
    report['levelNotSaved'] = True
    report['passed'] = True
except Exception:
    report['error'] = traceback.format_exc()
finally:
    general.run_console('ed_backgroundUpdatePeriod '+str(old_background))
    (Path(paths.log).parent/'CollisionRefreshButtonVerification.json').write_text(json.dumps(report,indent=2)+'\n')
    general.exit_no_prompt()
