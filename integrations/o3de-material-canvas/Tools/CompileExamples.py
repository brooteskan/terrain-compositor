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
import ctypes
import subprocess
import azlmbr.atomtools as atomtools
import azlmbr.bus as bus
import azlmbr.atomtools.general as general
import azlmbr.paths as paths

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from CompilerContract import verify_terrain_contract


def provenance(gem):
    executable = pathlib.Path(sys.executable)
    if os.name == 'nt':
        buffer = ctypes.create_unicode_buffer(32768)
        ctypes.windll.kernel32.GetModuleFileNameW(None, buffer, len(buffer))
        executable = pathlib.Path(buffer.value)
    engine = pathlib.Path(paths.engroot)
    try:
        revision = subprocess.check_output(['git', '-c', 'safe.directory=' + engine.as_posix(),
            '-C', str(engine), 'rev-parse', 'HEAD'], text=True, stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0).strip()
    except (OSError, subprocess.CalledProcessError):
        revision = None
    inputs = [gem / 'EnginePatches/MaterialGraphCompiler.cpp.patch', gem / 'EngineOverrides.cmake',
              engine / 'Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.cpp']
    inputs.extend((gem / 'Assets/MaterialCanvas').rglob('*.materialgraphnode'))
    inputs.extend((gem / 'Assets/ShaderLib').rglob('*.azsli'))
    return {'executable': str(executable), 'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
            'engine': str(engine), 'engine_revision': revision,
            'input_sha256': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
            'validation': 'native source generation; Asset Processor and process exit must be checked separately'}


def main():
    gem = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[1]
    report_root = pathlib.Path(os.environ.get('TC_CANVAS_REPORT_ROOT', gem))
    report_root.mkdir(parents=True, exist_ok=True)
    # A failed run must not leave a previous run's success report behind.
    (report_root / "compile-results.json").unlink(missing_ok=True)
    (report_root / "compile-environment.json").write_text(json.dumps(provenance(gem), indent=2) + '\n', encoding='utf-8')
    results = []
    pattern = os.environ.get("TC_CANVAS_GRAPH", sys.argv[2] if len(sys.argv) > 2 else "*.materialgraph")
    (report_root / "compile-progress.json").write_text(json.dumps({"arguments": sys.argv, "pattern": pattern}), encoding="utf-8")
    graph_root = pathlib.Path(os.environ.get("TC_CANVAS_GRAPH_ROOT", gem / "Assets/MaterialCanvas/Terrain/Examples"))
    graphs = sorted(graph_root.glob(pattern))
    if not graphs:
        raise RuntimeError("No graphs matched " + pattern)
    for path in graphs:
        (report_root / "compile-progress.json").write_text(json.dumps({"graph": str(path), "state": "opening"}), encoding="utf-8")
        source_document = json.loads(path.read_text(encoding='utf-8'))
        source_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        document = atomtools.AtomToolsDocumentSystemRequestBus(bus.Broadcast, "OpenDocument", str(path))
        if document.IsNull():
            raise RuntimeError("Cannot open " + str(path))
        deadline = time.monotonic() + 90
        # Clear the previous file list so a stale generation cannot count as success.
        atomtools.GraphDocumentRequestBus(bus.Event, "SetGeneratedFilePaths", document, [])
        atomtools.GraphDocumentRequestBus(bus.Event, "QueueCompileGraph", document)
        (report_root / "compile-progress.json").write_text(json.dumps({"graph": str(path), "state": "compiling"}), encoding="utf-8")
        generated = []
        while time.monotonic() < deadline:
            general.idle_wait_frames(10)
            generated = atomtools.GraphDocumentRequestBus(bus.Event, "GetGeneratedFilePaths", document)
            if generated and all(pathlib.Path(p).exists() for p in generated):
                break
        if not generated or not all(pathlib.Path(p).exists() for p in generated):
            raise RuntimeError("Compilation did not complete: " + str(path))
        contract = verify_terrain_contract(source_document, generated)
        if os.environ.get('TC_CANVAS_ROUNDTRIP') == '1':
            if not atomtools.AtomToolsDocumentSystemRequestBus(bus.Broadcast, 'SaveDocument', document):
                raise RuntimeError('Native graph save failed: ' + str(path))
            saved = json.loads(path.read_text(encoding='utf-8'))
            before = source_document['ClassData']
            after = saved['ClassData']
            if before.get('m_connections', []) != after.get('m_connections', []):
                raise RuntimeError('Native round trip changed graph wiring: ' + str(path))
            if {(n['Key'], n['Value'].get('configId')) for n in before['m_nodes']} != {
                    (n['Key'], n['Value'].get('configId')) for n in after['m_nodes']}:
                raise RuntimeError('Native round trip changed node identities: ' + str(path))
        results.append({"graph": str(path), "source_sha256": source_hash, "contract": contract, "generated": list(generated),
            "sha256": {str(p): hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() for p in generated}})
        # A first-time .material load can have failed before its generated type
        # existed. Reassess the user-owned file without replacing its contents.
        material = path.with_suffix(".material")
        if material.exists():
            material.touch()
        atomtools.AtomToolsDocumentSystemRequestBus(bus.Broadcast, "CloseDocument", document)
    report = report_root / "compile-results.json"
    report.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print("TERRAIN_CANVAS_COMPILE_SUCCESS " + str(report))


if __name__ == "__main__":
    try:
        main()
    finally:
        general.exit()
