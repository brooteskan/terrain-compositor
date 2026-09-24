import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools'))
import GraphMigration as shared
import MigrateReusableNodes as migration
import MigrateTintGraphs as tint


class ReusableMigrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stock = json.loads((ROOT / 'Tests/Fixtures/stock-node-contracts.json').read_text())
        cls.catalog = {c['id']: c for c in cls.stock}
        for path in (ROOT / 'Assets/MaterialCanvas').rglob('*.materialgraphnode'):
            c = json.loads(path.read_text())['ClassData']
            cls.catalog[c['id']] = c

    def fixture(self, name):
        return json.loads((ROOT / 'Tests/Fixtures/Legacy' / (name + '.materialgraph')).read_text())

    def test_all_examples_are_idempotent_and_keep_unrelated_data(self):
        for path in (ROOT / 'Tests/Fixtures/Legacy').glob('*.materialgraph'):
            with self.subTest(path=path.name):
                original = json.loads(path.read_text())
                before = copy.deepcopy(original)
                original['ClassData']['futureMetadata'] = {'preserve': [1, 2, 3]}
                result, changes = migration.migrate(original, self.catalog)
                self.assertEqual(original, dict(before, ClassData=dict(before['ClassData'], futureMetadata={'preserve': [1, 2, 3]})))
                self.assertEqual(result['ClassData']['futureMetadata'], original['ClassData']['futureMetadata'])
                self.assertEqual(migration.migrate(result, self.catalog), (result, {}))
                for node in result['ClassData']['m_nodes']:
                    self.assertNotIn(node['Value'].get('configId'), migration.MATH)
                    self.assertNotIn(node['Value'].get('configId'), (shared.GEOMETRY, shared.LEGACY_NORMAL))
                old_metadata = original['ClassData'].get('m_uiMetadata', {}).get('m_nodeMetadata', [])
                new_metadata = result['ClassData'].get('m_uiMetadata', {}).get('m_nodeMetadata', [])
                for item in old_metadata:
                    self.assertIn(item, new_metadata)

    def test_omitted_defaults_all_five_math_types(self):
        for old_id, (kind, defaults) in migration.MATH.items():
            with self.subTest(config=old_id):
                doc = self.fixture('surface_passthrough')
                doc['ClassData']['m_nodes'] = [{'Key': 17, 'Value': {
                    '$type': 'DynamicNode', 'toolId': 2034248906, 'configId': old_id}}]
                doc['ClassData']['m_connections'] = []
                result, changes = migration.migrate(doc, self.catalog)
                node = result['ClassData']['m_nodes'][0]
                self.assertEqual(node['Key'], 17)
                self.assertEqual(node['Value']['configId'], shared.STOCK[kind])
                actual = [s['Value']['m_value'] for s in node['Value']['m_inputDataSlots']]
                self.assertEqual(actual, [shared.literal(v) for _, v in defaults])

    def test_custom_literals_and_fanout(self):
        doc = self.fixture('minimal_tint')
        graph = doc['ClassData']
        lerp = next(n for n in graph['m_nodes'] if n['Value']['configId'] in migration.MATH)
        lerp['Value']['m_inputDataSlots'] = [shared.input_slot('a', shared.literal([1.8, .2, -.1])),
                                          shared.input_slot('weight', shared.literal(.25))]
        original = next(c for c in graph['m_connections'] if c['m_sourceEndpoint'][0] == lerp['Key'])
        duplicate = copy.deepcopy(original)
        duplicate['m_targetEndpoint'][1]['m_name'] = 'inBaseColor'
        duplicate['customConnectionData'] = 42
        graph['m_connections'].append(duplicate)
        result, _ = migration.migrate(doc, self.catalog)
        outgoing = [c for c in result['ClassData']['m_connections'] if c['m_sourceEndpoint'][0] == lerp['Key']]
        self.assertEqual(len(outgoing), 2)
        self.assertTrue(all(c['m_sourceEndpoint'][1]['m_name'] == 'outValue' for c in outgoing))
        self.assertEqual(outgoing[-1]['customConnectionData'], 42)
        updated = next(n for n in result['ClassData']['m_nodes'] if n['Key'] == lerp['Key'])
        self.assertEqual(updated['Value']['m_inputDataSlots'][0]['Value']['m_value']['Value'], [1.8, .2, -.1])

    def test_normal_uses_composed_shading_normal_and_explicit_position(self):
        result, _ = migration.migrate(self.fixture('procedural_normal'), self.catalog)
        graph = result['ClassData']
        normal = next(n for n in graph['m_nodes'] if n['Value']['configId'] == shared.NORMAL)
        nodes = {n['Key']: n['Value']['configId'] for n in graph['m_nodes']}
        inputs = {c['m_targetEndpoint'][1]['m_name']: c['m_sourceEndpoint'] for c in graph['m_connections']
                  if c['m_targetEndpoint'][0] == normal['Key']}
        self.assertEqual(nodes[inputs['inBaseNormalWS'][0]], shared.SURFACE)
        self.assertEqual(nodes[inputs['inPositionWS'][0]], shared.STOCK['position'])

    def test_rgb_output_width_cannot_shrink_when_all_inputs_are_connected(self):
        doc = self.fixture('minimal_tint')
        graph = doc['ClassData']
        lerp = next(n for n in graph['m_nodes'] if n['Value']['configId'] in migration.MATH)
        pos = shared.add_node(graph, lerp, shared.STOCK['position'])
        graph['m_connections'] = [c for c in graph['m_connections'] if c['m_targetEndpoint'][0] != lerp['Key']]
        for name in ('a', 'b', 'weight'):
            graph['m_connections'].append(shared.connect(shared.endpoint(pos, 'outZ'), shared.endpoint(lerp['Key'], name)))
        with self.assertRaisesRegex(ValueError, 'change output width'):
            migration.migrate(doc, self.catalog)

    def test_geometry_slope_retains_saturate_then_one_minus(self):
        result, _ = migration.migrate(self.fixture('slope_elevation'), self.catalog)
        graph = result['ClassData']
        subtract_id = shared.stock_function(self.catalog, 'inValue1 - inValue2', ['inValue1', 'inValue2'])
        subtract = next(n for n in graph['m_nodes'] if n['Value']['configId'] == subtract_id)
        self.assertEqual(subtract['Value']['m_inputDataSlots'][0]['Value']['m_value']['Value'], 1)
        self.assertTrue(any(c['m_targetEndpoint'] == shared.endpoint(subtract['Key'], 'inValue2')
                            for c in graph['m_connections']))

    def test_tint_and_reusable_migration_compose(self):
        original = self.fixture('minimal_tint')
        tinted, _ = tint.migrate(original)
        result, _ = migration.migrate(tinted, self.catalog)
        shared.validate(result, self.catalog)
        self.assertEqual(tint.migrate(result), (result, 0))
        self.assertEqual(migration.migrate(result, self.catalog), (result, {}))

    def test_malformed_and_unknown_source_types_leave_original_untouched(self):
        for defect in ('missing_node', 'duplicate_node', 'duplicate_connection', 'unknown_source', 'narrowing'):
            doc = self.fixture('procedural_tint')
            graph = doc['ClassData']
            if defect == 'missing_node':
                graph['m_connections'][0]['m_sourceEndpoint'][0] = 987654
            elif defect == 'duplicate_node':
                graph['m_nodes'].append(copy.deepcopy(graph['m_nodes'][0]))
            elif defect == 'duplicate_connection':
                graph['m_connections'].append(copy.deepcopy(graph['m_connections'][0]))
            else:
                dest = next(n for n in graph['m_nodes'] if n['Value']['configId'] == '{DCF36928-754A-51DD-8DB5-1DCA5DAA84C7}')
                c = next(c for c in graph['m_connections'] if c['m_targetEndpoint'][0] == dest['Key'])
                src = next(n for n in graph['m_nodes'] if n['Key'] == c['m_sourceEndpoint'][0])
                src['Value']['configId'] = '{UNKNOWN}' if defect == 'unknown_source' else shared.STOCK['position']
                c['m_sourceEndpoint'][1]['m_name'] = 'outPosition'
            before = copy.deepcopy(doc)
            with self.subTest(defect=defect), self.assertRaises(ValueError):
                migration.migrate(doc, self.catalog)
            self.assertEqual(doc, before)

    def test_atomic_backup_refusal_and_existing_temp_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'test.materialgraph'
            original = json.dumps(self.fixture('minimal_tint')).encode()
            path.write_bytes(original)
            result, _ = migration.migrate(json.loads(original), self.catalog)
            temporary = path.with_suffix('.materialgraph.migration.tmp')
            temporary.write_text('belongs to another operation')
            with self.assertRaises(FileExistsError):
                shared.write_graph(path, original, result)
            self.assertEqual(temporary.read_text(), 'belongs to another operation')
            self.assertEqual(path.read_bytes(), original)
            with self.assertRaises(FileExistsError):
                shared.write_graph(path, original, result)
            self.assertEqual(path.with_suffix('.materialgraph.bak').read_bytes(), original)

    def test_cli_dry_run_write_and_idempotence(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            nodes = root / 'Gems/Atom/Tools/MaterialCanvas/Assets/MaterialCanvas/GraphData/Nodes'
            nodes.mkdir(parents=True)
            for i, config in enumerate(self.stock):
                (nodes / f'{i}.materialgraphnode').write_text(json.dumps({'ClassData': config}))
            path = root / 'test.materialgraph'
            original = json.dumps(self.fixture('minimal_tint')).encode()
            path.write_bytes(original)
            command = [sys.executable, str(ROOT / 'Tools/MigrateReusableNodes.py'), str(path), '--engine-root', str(root)]
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(path.read_bytes(), original)
            self.assertFalse(path.with_suffix('.materialgraph.bak').exists())
            self.assertEqual(subprocess.run(command + ['--write'], capture_output=True).returncode, 0)
            converted = path.read_bytes()
            self.assertNotEqual(converted, original)
            self.assertEqual(subprocess.run(command + ['--write'], capture_output=True).returncode, 0)
            self.assertEqual(path.read_bytes(), converted)
            # Deliberately change the stock semantics; fail before any graph write.
            for file in nodes.glob('*.materialgraphnode'):
                config = json.loads(file.read_text())
                if config['ClassData']['id'] == shared.STOCK['multiply']:
                    config['ClassData']['slotDataTypeGroups'] = []
                    file.write_text(json.dumps(config))
            self.assertNotEqual(subprocess.run(command + ['--write'], capture_output=True).returncode, 0)
            self.assertEqual(path.read_bytes(), converted)


if __name__ == '__main__':
    unittest.main()
