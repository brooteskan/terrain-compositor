from pathlib import Path
p=Path('D:/diversion/TG/TGProject/Gems/terrain-compositor/tests/tg/test_vue_terrain_overlap.py');s=p.read_text();needle='    def test_generated_layout_specializes_published_count_and_precision(self):'
test='''    def test_normal_quality_control_and_runtime_assets_ship_together(self):
        canonical = Path(__file__).resolve().parents[2]/'integrations/tg/project'
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory)
            folder = project/'Assets/VueLiveLink'
            folder.mkdir(parents=True)
            (folder/'VueTerrainMesh.materialtype').write_bytes(
                (PROJECT/'Assets/VueLiveLink/VueTerrainMesh.materialtype').read_bytes())
            overlap.install_material_layout(project)
            layout = json.loads((folder/'VueTerrainOverlap.materialtype').read_text())
        groups = layout['propertyLayout']['propertyGroups']
        quality = next(g for g in groups if g.get('name') == 'contributorSelection')
        detail = next(p for p in quality['properties'] if p['name'] == 'detailNormals')
        self.assertIs(detail['defaultValue'], False)
        self.assertEqual(detail['connection'],
            {'type':'ShaderOption', 'name':'o_terrainDetailNormalMaps'})
        self.assertIn({'$import':'VueTerrainNormalPropertyGroup.json'}, groups)
        self.assertIn({'type':'Lua', 'args':{'file':'Assets/VueLiveLink/VueTerrainNormalPolicy.lua'}},
            layout['functors'])
        normal = json.loads((canonical/'Assets/VueLiveLink/VueTerrainNormalPropertyGroup.json').read_text())
        self.assertNotIn('functors', normal)  # Exactly one owner of o_normal_useTexture.
        props = {p['name']:p for p in normal['properties']}
        self.assertIs(props['useTexture']['defaultValue'], True)
        self.assertEqual(props['textureMap']['connection'],
            {'type':'ShaderInput', 'name':'m_normalMap'})
        manifest = json.loads((canonical.parent/'manifest.json').read_text())
        for rel in ('Assets/VueLiveLink/VueTerrainNormalPropertyGroup.json',
                    'Assets/VueLiveLink/VueTerrainNormalPolicy.lua'):
            entry = next(e for e in manifest['files'] if e['path'] == rel)
            self.assertEqual((canonical.parent/entry['source']).read_bytes(), (PROJECT/rel).read_bytes())

'''
assert needle in s and 'def test_normal_quality_control_and_runtime_assets_ship_together' not in s;s=s.replace(needle,test+needle,1);p.write_text(s)
