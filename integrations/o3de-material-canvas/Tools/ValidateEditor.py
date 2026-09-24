"""Run in Editor against a saved terrain level; makes no saved level changes.

The optional first argument is a level name (default DefaultLevel). Captures and
the pass/fail report go to project/user/TerrainCanvasValidation.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
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

OUTPUT = Path(paths.projectroot) / "user/TerrainCanvasValidation"
OUTPUT.mkdir(parents=True, exist_ok=True)
RESULTS = {}


def wait_for(predicate, seconds=60):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        value = predicate()
        if value:
            return value
        general.idle_wait_frames(1)
    raise RuntimeError("Timed out waiting for terrain tint state")


def status(owner):
    return canvas.TerrainTintMaterialRequestBus(bus.Event, "GetStatus", owner)


def screenshot(name):
    general.set_current_view_position(16.0, 16.0, 32.0)
    general.set_current_view_rotation(-35.0, 0.0, 0.0)
    general.idle_wait_frames(120)
    result = atom.FrameCaptureRequestBus(bus.Broadcast, "CaptureScreenshot", str(OUTPUT / (name + ".png")))
    assert result.IsSuccess(), result.GetError()
    done = {}
    handler = atom.FrameCaptureNotificationBusHandler()
    handler.connect(result.GetValue())
    handler.add_callback("OnFrameCaptureFinished", lambda args: done.update(success=args[0] == atom.FrameCaptureResult_Success))
    try:
        wait_for(lambda: done)
        assert done["success"]
    finally:
        handler.disconnect()


def gpu_capture(name):
    done = {}
    handler = atom.ProfilingCaptureNotificationBusHandler()
    handler.connect()
    handler.add_callback("OnCaptureQueryTimestampFinished", lambda args: done.update(success=bool(args[0]), info=str(args[1])))
    try:
        accepted = atom.ProfilingCaptureRequestBus(bus.Broadcast, "CapturePassTimestamp", str(OUTPUT / (name + "-gpu.json")))
        if accepted:
            wait_for(lambda: done)
        RESULTS[name + "_gpu_capture"] = {"accepted": bool(accepted), **done}
    finally:
        handler.disconnect()


def main():
    general.idle_enable(True)
    level = sys.argv[1] if len(sys.argv) > 1 else "DefaultLevel"
    assert general.open_level(level)
    general.idle_wait_frames(180)
    # Match the example's settings rather than assuming the level uses defaults.
    ground = general.find_editor_entity("Procedural Ground Source")
    assert ground.IsValid(), "Expected the saved demonstration's Procedural Ground Source"
    ground_types = editor.EditorComponentAPIBus(bus.Broadcast, "FindComponentTypeIdsByEntityType",
        ["Procedural Ground Gradient"], entity.EntityType().Game)
    ground_result = editor.EditorComponentAPIBus(bus.Broadcast, "GetComponentOfType", ground, ground_types[0])
    assert ground_result.IsSuccess()
    ground_component = ground_result.GetValue()
    ground_properties = editor.EditorComponentAPIBus(bus.Broadcast, "BuildComponentPropertyList", ground_component)
    strength_path = next(p for p in ground_properties if p.endswith("Noise Tint Strength"))
    previous_strength = editor.EditorComponentAPIBus(bus.Broadcast, "GetComponentProperty", ground_component, strength_path).GetValue()
    assert editor.EditorComponentAPIBus(bus.Broadcast, "SetComponentProperty", ground_component, strength_path, 0.75).IsSuccess()
    RESULTS["matched_strength"] = 0.75
    general.set_current_view_position(16.0, 16.0, 32.0)
    general.set_current_view_rotation(-35.0, 0.0, 0.0)
    general.idle_wait_frames(120)
    screenshot("legacy")
    gpu_capture("legacy")
    owner = editor.ToolsApplicationRequestBus(bus.Broadcast, "CreateNewEntity", entity.EntityId())
    editor.EditorEntityAPIBus(bus.Event, "SetName", owner, "Terrain Canvas Validation")
    types = editor.EditorComponentAPIBus(bus.Broadcast, "FindComponentTypeIdsByEntityType", ["Terrain Tint Material"], entity.EntityType().Game)
    added = editor.EditorComponentAPIBus(bus.Broadcast, "AddComponentsOfType", owner, types)
    assert added.IsSuccess(), added.GetError()
    component = added.GetValue()[0]
    editor.ToolsApplicationRequestBus(bus.Broadcast, "SetSelectedEntities", [])
    RESULTS["properties"] = list(editor.EditorComponentAPIBus(bus.Broadcast, "BuildComponentPropertyList", component))
    quick = os.environ.get("TC_CANVAS_QUICK") == "1"
    names = ("minimal_tint", "procedural_tint") if quick else ("minimal_tint", "procedural_tint", "world_bands", "texture_tint")
    for name in names:
        material = asset.AssetCatalogRequestBus(bus.Broadcast, "GetAssetIdByPath",
            "materialcanvas/terrain/examples/" + name + ".azmaterial", math.Uuid(), False)
        assert material.is_valid(), name
        result = editor.EditorComponentAPIBus(bus.Broadcast, "SetComponentProperty", component, "Tint Material", material)
        assert result.IsSuccess(), result.GetError()
        try:
            wait_for(lambda: status(owner) == "Canvas terrain tint active")
        finally:
            RESULTS[name + "_selection"] = status(owner)
        general.idle_wait_frames(120)
        RESULTS[name] = status(owner)
        screenshot(name)
        gpu_capture(name)
        if name == "procedural_tint" and not quick:
            # Exercise source-material reload without editing generated shader code.
            source = Path(__file__).resolve().parents[1] / "Assets/MaterialCanvas/Terrain/Examples/procedural_tint.material"
            original = source.read_bytes()
            try:
                edited = json.loads(original)
                edited["propertyValues"]["tint.strength"] = 1.0
                edited["propertyValues"]["tint.color"] = [1.0, 0.05, 0.05]
                source.write_text(json.dumps(edited, indent=4) + "\n", encoding="utf-8")
                general.idle_wait_frames(1200)
                assert status(owner) == "Canvas terrain tint active", status(owner)
                screenshot("parameter_reload")
                RESULTS["parameter_reload"] = status(owner)
            finally:
                source.write_bytes(original)
            general.idle_wait_frames(1200)
            assert status(owner) == "Canvas terrain tint active", status(owner)
            screenshot("parameter_restored")
            if os.environ.get("TC_CANVAS_TEST_GRAPH_RELOAD") == "1":
                graph = source.with_suffix(".materialgraph")
                original_graph = graph.read_bytes()
                def compile_graph():
                    env = dict(os.environ, TC_CANVAS_GRAPH=graph.name)
                    executable = Path(paths.executableFolder) / "MaterialCanvas.exe"
                    report = Path(__file__).resolve().parents[1] / "compile-results.json"
                    previous_report_time = report.stat().st_mtime_ns if report.exists() else 0
                    process = subprocess.Popen([str(executable), "--project-path=" + str(paths.projectroot),
                        "--rhi=null", "--allowMultipleInstances", "--autotest_mode", "--timeout=180000",
                        "--regset=/O3DE/AtomToolsFramework/AtomToolsDocumentSystem/DisplayErrorMessageDialogs=false",
                        "--runpython=" + str(Path(__file__).with_name("CompileExamples.py"))],
                        env=env, creationflags=subprocess.CREATE_NO_WINDOW)
                    try:
                        wait_for(lambda: process.poll() is not None, 180)
                        RESULTS.setdefault("canvas_process_exit_codes", []).append(process.returncode)
                        # This pinned tool can fault in shutdown after successful
                        # generation. Record that fault separately; require fresh
                        # native compiler evidence rather than trusting old files.
                        assert report.exists() and report.stat().st_mtime_ns > previous_report_time
                        generated = json.loads(report.read_text())
                        assert any(Path(item["graph"]).name == graph.name for item in generated)
                    finally:
                        if process.poll() is None:
                            process.terminate()
                    general.idle_wait_frames(600)
                    assert status(owner) == "Canvas terrain tint active", status(owner)
                try:
                    changed = json.loads(original_graph)
                    node = next(n["Value"] for n in changed["ClassData"]["m_nodes"] if n["Key"] == 3)
                    slot = next(s for s in node["m_inputDataSlots"] if s["Key"]["m_name"] == "scale")
                    slot["Value"]["m_value"]["Value"] = 8.0
                    graph.write_text(json.dumps(changed, indent=4) + "\n", encoding="utf-8")
                    compile_graph()
                    screenshot("graph_reload_scale8")
                    RESULTS["graph_reload"] = status(owner)
                finally:
                    graph.write_bytes(original_graph)
                    compile_graph()
                screenshot("graph_restored")
    invalid = asset.AssetCatalogRequestBus(bus.Broadcast, "GetAssetIdByPath",
        "materials/terrain/defaultpbrterrain.azmaterial", math.Uuid(), False)
    assert invalid.is_valid()
    rejected = editor.EditorComponentAPIBus(bus.Broadcast, "SetComponentProperty", component, "Tint Material", invalid)
    assert rejected.IsSuccess()
    wait_for(lambda: "Incompatible tint material" in status(owner))
    RESULTS["invalid_contract"] = status(owner)
    screenshot("invalid_retains_active")
    restored = editor.EditorComponentAPIBus(bus.Broadcast, "SetComponentProperty", component, "Tint Material", material)
    assert restored.IsSuccess()
    wait_for(lambda: status(owner) == "Canvas terrain tint active")
    general.enter_game_mode()
    def find_game_owner():
        candidate = general.find_game_entity("Terrain Canvas Validation")
        return candidate if candidate.IsValid() else None
    game_owner = wait_for(find_game_owner)
    wait_for(lambda: status(game_owner) == "Canvas terrain tint active")
    RESULTS["runtime"] = status(game_owner)
    general.exit_game_mode()
    general.idle_wait_frames(60)
    editor.EditorComponentAPIBus(bus.Broadcast, "DisableComponents", [component])
    general.idle_wait_frames(120)
    screenshot("restored_legacy")
    assert editor.EditorComponentAPIBus(bus.Broadcast, "SetComponentProperty", ground_component, strength_path, previous_strength).IsSuccess()
    RESULTS["passed"] = True
    editor.ToolsApplicationRequestBus(bus.Broadcast, "DeleteEntityById", owner)


try:
    main()
except Exception:
    RESULTS["passed"] = False
    RESULTS["error"] = traceback.format_exc()
finally:
    (OUTPUT / "results.json").write_text(json.dumps(RESULTS, indent=2) + "\n", encoding="utf-8")
    print("TERRAIN_CANVAS_VALIDATION " + str(RESULTS))
