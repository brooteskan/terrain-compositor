import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools'))
from CompilerContract import INHERITED, verify_terrain_contract


class CompilerContractTests(unittest.TestCase):
    def setUp(self):
        self.document = json.loads((ROOT / 'Tests/Fixtures/Legacy/surface_passthrough.materialgraph').read_text())

    def test_checks_actual_native_passthrough(self):
        path = ROOT / 'Assets/MaterialCanvas/Terrain/Examples/surface_passthrough_Tint.azsli'
        self.assertEqual(len(verify_terrain_contract(self.document, [path])['inherited']), 6)

    def test_stock_literal_defaults_cannot_impersonate_inheritance(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'probe_Tint.azsli'
            valid = 'TC_CanvasSurface(\n' + '\n'.join(f'{slot} = incoming.{field};' for slot, field in INHERITED.items())
            path.write_text(valid)
            self.assertEqual(len(verify_terrain_contract(self.document, [path])['inherited']), 6)
            for slot, field in INHERITED.items():
                path.write_text(valid.replace(f'{slot} = incoming.{field};', f'{slot} = 0;'))
                with self.subTest(slot=slot), self.assertRaisesRegex(ValueError, 'lacks terrain inheritance'):
                    verify_terrain_contract(self.document, [path])


if __name__ == '__main__':
    unittest.main()
