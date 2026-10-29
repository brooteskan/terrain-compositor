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
from PySide6 import QtCore
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
    # Exercise real Qt modality/widgets without putting temporary test windows
    # over the user's working editor or accepting unrelated desktop input.
    dialog_init = surface.RefreshProgress.__init__
    def hidden_dialog_init(dialog):
        dialog_init(dialog)
        dialog.setAttribute(QtCore.Qt.WA_DontShowOnScreen,True)
    surface.RefreshProgress.__init__ = hidden_dialog_init
    cancel_refresh = surface.Refresh.cancel
    def traced_cancel(job):
        report.setdefault('cancelCalls',[]).append(traceback.format_stack())
        cancel_refresh(job)
    surface.Refresh.cancel = traced_cancel
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
    assert job is not None, editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event,'GetRefreshStatus',composition)
    assert job.dialog.isVisible() and job.dialog.isModal()
    assert job.dialog.windowModality()==QtCore.Qt.ApplicationModal
    assert job.dialog.button.text()=='Cancel'
    report['modalProgressAndCancelVisible'] = True
    job.dialog.close()
    assert bridge.job is job and job.dialog.active and job.dialog.isVisible()
    report['syntheticCloseDoesNotCancelWork'] = True
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
    assert bridge.dialog.isVisible() and not bridge.dialog.active
    assert bridge.dialog.stage.text()=='Collision surface refreshed'
    assert bridge.dialog.button.text()=='Close'
    bridge.dialog.button.click()
    assert not bridge.dialog.isVisible()
    report['explicitCompletionShown'] = True
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
    # Cancel an actual background worker, with a pending publication rollback.
    components.TransformBus(bus.Event,'SetWorldTranslation',terrain,math.Vector3(moved.x+.5,moved.y,moved.z))
    editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event,'RefreshCollisionSurface',composition)
    cancelled = bridge.job
    assert cancelled is not None and cancelled.process is not None
    cancelled.backup = surface.capture_bindings()
    assert editor.EditorComponentAPIBus(bus.Broadcast,'SetComponentProperty',stamp,'Configuration|Height Scale',1.0).IsSuccess()
    cancelled.dialog.button.click()
    assert bridge.job is None and cancelled.phase=='cancelled'
    assert cancelled.dialog.stage.text()=='Refresh cancelled' and cancelled.dialog.button.text()=='Close'
    assert not editor.TerrainCollisionSurfaceEditorRequestBus(bus.Event,'IsRefreshInProgress',composition)
    assert editor.EditorComponentAPIBus(bus.Broadcast,'CompareComponentProperty',stamp,'Configuration|Heightmap Asset',before_asset)
    restored = editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentProperty',stamp,'Configuration|Height Scale').GetValue()
    assert abs(previous-restored)<0.0001
    deadline=time.monotonic()+5
    while cancelled.process.poll() is None and time.monotonic()<deadline:general.idle_wait_frames(1)
    assert cancelled.process.poll() is not None
    report['cancelStopsWorkerAndRestoresPriorSurface'] = True
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
    assert bridge.dialog.isVisible() and bridge.dialog.stage.text()=='Collision refresh failed'
    report['failureDialogShown'] = True
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
    if 'bridge' in globals() and bridge.dialog is not None:
        bridge.dialog.accept()
        bridge.dialog.deleteLater()
    general.run_console('ed_backgroundUpdatePeriod '+str(old_background))
    (Path(paths.log).parent/'CollisionRefreshButtonVerification.json').write_text(json.dumps(report,indent=2)+'\n')
    general.exit_no_prompt()
