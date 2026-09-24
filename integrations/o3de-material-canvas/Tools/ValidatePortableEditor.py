"""Capture reusable bump normals on an ordinary mesh in a dedicated Editor.

Compile Tests/Fixtures/Portable copies under Assets/CanvasFollowupValidation/portable
first. This opens the saved DefaultLevel, creates one temporary plane, captures
zero/procedural/analytic-reference materials, then deletes the plane without saving.
Compare the captures separately; successful capture alone is not pixel parity.
"""
import json
import math as scalar_math
from pathlib import Path
import time
import traceback
import azlmbr.asset as asset
import azlmbr.atom as atom
import azlmbr.bus as bus
import azlmbr.components as components
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as math
import azlmbr.paths as paths
import azlmbr.render as render

OUTPUT = Path(paths.projectroot) / 'user/CanvasPortableValidation'
OUTPUT.mkdir(parents=True, exist_ok=True)
RESULT = {'passed': False, 'captures': [], 'pixel_comparison': 'pending'}


def wait(predicate, timeout=90):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        general.idle_wait_frames(1)
    raise RuntimeError('Timed out waiting for portable material validation')


def asset_id(path):
    result = asset.AssetCatalogRequestBus(bus.Broadcast, 'GetAssetIdByPath', path, math.Uuid(), False)
    assert result.is_valid(), path
    return result


def capture(name):
    done = {}
    result = atom.FrameCaptureRequestBus(bus.Broadcast, 'CaptureScreenshot', str(OUTPUT / (name + '.png')))
    assert result.IsSuccess(), result.GetError()
    handler = atom.FrameCaptureNotificationBusHandler()
    handler.connect(result.GetValue())
    handler.add_callback('OnFrameCaptureFinished', lambda args: done.update(success=args[0] == atom.FrameCaptureResult_Success))
    try:
        wait(lambda: bool(done))
        assert done['success'], name
        RESULT['captures'].append(name)
    finally:
        handler.disconnect()


def main():
    general.idle_enable(True)
    assert general.open_level('DefaultLevel')
    general.idle_wait_frames(180)
    owner = editor.ToolsApplicationRequestBus(bus.Broadcast, 'CreateNewEntity', entity.EntityId())
    assert owner.IsValid()
    try:
        editor.EditorEntityAPIBus(bus.Event, 'SetName', owner, 'Temporary Canvas Normal Validation')
        types = editor.EditorComponentAPIBus(bus.Broadcast, 'FindComponentTypeIdsByEntityType', ['Mesh', 'Material'], entity.EntityType().Game)
        added = editor.EditorComponentAPIBus(bus.Broadcast, 'AddComponentsOfType', owner, types)
        assert added.IsSuccess(), added.GetError()
        mesh = editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentOfType', owner, types[0]).GetValue()
        properties = editor.EditorComponentAPIBus(bus.Broadcast, 'BuildComponentPropertyList', mesh)
        RESULT['mesh_properties'] = list(properties)
        model_path = next(p for p in properties if p.endswith('Model Asset'))
        model = asset_id('materialeditor/viewportmodels/plane_3x3.fbx.azmodel')
        changed = editor.EditorComponentAPIBus(bus.Broadcast, 'SetComponentProperty', mesh, model_path, model)
        assert changed.IsSuccess(), changed.GetError()
        components.TransformBus(bus.Event, 'SetWorldTranslation', owner, math.Vector3(0.0, 0.0, 100.0))
        components.TransformBus(bus.Event, 'SetLocalUniformScale', owner, 2.0)
        editor.ToolsApplicationRequestBus(bus.Broadcast, 'SetSelectedEntities', [])
        general.set_current_view_position(0.0, -7.0, 105.0)
        general.set_current_view_rotation(-35.0, 0.0, 0.0)
        for pose, angle, reference in [('flat', 0, 'normal_flat_reference'), ('tilted', 35, 'normal_tilt_reference')]:
            components.TransformBus(bus.Event, 'SetLocalRotation', owner, math.Vector3(scalar_math.radians(angle), 0.0, 0.0))
            general.idle_wait_frames(360)
            for label, material in [('zero', 'normal_zero'), ('procedural', 'normal_lit'), ('reference', reference)]:
                selected = asset_id('assets/canvasfollowupvalidation/portable/' + material + '.azmaterial')
                render.MaterialComponentRequestBus(bus.Event, 'SetMaterialAssetIdOnDefaultSlot', owner, selected)
                wait(lambda: render.MaterialComponentRequestBus(bus.Event, 'GetMaterialAssetIdOnDefaultSlot', owner) == selected)
                general.idle_wait_frames(360)
                capture(pose + '-' + label)
        RESULT['passed'] = True
    finally:
        editor.ToolsApplicationRequestBus(bus.Broadcast, 'DeleteEntityById', owner)


try:
    main()
except Exception:
    RESULT['error'] = traceback.format_exc()
finally:
    (OUTPUT / 'results.json').write_text(json.dumps(RESULT, indent=2) + '\n', encoding='utf-8')
    print('CANVAS_PORTABLE_VALIDATION ' + str(RESULT))
