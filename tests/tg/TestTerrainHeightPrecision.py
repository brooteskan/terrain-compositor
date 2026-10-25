"""Precision formats and real production half arithmetic (not a storage-only probe)."""
from pathlib import Path
import importlib.util,json,struct,sys,tempfile,unittest
import ast,types

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'integrations/tg/project/Gem/Source/Editor/VueLiveLink'))
sys.path.insert(1,str(ROOT.parents[1]/'Gem/Source/Editor/VueLiveLink'))
import terrain_overlap as model

RAW='''Vertices: *12 { a: 0,0,1.23456, 1,0,2.34567, 1,1,3.45678, 0,1,4.56789 }
PolygonVertexIndex: *4 { a: 0,1,2,-4 }
UV: *8 { a: 0,0,1,0,1,1,0,1 }
UVIndex: *4 { a: 0,1,2,3 }
'''

class HeightPrecisionTests(unittest.TestCase):
    def test_formats_pitch_payload_and_identical_mapping(self):
        full,lo,span=model.height_field(RAW,4,32)
        half,lo16,span16=model.height_field(RAW,4,16)
        self.assertEqual((lo,span),(lo16,span16))
        self.assertEqual(struct.unpack_from('<I',full,128)[0],41)
        self.assertEqual(struct.unpack_from('<I',half,128)[0],54)
        self.assertEqual(struct.unpack_from('<I',full,20)[0],16)
        self.assertEqual(struct.unpack_from('<I',half,20)[0],8)
        self.assertEqual(len(full),148+16*4);self.assertEqual(len(half),148+16*2)
        for f,h in zip(struct.iter_unpack('<f',full[148:]),struct.iter_unpack('<e',half[148:])):
            self.assertEqual(h,struct.unpack('<e',struct.pack('<e',*f)))
    def test_invalid_precision_and_half_overflow_are_rejected(self):
        with self.assertRaises(ValueError):model.height_field(RAW,4,8)
        with self.assertRaises(ValueError):model.height_field(RAW.replace('1.23456','70000'),4,16)
    def test_precision_is_part_of_publication_identity(self):
        self.assertNotEqual(model.fingerprint('.',[],10,16),model.fingerprint('.',[],10,32))
    def test_lost_ownership_metadata_recovers_exact_current_appearance(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);child=p/'Assets/VueLiveLink/overlap/a/old.material'
            properties={'normalSmoothing.field':'Assets/n.dds','vueResponse.specularGain':0.0,
                        'baseColor.color':[0.8,0.7,0.6],'overlapSource0.heightMap':'Assets/h.dds','terrainOverlap.width':10}
            model.write_json(child,{'description':model.OWNED_DESCRIPTION,'materialTypeVersion':2,'propertyValues':properties})
            original=model.original_material(p,child.relative_to(p).with_suffix('.azmaterial').as_posix())
            captured=json.loads((p/original).with_suffix('.material').read_text())
            self.assertEqual(captured['propertyValues'],{k:v for k,v in properties.items() if not k.startswith(('overlapSource','terrainOverlap'))})
            self.assertEqual(original,model.original_material(p,child.relative_to(p).with_suffix('.azmaterial').as_posix()))
    def test_native_compiler_flag_and_option_are_explicit(self):
        text=(ROOT/'integrations/tg/project/Assets/VueLiveLink/VueTerrainOverlap.azsli').read_text()
        self.assertIn('option bool o_terrainHeightFp16 = false;',text)
        self.assertIn('half height = rawHeight * heightScale + heightOffset;',text)
        self.assertIn('TC_OverlapWeights16(heights, coverage',text)
        layout=json.loads((ROOT/'integrations/tg/project/Assets/VueLiveLink/VueTerrainOverlap.materialtype').read_text())
        props=[p for g in layout['propertyLayout']['propertyGroups'] for p in g.get('properties',[])]
        self.assertTrue(any(p.get('connection')=={'type':'ShaderOption','name':'o_terrainHeightFp16'} for p in props))
    def test_inspector_lookup_uses_the_editor_uuid_factory(self):
        path=ROOT/'integrations/tg/project/Gem/Source/Editor/VueLiveLink/terrain_overlap_editor.py'
        node=next(n for n in ast.parse(path.read_text(encoding='utf-8')).body if isinstance(n,ast.FunctionDef) and n.name=='height_precision')
        token=object();component=object()
        def read(c,path):
            self.assertIs(c,component);self.assertEqual(path,'Configuration|Height Precision');return 16
        def lookup(mode,event,owner,kind):
            self.assertIs(kind,token)
            return types.SimpleNamespace(IsSuccess=lambda:True,GetValue=lambda:component)
        def uuid(text):
            self.assertEqual(text,'{43588E85-B704-461F-AC94-8C9E1D916A9A}');return token
        api=types.SimpleNamespace(_get=read)
        env=dict(modules=lambda:(api,None),general=types.SimpleNamespace(find_editor_entity=lambda name:types.SimpleNamespace(IsValid=lambda:True)),
                 editor=types.SimpleNamespace(EditorComponentAPIBus=lookup),bus=types.SimpleNamespace(Broadcast=None),azmath=types.SimpleNamespace(Uuid_CreateString=uuid))
        exec(compile(ast.Module(body=[node],type_ignores=[]),str(path),'exec'),env)
        self.assertEqual(env['height_precision'](),16)

if __name__=='__main__':unittest.main()
