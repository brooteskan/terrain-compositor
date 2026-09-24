"""Check portability boundaries that numerical concatenation tests cannot see."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NodeLibraryTests(unittest.TestCase):
    def test_unique_configs_and_categories(self):
        ids = set()
        for path in (ROOT / 'Assets/MaterialCanvas').rglob('*.materialgraphnode'):
            c = json.loads(path.read_text())['ClassData']
            self.assertNotIn(c['id'], ids, path)
            ids.add(c['id'])
            if 'Procedural' in path.parts:
                self.assertIn(c['category'], ('Math Functions', 'Procedural'))
                self.assertNotRegex(path.read_text(), r'\b(context|incoming|TerrainMaterialSrg|TerrainSurfaceChannels)\b')
            elif path.name == 'output.materialgraphnode':
                self.assertEqual(c['category'], 'Material Outputs')

    def test_include_closure_and_ownership(self):
        shader_root = ROOT / 'Assets/ShaderLib'
        for path in (ROOT / 'Assets/MaterialCanvas/Procedural/Nodes').glob('*.materialgraphnode'):
            c = json.loads(path.read_text())['ClassData']
            includes = c.get('settings', {}).get('includePaths', [])
            if path.stem in ('noise', 'normal_from_height'):
                self.assertEqual(len(includes), 1)
            for include in includes:
                helper = shader_root / include
                self.assertTrue(helper.is_file())
                self.assertNotRegex(helper.read_text(), r'TerrainCompositor|TerrainSurface|TerrainMaterialSrg|\bincoming\b|\bcontext\b')
                self.assertFalse(re.findall(r'#include\s+[<"]Terrain', helper.read_text()))
        template = (ROOT / 'Assets/MaterialCanvas/Terrain/Templates/MaterialGraphName_Tint.azsli').read_text()
        self.assertNotIn('LatticeNoise.azsli', template)
        self.assertNotIn('NormalFromHeight.azsli', template)
        self.assertNotIn('SurfaceHelpers.azsli', template)


if __name__ == '__main__':
    unittest.main()
