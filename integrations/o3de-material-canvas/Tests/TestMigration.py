import copy
import importlib.util
import json
from pathlib import Path
import unittest
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools'))
spec = importlib.util.spec_from_file_location('migration', ROOT / 'Tools/MigrateTintGraphs.py')
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)


class MigrationTests(unittest.TestCase):
    def source(self):
        return json.loads((ROOT / 'Tests/Fixtures/Legacy/minimal_tint.materialgraph').read_text())

    def test_connected_tint_preserves_source_nodes_and_rewires_multiply(self):
        original = self.source()
        before = copy.deepcopy(original)
        result, count = migration.migrate(original)
        self.assertEqual(count, 1)
        self.assertEqual(original, before)
        graph = result['ClassData']
        old = next(c for c in before['ClassData']['m_connections'] if c['m_targetEndpoint'][1]['m_name'] == 'inTint')
        output_id = old['m_targetEndpoint'][0]
        multiply = next(n for n in graph['m_nodes'] if n['Value']['configId'] == migration.config_id('stock_multiply'))['Key']
        self.assertIn({'m_sourceEndpoint': old['m_sourceEndpoint'], 'm_targetEndpoint': [multiply, {'m_name': 'inValue2'}]}, graph['m_connections'])
        self.assertIn({'m_sourceEndpoint': [multiply, {'m_name': 'outValue'}], 'm_targetEndpoint': [output_id, {'m_name': 'inBaseColor'}]}, graph['m_connections'])
        for node in before['ClassData']['m_nodes']:
            if node['Key'] != output_id:
                self.assertIn(node, graph['m_nodes'])
        again, changes = migration.migrate(result)
        self.assertEqual(changes, 0)
        self.assertEqual(again, result)

    def test_constant_tint_and_existing_base_color_are_both_preserved(self):
        doc = self.source()
        graph = doc['ClassData']
        connection = next(c for c in graph['m_connections'] if c['m_targetEndpoint'][1]['m_name'] == 'inTint')
        output_id = connection['m_targetEndpoint'][0]
        connection['m_targetEndpoint'][1]['m_name'] = 'inBaseColor'
        output = next(n['Value'] for n in graph['m_nodes'] if n['Key'] == output_id)
        next(s for s in output['m_inputDataSlots'] if s['Key']['m_name'] == 'inTint')['Value']['m_value']['Value'] = [.2, .4, 1.7]
        result, count = migration.migrate(doc)
        self.assertEqual(count, 1)
        multiply = next(n for n in result['ClassData']['m_nodes'] if n['Value']['configId'] == migration.config_id('stock_multiply'))
        self.assertEqual(multiply['Value']['m_inputDataSlots'][1]['Value']['m_value']['Value'], [.2, .4, 1.7])
        self.assertIn({'m_sourceEndpoint': connection['m_sourceEndpoint'], 'm_targetEndpoint': [multiply['Key'], {'m_name': 'inValue1'}]}, result['ClassData']['m_connections'])
        self.assertEqual(migration.migrate(result)[1], 0)

    def test_passthrough_needs_no_migration(self):
        doc = json.loads((ROOT / 'Tests/Fixtures/Legacy/surface_passthrough.materialgraph').read_text())
        self.assertEqual(migration.migrate(doc), (doc, 0))


if __name__ == '__main__':
    unittest.main()
