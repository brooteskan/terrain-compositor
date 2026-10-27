from pathlib import Path
p=Path('D:/diversion/TG/TGProject/Gems/terrain-compositor/tests/tg/test_vue_terrain_overlap.py');s=p.read_text();needle='    def test_generated_layout_specializes_published_count_and_precision(self):'
test='''    def test_optimized_default_leaves_frozen_contributor_rejection_unkeyed(self):
        # Default adoption must not depend on a frozen terrain publication.
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory)
            folder = project/'Assets/VueLiveLink'
            folder.mkdir(parents=True)
            (folder/'VueTerrainMesh.materialtype').write_bytes(
                (PROJECT/'Assets/VueLiveLink/VueTerrainMesh.materialtype').read_bytes())
            overlap.install_material_layout(project)
            layout = json.loads((folder/'VueTerrainOverlap.materialtype').read_text())
            group = next(g for g in layout['propertyLayout']['propertyGroups'] if g.get('name') == 'contributorSelection')
            props = {p['name']:p for p in group['properties']}
            self.assertIs(props['enabled']['defaultValue'], True)
            self.assertEqual(props['enabled']['connection'],
                {'type':'ShaderOption', 'name':'o_terrainContributorTiles'})
            self.assertEqual(props['key']['defaultValue'], 0)
            self.assertEqual(props['key']['connection'],
                {'type':'ShaderInput', 'name':'m_vueContributorKey'})

'''
assert needle in s and 'def test_optimized_default_leaves_frozen' not in s;s=s.replace(needle,test+needle,1);p.write_text(s)
