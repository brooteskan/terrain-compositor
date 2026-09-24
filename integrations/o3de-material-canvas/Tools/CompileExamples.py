"""Run inside MaterialCanvas with --runpython and --runpythonargs <Gem path>.

Uses the installed native graph compiler; never synthesizes shader expressions.
The user-owned .material files are intentionally not generation templates.
"""
import json
import pathlib
import os
import sys
import time
import hashlib
import azlmbr.atomtools as atomtools
import azlmbr.bus as bus
import azlmbr.atomtools.general as general


def main():
    gem = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[1]
    # A failed run must not leave a previous run's success report behind.
    (gem / "compile-results.json").unlink(missing_ok=True)
    results = []
    pattern = os.environ.get("TC_CANVAS_GRAPH", sys.argv[2] if len(sys.argv) > 2 else "*.materialgraph")
    (gem / "compile-progress.json").write_text(json.dumps({"arguments": sys.argv, "pattern": pattern}), encoding="utf-8")
    graph_root = pathlib.Path(os.environ.get("TC_CANVAS_GRAPH_ROOT", gem / "Assets/MaterialCanvas/Terrain/Examples"))
    graphs = sorted(graph_root.glob(pattern))
    if not graphs:
        raise RuntimeError("No graphs matched " + pattern)
    for path in graphs:
        (gem / "compile-progress.json").write_text(json.dumps({"graph": str(path), "state": "opening"}), encoding="utf-8")
        document = atomtools.AtomToolsDocumentSystemRequestBus(bus.Broadcast, "OpenDocument", str(path))
        if document.IsNull():
            raise RuntimeError("Cannot open " + str(path))
        deadline = time.monotonic() + 90
        # Clear the previous file list so a stale generation cannot count as success.
        atomtools.GraphDocumentRequestBus(bus.Event, "SetGeneratedFilePaths", document, [])
        atomtools.GraphDocumentRequestBus(bus.Event, "QueueCompileGraph", document)
        (gem / "compile-progress.json").write_text(json.dumps({"graph": str(path), "state": "compiling"}), encoding="utf-8")
        generated = []
        while time.monotonic() < deadline:
            general.idle_wait_frames(10)
            generated = atomtools.GraphDocumentRequestBus(bus.Event, "GetGeneratedFilePaths", document)
            if generated and all(pathlib.Path(p).exists() for p in generated):
                break
        if not generated or not all(pathlib.Path(p).exists() for p in generated):
            raise RuntimeError("Compilation did not complete: " + str(path))
        results.append({"graph": str(path), "generated": list(generated),
            "sha256": {str(p): hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() for p in generated}})
        # A first-time .material load can have failed before its generated type
        # existed. Reassess the user-owned file without replacing its contents.
        material = path.with_suffix(".material")
        if material.exists():
            material.touch()
        atomtools.AtomToolsDocumentSystemRequestBus(bus.Broadcast, "CloseDocument", document)
    report = gem / "compile-results.json"
    report.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print("TERRAIN_CANVAS_COMPILE_SUCCESS " + str(report))


if __name__ == "__main__":
    try:
        main()
    finally:
        general.exit()
