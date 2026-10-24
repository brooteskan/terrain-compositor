import copy
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import importlib.util
import types
import ast
import runpy
from unittest.mock import patch

PROJECT = next(p for p in Path(__file__).resolve().parents if (p/'project.json').is_file())
sys.path.insert(0, str(PROJECT/'Gem/Source/Editor/VueLiveLink'))
import terrain_overlap as overlap

RAW = '''Vertices: *12 { a: -1,-1,-5,1,-1,1,1,1,5,-1,1,-1 }
UV: *8 { a: 0,0,1,0,1,1,0,1 }
PolygonVertexIndex: *4 { a: 0,1,2,-4 }
UVIndex: *4 { a: 0,1,2,3 }
'''


def adaptive_raw(complete=True):
    coords = [(0, 0), (.998046875, 0), (.998046875, .998046875),
              (0, .998046875), (.31, .43)]
    vertices = [(256*u-128, 256*v-128, 3*(256*u-128)+2*(256*v-128)) for u, v in coords]
    faces = [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)]
    if not complete:
        faces.pop(0)
    indices = [index for face in faces for index in (*face[:-1], -face[-1]-1)]
    uv_indices = [index for face in faces for index in face]
    def array(name, values):
        return '%s: *%d { a: %s }' % (name, len(values), ','.join(map(str, values)))
    return '\n'.join((array('Vertices', [x for p in vertices for x in p]),
        array('UV', [x for p in coords for x in p]), array('PolygonVertexIndex', indices),
        array('UVIndex', uv_indices)))


