import json
from pathlib import Path
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools'))
from CompilerContract import DEFAULTS, verify_terrain_contract

class CompilerContractTests(unittest.TestCase):
    def test_defaults_are_numeric_and_normal_uses_geometry_sentinel(self):
        config = json.loads((ROOT / 'Assets/MaterialCanvas/Terrain/Nodes/output.materialgraphnode').read_text())['ClassData']
        self.assertEqual({s['name']: s['defaultValue']['Value'] for s in config['inputSlots']}, DEFAULTS)
        self.assertNotIn('unconnectedValueExpression', json.dumps(config))
        self.assertFalse(any(p.endswith('.material') for p in config['settings']['templatePaths']))

    def test_shipped_instances_bind_generated_property_names(self):
        examples = ROOT / 'Assets/MaterialCanvas/Terrain/Examples'
        for path in examples.glob('*.material'):
            instance = json.loads(path.read_text())
            self.assertNotIn('properties', instance, 'Flat values require the native propertyValues schema')
            material_type = json.loads((path.parent / instance['materialType']).read_text())
            names = {group['name'] + '.' + prop['name']
                     for group in material_type['propertyLayout']['propertyGroups'] for prop in group['properties']}
            self.assertTrue(set(instance['propertyValues']).issubset(names), path.name)
        textures = json.loads((examples / 'textured_terrain.material').read_text())['propertyValues']
        self.assertNotEqual(textures['surface.texture_a'], textures['surface.texture_b'])
        self.assertTrue(all((examples / value).is_file() for value in textures.values()))

    def test_shared_layout_and_obsolete_surface_rejection(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = {'ClassData': {'m_nodes': [{'Key': 1, 'Value': {'configId': '{EB039C35-AA34-58D2-9992-BF88368F5DFB}'}}]}}
            shader = 'const MaterialParameters params\n#include "probe_Material.azsli"\n' + '\n'.join(
                f'{s} = {json.dumps(v)};' for s, v in DEFAULTS.items())
            contents = {'probe_Tint.azsli': shader, 'probe_Parameters.azsli': 'struct MaterialParameters { uint id; };',
                        'probe_Material.azsli': '#define TC_GRAPH_MATERIAL\n#include "probe_Parameters.azsli"',
                        'probe_Depth.azsl': '#include "probe_Material.azsli"',
                        'probe.materialtype': json.dumps({'shaders': [{'file': 'probe_' + p + '.shader'} for p in ['Forward', 'Depth', 'Shadow']]})}
            for name, text in contents.items(): (root / name).write_text(text)
            generated = list(root.iterdir())
            self.assertEqual(verify_terrain_contract(document, generated)['contract'], 'terrain-graph')
            # Native save preserves float32 precision while AZSL prints six decimals.
            document['ClassData']['m_nodes'][0]['Value']['m_inputDataSlots'] = [
                {'Key': {'m_name': 'inSpecularFactor'}, 'Value': {'m_value': {'Value': 0.50000001}}}]
            self.assertEqual(verify_terrain_contract(document, generated)['contract'], 'terrain-graph')
            (root / 'probe_Tint.azsli').write_text(shader + '\nincoming.baseColor')
            with self.assertRaisesRegex(ValueError, 'Obsolete'): verify_terrain_contract(document, generated)
            (root / 'probe_Tint.azsli').write_text(shader.replace('inNormal = [0, 0, 0]', 'inNormal = [0, 0, 1]'))
            with self.assertRaisesRegex(ValueError, 'Wrong default'): verify_terrain_contract(document, generated)
            (root / 'probe_Tint.azsli').write_text(shader)
            (root / 'probe_Depth.azsl').write_text('// missing declaration')
            with self.assertRaisesRegex(ValueError, 'share'): verify_terrain_contract(document, generated)

if __name__ == '__main__': unittest.main()
