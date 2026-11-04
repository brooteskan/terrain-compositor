import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

MODULE=Path(__file__).resolve().parents[1]/'tools/create_mesh_blend_material.py'
spec=importlib.util.spec_from_file_location('mesh_blend',MODULE)
adapter=importlib.util.module_from_spec(spec); spec.loader.exec_module(adapter)

class ImportedMeshBlendTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        (self.root/'source').mkdir(); (self.root/'terrain').mkdir()
        (self.root/'source/atlas.png').write_bytes(b'unchanged source atlas')
        (self.root/'terrain/ground.png').write_bytes(b'shared terrain texture')
        self.base=self.root/'source/base.material'
        self.base.write_text(json.dumps({'materialType':'Materials/Types/StandardPBR.materialtype','propertyValues':{
            'baseColor.textureMap':'atlas.png','normal.flipY':True,'parallax.factor':.16,'roughness.factor':.7}}))
        self.profile=self.root/'terrain/profile.json'
        self.profile.write_text(json.dumps({'propertyValues':{'terrainBlend.colorTexture':'ground.png','terrainBlend.distance':.15}}))
        self.output=self.root/'result/blended.material'

    def test_keeps_source_properties_and_resolves_each_input_from_its_own_folder(self):
        original=self.base.read_bytes()
        data=adapter.create(self.base,self.profile,self.output)
        props=data['propertyValues']
        self.assertEqual((self.output.parent/props['baseColor.textureMap']).resolve(),self.root/'source/atlas.png')
        self.assertEqual((self.output.parent/props['terrainBlend.colorTexture']).resolve(),self.root/'terrain/ground.png')
        self.assertTrue(props['normal.flipY']); self.assertEqual(props['parallax.factor'],.16)
        self.assertEqual(self.base.read_bytes(),original)
        self.assertEqual((self.root/'source/atlas.png').read_bytes(),b'unchanged source atlas')

    def test_inheritance_and_repeat_export(self):
        child=self.root/'source/child.material'
        child.write_text(json.dumps({'parentMaterial':'base.material','propertyValues':{'roughness.factor':.2}}))
        data=adapter.create(child,self.profile,self.output); before=self.output.read_bytes()
        self.assertEqual(data['propertyValues']['roughness.factor'],.2)
        adapter.create(child,self.profile,self.output); self.assertEqual(self.output.read_bytes(),before)

    def test_invalid_input_preserves_existing_output(self):
        adapter.create(self.base,self.profile,self.output); before=self.output.read_bytes()
        for distance in (-1,float('nan'),float('inf')):
            self.profile.write_text(json.dumps({'propertyValues':{'terrainBlend.colorTexture':'ground.png','terrainBlend.distance':distance}}))
            with self.assertRaises(ValueError): adapter.create(self.base,self.profile,self.output)
            self.assertEqual(self.output.read_bytes(),before)

    def test_rejects_unsupported_graph_and_missing_images(self):
        data=json.loads(self.base.read_text()); data['materialType']='CustomGraph.materialtype'
        self.base.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError,'StandardPBR'): adapter.create(self.base,self.profile,self.output)
        data['materialType']='StandardPBR.materialtype'; data['propertyValues']['baseColor.textureMap']='missing.png'
        self.base.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError,'Missing material texture'): adapter.create(self.base,self.profile,self.output)

    def test_rejects_material_inheritance_cycle(self):
        self.base.write_text(json.dumps({'parentMaterial':'base.material'}))
        with self.assertRaisesRegex(ValueError,'cycle'): adapter.create(self.base,self.profile,self.output)

if __name__=='__main__': unittest.main()