class VueTerrainOverlapTests(unittest.TestCase):
    def test_generated_layout_specializes_published_count_and_precision(self):
        # Exercise the generator, rather than a second hand-maintained layout.
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory)
            folder = project/'Assets/VueLiveLink'
            folder.mkdir(parents=True)
            (folder/'VueTerrainMesh.materialtype').write_bytes(
                (PROJECT/'Assets/VueLiveLink/VueTerrainMesh.materialtype').read_bytes())
            overlap.install_material_layout(project)
            layout = json.loads((folder/'VueTerrainOverlap.materialtype').read_text())
            canonical = Path(__file__).resolve().parents[2]/'integrations/tg/project'
            self.assertEqual(layout, json.loads((canonical/'Assets/VueLiveLink/VueTerrainOverlap.materialtype').read_text()))
            group = next(g for g in layout['propertyLayout']['propertyGroups'] if g.get('name') == 'terrainOverlap')
            connections = {p['name']:p['connection'] for p in group['properties']}
            self.assertEqual(connections['count'],{'type':'ShaderOption','name':'o_terrainOverlapSourceCount'})
            self.assertEqual(connections['heightFp16'],{'type':'ShaderOption','name':'o_terrainHeightFp16'})

    def test_equivalent_response_defaults_allow_blending_but_authored_differences_do_not(self):
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory)
            layout = project/'Assets/VueLiveLink/VueTerrainMesh.materialtype'
            layout.parent.mkdir(parents=True)
            overlap.write_json(layout, {'propertyLayout': {'propertyGroups': [
                {'$import': 'VueResponsePropertyGroup.json'},
                {'name': 'groundBlend', 'properties': [
                    {'name': 'colorTint', 'type': 'Color', 'defaultValue': [1,1,1,1]},
                    {'name': 'normalTexture', 'type': 'Image'}]}]}})
            declarations = layout.with_name('VueResponsePropertyGroup.json')
            overlap.write_json(declarations, {'name': 'vueResponse', 'properties': [
                {'name': 'directGain', 'type': 'Float', 'defaultValue': 1},
                {'name': 'backlight', 'type': 'Float', 'defaultValue': 0}]})
            sources = []
            for i in range(2):
                folder = project/f'Assets/source{i}'
                folder.mkdir()
                (folder/'terrain.fbx').write_text(RAW)
                (folder/'normals.dds').write_bytes(b'fixture')
                props = {'normalSmoothing.field': 'normals.dds'}
                props.update({'vueResponse.directGain': 1.0, 'vueResponse.backlight': 0.0,
                              'groundBlend.normalTexture': None} if i else {'groundBlend.colorTint': [1,1,1]})
                overlap.write_json(folder/'terrain.material', {'propertyValues': props})
                sources.append(dict(key=str(i), material=str(folder/'terrain.material'), fbx=str(folder/'terrain.fbx'),
                    matrix=[[1,0,0],[0,1,0],[0,0,1]], translation=[i,0,0], normalRows=[[1,0,0],[0,1,0],[0,0,1]]))
            before = [Path(source['material']).read_bytes() for source in sources]
            self.assertEqual(len(overlap.prepare(project, sources)), 2)
            self.assertEqual(before, [Path(source['material']).read_bytes() for source in sources])
            paths = {Path(path) for path, _, _ in overlap.dependency_state(project, sources[0])}
            self.assertIn(layout, paths)
            self.assertIn(declarations, paths)
            fingerprint = overlap.fingerprint(project, sources, 10)
            declarations.write_text(declarations.read_text()+'\n')
            self.assertNotEqual(fingerprint, overlap.fingerprint(project, sources, 10))
            second = Path(sources[1]['material'])
            data = json.loads(second.read_text())
            data['propertyValues']['vueResponse.directGain'] = 2.0
            overlap.write_json(second, data)
            with self.assertRaisesRegex(ValueError, 'matching Vue lighting response'):
                overlap.prepare(project, sources)

    def test_adaptive_height_query_retains_uv_scale_and_extends_only_outer_border(self):
        size = 16
        data, lo, span = overlap.height_field(adaptive_raw(), size)
        self.assertEqual(lo, [-128, -128])
        self.assertEqual(span, [256, 256])
        transform = [[0, -5, 0], [3, 0, 0], [0, 0, 7]]
        values = overlap.mapping(transform, [100, -50, 11], lo, span)
        for y in range(size):
            for x in range(size):
                u, v = min(.998046875, x/(size-1)), min(.998046875, 1-y/(size-1))
                raw_x, raw_y = 256*u-128, 256*v-128
                height, coverage, _, _ = struct.unpack_from('<4f', data, 148+(y*size+x)*16)
                self.assertAlmostEqual(height, 3*raw_x+2*raw_y, places=3)
                self.assertEqual(coverage, 1)
                world = [100-5*raw_y, -50+3*raw_x, 1]
                self.assertAlmostEqual(sum(a*b for a,b in zip(values['uvRow0'], world)), u)
                self.assertAlmostEqual(sum(a*b for a,b in zip(values['uvRow1'], world)), 1-v)

    def test_adaptive_height_query_still_rejects_interior_holes_and_nonaffine_uvs(self):
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            overlap.height_field(adaptive_raw(complete=False), 16)
        raw = adaptive_raw().replace('-48.64', '-47.64', 1)
        with self.assertRaisesRegex(ValueError, 'affine'):
            overlap.height_field(raw, 16)

    def test_missing_normal_field_identifies_terrain_to_bake(self):
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory)
            sources = []
            for name in ('Terrain6', 'Terrain7'):
                folder = project/'Assets'/name
                folder.mkdir(parents=True)
                (folder/'terrain.fbx').write_text(RAW)
                (folder/'terrain.material').write_text('{"propertyValues": {}}')
                sources.append(dict(key=name, name=name, material=str(folder/'terrain.material'),
                    fbx=str(folder/'terrain.fbx'), matrix=[[1,0,0],[0,1,0],[0,0,1]],
                    translation=[0,0,0], normalRows=[[1,0,0],[0,1,0],[0,0,1]]))
            with self.assertRaisesRegex(ValueError, 'Terrain6: baked normal field is missing; use Bake terrain'):
                overlap.prepare(project, sources)

    def test_reload_replaces_overlap_monitor_after_reloading_its_model(self):
        events = []
        api = types.ModuleType('editor_link')
        api._SESSION = types.SimpleNamespace(timer=types.SimpleNamespace(isActive=lambda: True))
        api.stop = lambda: events.append('stop link')
        api.start = lambda: events.append('start link')
        monitor = types.ModuleType('terrain_overlap_editor')
        monitor._monitor = object()
        monitor.stop = lambda: events.append('stop overlap')
        monitor.start = lambda: events.append('start overlap')
        model = types.ModuleType('terrain_overlap')
        requests = types.ModuleType('link_requests')
        modules = {'editor_link': api, 'terrain_overlap_editor': monitor,
                   'terrain_overlap': model, 'link_requests': requests}
        with patch.dict(sys.modules, modules), patch('importlib.reload',
                side_effect=lambda module: events.append('reload '+module.__name__)):
            runpy.run_path(str(PROJECT/'Gem/Source/Editor/VueLiveLink/EditorScripts/ReloadLiveLink.py'))
        self.assertLess(events.index('stop overlap'), events.index('reload terrain_overlap'))
        self.assertLess(events.index('reload terrain_overlap'), events.index('reload terrain_overlap_editor'))
        self.assertLess(events.index('reload terrain_overlap_editor'), events.index('start overlap'))
        self.assertLess(events.index('start link'), events.index('start overlap'))

    def test_material_evidence_uses_only_serialized_slots_and_explicit_overrides(self):
        path = PROJECT/'Gem/Source/Editor/VueLiveLink/material_response_editor.py'
        function = next(n for n in ast.parse(path.read_text()).body
                        if isinstance(n, ast.FunctionDef) and n.name == 'assignment_evidence')
        data = {'Value': {'Configuration': {'materials': [
            {'Key': {'lodIndex': 2**64-1, 'materialSlotStableId': 2**32-1},
             'Value': {'PropertyOverrides': []}},
            {'Key': {'lodIndex': 2**64-1, 'materialSlotStableId': 42},
             'Value': {'PropertyOverrides': [{'Key': 'roughness.factor', 'Value': {'Value': .3}}]}}
        ]}}}
        class Slot:
            def __init__(self, lod, stable): self.lod, self.stable = lod, stable
            def ToString(self): return f'{self.stable}:{self.lod}'
            def IsDefault(self): return self.stable == 2**32-1 and self.lod == 2**64-1
            def __hash__(self): raise AssertionError('Native slot must never be a Python dictionary key')
        requests = []
        def request(_, event, owner, slot, *args):
            requests.append(event)
            if event == 'GetPropertyValue': return .3
            if event == 'IsMaterialAssetIdOverridden': return slot.IsDefault()
            if event == 'GetMaterialAssetId': return 'asset'
            raise AssertionError('Unexpected native material map request: ' + event)
        outcome = types.SimpleNamespace(IsSuccess=lambda: True,
            GetValue=lambda: types.SimpleNamespace(to_json=lambda: data))
        environment = dict(editor=types.SimpleNamespace(EditorComponentAPIBus=lambda *args: outcome),
            bus=types.SimpleNamespace(Broadcast=None, Event=None),
            render=types.SimpleNamespace(MaterialAssignmentId=Slot, MaterialComponentRequestBus=request),
            product_path=lambda asset: asset)
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(path), 'exec'), environment)
        target = dict(ground=False, owner='terrain', component='material', name='Terrain')
        evidence = environment['assignment_evidence'](target)
        self.assertEqual(evidence[f'{2**32-1}:{2**64-1}']['properties'], {})
        self.assertEqual(evidence[f'42:{2**64-1}']['properties'], {'roughness.factor': '0.3'})
        self.assertFalse(evidence[f'42:{2**64-1}']['default'])
        self.assertEqual(requests.count('GetPropertyValue'), 1)
        self.assertNotIn('GetMaterialMapCopy', requests)
        target['ground'] = True
        self.assertEqual(environment['assignment_evidence'](target), {})

    def test_adapter_layout_preserves_original_and_normalizes_color_defaults(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root/'Assets/VueLiveLink/VueTerrainMesh.materialtype'
            original.parent.mkdir(parents=True)
            original.write_text(json.dumps({'propertyLayout': {'propertyGroups': [
                {'name': 'groundBlend', 'properties': [
                    {'name': 'colorTint', 'type': 'Color', 'defaultValue': [1,1,1,1]}]}]}}))
            before = original.read_bytes()
            overlap.install_material_layout(root)
            self.assertEqual(before, original.read_bytes())
            adapter = json.loads(original.with_name('VueTerrainOverlap.materialtype').read_text())
            for group in adapter['propertyLayout']['propertyGroups']:
                for prop in group['properties']:
                    if prop['type'] == 'Color':
                        self.assertTrue(all(isinstance(v, float) for v in prop['defaultValue']))

    def test_monitor_waits_for_all_assets_and_removes_stale_bindings(self):
        # Exercise the actual monitor state machine with only the Editor buses
        # replaced. Publication, dependency detection and source files are real.
        fake = {}
        for name in ('PySide6', 'azlmbr', 'azlmbr.asset', 'azlmbr.bus', 'azlmbr.components', 'azlmbr.editor', 'azlmbr.render', 'azlmbr.entity',
                     'azlmbr.legacy', 'azlmbr.legacy.general', 'azlmbr.math'):
            fake[name] = types.ModuleType(name)
        for name, module in fake.items():
            if '.' in name:
                parent, child = name.rsplit('.', 1)
                setattr(fake[parent], child, module)
        fake['PySide6'].QtCore = None
        file = Path(overlap.__file__).with_name('terrain_overlap_editor.py')
        spec = importlib.util.spec_from_file_location('overlap_monitor_test', file)
        monitor_module = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, fake): spec.loader.exec_module(monitor_module)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); bindings = {}; sources = []
            for i in range(2):
                folder = root/f'Assets/source{i}'; folder.mkdir(parents=True)
                (folder/'terrain.fbx').write_text(RAW)
                (folder/'normals.dds').write_bytes(b'fixture')
                (folder/'terrain.material').write_text(json.dumps({'propertyValues':{'normalSmoothing.field':'normals.dds'}}))
                bindings[str(i)] = f'assets/source{i}/terrain.azmaterial'
                sources.append(dict(key=str(i),material=str(folder/'terrain.material'),fbx=str(folder/'terrain.fbx'),
                    matrix=[[1,0,0],[0,1,0],[0,0,1]],translation=[i,0,0],normalRows=[[1,0,0],[0,1,0],[0,0,1]]))
            config = root/'Assets/VueLiveLink/terrain_overlap.json'
            overlap.write_json(config, {'enabled':True, 'width':10})
            api = types.SimpleNamespace(PROJECT=root, STATE=root/'user', LEVEL='VueLiveLink', _SESSION=None,
                _set=lambda key, prop, value: bindings.__setitem__(key, value))
            response = types.SimpleNamespace(_applying=False, binding=lambda target: bindings[target['key']],
                product=lambda path: path, targets=lambda: [dict(key=s['key'],owner=s['key'],component=s['key'],property='material',ground=False) for s in sources])
            monitor_module.modules = lambda: (api, response)
            monitor_module.height_precision = lambda: 32
            monitor_module.raw_path = lambda value: str(value)
            monitor_module.snapshot = lambda: [(t, s, bindings[s['key']], overlap.original_material(root,bindings[s['key']]))
                for t,s in zip(response.targets(),sources)]
            monitor_module.general.get_current_level_name = lambda: 'VueLiveLink'
            monitor_module.general.is_in_game_mode = lambda: False
            monitor_module.bus.Broadcast = None
            monitor_module.azmath.Uuid = lambda: None
            class AssetId(str):
                def is_valid(self): return True
            monitor_module.asset.AssetCatalogRequestBus = lambda _, event, path, *args: AssetId(path)
            now = [0]
            monitor_module.time = types.SimpleNamespace(monotonic=lambda: now[0], perf_counter=lambda: now[0])
            m = monitor_module.Monitor.__new__(monitor_module.Monitor)
            m.key=m.pending=m.candidate_key=m.last_status=None
            m.candidate_since=0; m.busy=False
            m.full_scans=0; m.max_tick_ms=0
            m.needs_snapshot = True; m.wait_since = None
            class Timer:
                active = False
                def start(self, delay): self.active = True
                def stop(self): self.active = False
            m.timer = Timer()
            m.watch = types.SimpleNamespace(capture=lambda rows, **kwargs: None)
            m.tick(); now[0]=1; m.tick()
            self.assertEqual(m.last_status['phase'], 'processing')
            outputs = m.pending[0]
            for i,path in enumerate(outputs.values()):
                product = root/'Cache/pc'/path.relative_to(root).with_suffix('.azmaterial').as_posix().lower()
                product.parent.mkdir(parents=True, exist_ok=True); product.touch()
                m.tick()
                if i==0: self.assertEqual(bindings['0'], 'assets/source0/terrain.azmaterial')
            self.assertEqual(m.last_status['phase'], 'ready')
            old = dict(bindings); self.assertFalse(m.timer.active)
            scans = m.full_scans
            with patch.object(monitor_module, 'snapshot', side_effect=AssertionError('Idle scene scan')):
                for _ in range(100):
                    now[0] += 1
                    if m.timer.active: m.tick()
            self.assertEqual(m.full_scans, scans)
            self.assertEqual(bindings, old)
            sources[1]['translation'][0]+=5
            now[0]=102; m.request_update(); m.tick()
            self.assertEqual(bindings['0'], 'assets/source0/terrain.azmaterial')
            self.assertEqual(bindings['1'], 'assets/source1/terrain.azmaterial')
            scans = m.full_scans
            now[0]=103
            with patch.object(monitor_module, 'snapshot', side_effect=AssertionError('Asset wait scene scan')):
                m.tick()
            self.assertEqual(m.full_scans, scans)
            self.assertIsNotNone(m.pending)
            now[0]=400; m.tick()
            self.assertEqual(m.last_status['phase'], 'waiting')
            self.assertFalse(m.timer.active)
            sources.pop(); now[0]=401; m.request_update(); m.tick()
            self.assertEqual(m.last_status['phase'], 'disabled')
            self.assertIsNone(m.pending)
            self.assertFalse(m.timer.active)

            # File events check known dependencies, including atomic replacement
            # and deletion. Disabled overlap ignores terrain events and still
            # sees its configuration file; there is no selection-change trigger.
            watch = monitor_module.EditWatch.__new__(monitor_module.EditWatch)
            events = []
            watch.request = lambda: events.append('edit')
            watch.enabled = True; watch.owners = {'terrain'}
            watch.files = [config]
            watch.baseline = watch.file_state()
            arms = []
            watch.arm_files = lambda: arms.append(True)
            watch.terrain_changed(['unrelated']); self.assertEqual(events, [])
            watch.terrain_changed(['terrain']); self.assertEqual(events, ['edit'])
            watch.enabled = False
            watch.terrain_changed(['terrain']); watch.entity_changed()
            self.assertEqual(events, ['edit'])
            watch.file_changed(); self.assertEqual(events, ['edit'])
            replacement = config.with_suffix('.new'); replacement.write_text('{}')
            replacement.replace(config); watch.file_changed()
            self.assertEqual(events, ['edit', 'edit'])
            config.unlink(); watch.file_changed()
            self.assertEqual(events, ['edit', 'edit', 'edit'])
            self.assertEqual(len(arms), 3)
            watch.capture(monitor_module.snapshot())
            self.assertIn(Path(sources[0]['fbx'] + '.assetinfo'), watch.files)
            Path(sources[0]['material']).unlink()
            watch.capture(monitor_module.snapshot())
            self.assertIn(Path(sources[0]['material']), watch.files)
            watch.capture([], enabled=False)
            self.assertEqual(watch.files, [config])
            self.assertFalse(watch.enabled)

    def test_geometry_height_and_imported_v_flip(self):
        data, lo, span = overlap.height_field(RAW, 8)
        self.assertEqual((lo, span), ([-1, -1], [2, 2]))
        self.assertEqual(len(data), 148 + 8*8*16)
        for y in range(8):
            for x in range(8):
                height, coverage, _, _ = struct.unpack_from('<4f', data, 148 + (y*8+x)*16)
                self.assertAlmostEqual(height, 3*(2*x/7-1) + 2*(1-2*y/7), places=5)
                self.assertEqual(coverage, 1)

    def test_mapping_preserves_rotated_scaled_instance_uv(self):
        matrix = [[0, -5, 0], [3, 0, 0], [0, 0, 7]]
        values = overlap.mapping(matrix, [100, -50, 11], [-1, -1], [2, 2])
        for raw_x, raw_y in [(-1, -1), (.4, -.2), (1, 1)]:
            world = [100-5*raw_y, -50+3*raw_x, 1]
            self.assertAlmostEqual(sum(a*b for a, b in zip(values['uvRow0'], world)), (raw_x+1)/2)
            self.assertAlmostEqual(sum(a*b for a, b in zip(values['uvRow1'], world)), (1-raw_y)/2)
        self.assertEqual(values['heightScaleOffset'], [7, 11])
        with self.assertRaisesRegex(ValueError, 'Tilted'):
            overlap.mapping([[1,0,1],[0,1,0],[0,0,1]], [0,0,0], [-1,-1], [2,2])

    def test_sceneapi_yaw_and_normal_inverse_transpose(self):
        result = overlap.instance_mapping([[-1,0,0],[0,-1,0],[0,0,1]], [3,4,5], [2,3,4],
                                          [[5,0,0],[0,6,0],[0,0,7]])
        self.assertEqual(result['matrix'], [[10,0,0],[0,18,0],[0,0,28]])
        self.assertEqual(result['normalRows'], [[-.5,0,0],[0,-1/3,0],[0,0,.25]])

    def test_publication_is_immutable_tracks_changes_and_preserves_parent(self):
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory)
            sources = []
            for i in range(2):
                folder = project / f'Assets/source{i}'
                folder.mkdir(parents=True)
                (folder/'terrain.fbx').write_text(RAW)
                (folder/'normals.dds').write_bytes(b'fixture')
                parent = folder/'parent.material'
                parent.write_text(json.dumps({'propertyValues': {'normalSmoothing.field':'normals.dds'}}))
                material = folder/'terrain.material'
                material.write_text(json.dumps({'parentMaterial':'parent.material', 'propertyValues':{'roughness.factor': .2+i*.4}}))
                sources.append(dict(key=str(i), material=str(material), fbx=str(folder/'terrain.fbx'),
                    matrix=[[1,0,0],[0,1,0],[0,0,1]], translation=[i,0,0], normalRows=[[1,0,0],[0,1,0],[0,0,1]]))
            before = [Path(s['material']).read_bytes() for s in sources]
            first = overlap.prepare(project, sources)
            self.assertEqual(first, overlap.prepare(project, list(reversed(sources))))
            content = first['0'].read_bytes()
            owned = first['0'].relative_to(project).with_suffix('.azmaterial').as_posix()
            self.assertEqual(overlap.original_material(project, owned), 'assets/source0/terrain.azmaterial')
            data = json.loads(content)['propertyValues']
            self.assertEqual(data['overlapSource0.normalField'], 'Assets/source0/normals.dds')
            self.assertAlmostEqual(data['overlapSource1.roughness'], .6)
            sources[1]['translation'][0] += 4
            moved = overlap.prepare(project, sources)
            self.assertNotEqual(first, moved)
            self.assertEqual(first['0'].read_bytes(), content)
            Path(sources[1]['material']).write_text(json.dumps({'parentMaterial':'parent.material', 'propertyValues':{'roughness.factor': .1}}))
            changed = overlap.prepare(project, sources)
            self.assertNotEqual(moved, changed)
            self.assertEqual(Path(sources[0]['material']).read_bytes(), before[0])
            with self.assertRaisesRegex(ValueError, 'baked sources'):
                overlap.prepare(project, sources[:1])
            # The optimized endpoint uses the baked UV0 derivatives. Cover
            # compound occlusion property names as well as simple textureMapUv.
            for key in ('normal.textureMapUv','occlusion.diffuseTextureMapUv','occlusion.specularTextureMapUv'):
                original=Path(sources[0]['material']).read_text()
                material=json.loads(original);material['propertyValues'][key]='Unwrapped'
                overlap.write_json(sources[0]['material'],material)
                with self.assertRaisesRegex(ValueError,'baked UV0'):
                    overlap.prepare(project,sources)
                Path(sources[0]['material']).write_text(original)
            invalid = copy.deepcopy(sources); invalid[1]['key'] = invalid[0]['key']
            with self.assertRaisesRegex(ValueError, 'Duplicate'):
                overlap.prepare(project, invalid)


if __name__ == '__main__':
    unittest.main()
